#!/usr/bin/env python3
"""Build a private native SPU2 observer; never edit tracked production sources.

Native arm64 macOS tooling, standard library only. The documented observation
patch is applied only in ignored build/spu2-a2/native-observer. The additional
private hooks capture source/DMA and legacy voice inputs without replacing the
mixer. A separate replay anchor hook is identical when observation is disabled.
No game run is started here. Capture payloads remain local and ignored.
"""
from __future__ import annotations
import argparse
import difflib
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_DEST = ROOT / "build/spu2-a2/native-observer"


def private_path(path, fresh=False):
    if ".." in path.parts:
        raise ValueError("private path cannot contain traversal")
    path = path if path.is_absolute() else ROOT / path
    if not path.is_relative_to(ROOT / "build"):
        raise ValueError("private path must be under this worktree's build/")
    if any(part.is_symlink() for part in (path, *path.parents)):
        raise ValueError("private path cannot contain symlinks")
    if fresh and path.exists():
        raise ValueError("private destination already exists; choose a fresh --out path")
    return path


def sha(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while data := stream.read(1 << 20):
            digest.update(data)
    return digest.hexdigest()


def replace(path, old, new, count=1):
    text = path.read_text()
    if text.count(old) != count:
        raise RuntimeError(f"{path.name}: expected {count} exact anchors, got {text.count(old)}")
    path.write_text(text.replace(old, new))


def finalize(directory):
    directory = private_path(directory)
    path = private_path(directory / "capture.json")
    if not path.is_file():
        raise ValueError("capture metadata must be an existing regular file")
    meta = json.loads(path.read_text())
    assert meta["schema"] == "spu2-native-observer-v1" and meta["complete"]
    names = ("events.jsonl", "voice-frames.bin", "sources.bin")
    for name in names:
        candidate = private_path(directory / name)
        if not candidate.is_file():
            raise ValueError("capture payload must be an existing regular file")
    meta["sha256"] = {name: sha(directory / name) for name in names}
    meta["bytes"] = {name: (directory / name).stat().st_size for name in names}
    assert meta["bytes"]["voice-frames.bin"] == meta["voice_records"] * 64
    meta["layout"] = [
        ["frame", 0, "u64"], ["domain", 8, "u32"], ["voice", 12, "u32"],
        ["history_oldest_first", 16, "i16[4]"], ["fraction_q12", 24, "u16"],
        ["legacy_envx", 26, "u16"], ["voll", 28, "u16"], ["volr", 30, "u16"],
        ["adsr1", 32, "u16"], ["adsr2", 34, "u16"],
        ["legacy_gain_l", 36, "f32"], ["legacy_gain_r", 40, "f32"],
        ["legacy_source_pcm_units", 44, "f32"], ["source_index", 48, "u64"],
        ["source_id", 56, "u32"], ["reserved", 60, "u32"]]
    meta["endianness"] = "little"
    meta["row_order"] = "native render order; SFX is voice-major within each render_range; sort by domain/frame/voice"
    path.write_text(json.dumps(meta, indent=2) + "\n")
    print(json.dumps({"capture": str(path), "rows": meta["voice_records"],
                      "bytes": meta["bytes"], "payload_printed": False}))


def prepare(build, destination=DEFAULT_DEST):
    DEST = private_path(destination, fresh=True)
    DEST.mkdir(parents=True, exist_ok=False)
    tracked = subprocess.check_output(["git", "ls-files", "-z", "src", "Makefile"], cwd=ROOT).split(b"\0")
    paths = [Path(p.decode()) for p in tracked if p]
    before = {str(p): sha(ROOT / p) for p in paths}
    for relative in paths:
        target = DEST / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / relative, target)
    for suffix in ("c", "h"):
        shutil.copy2(ROOT / f"tools/spu2_observer_capture.{suffix}",
                     DEST / f"src/game/em_spu2_observer_capture.{suffix}")
    docs = (ROOT / "docs/SPU2_OUTPUT.md").read_text()
    section = docs.index("### Exact observation patch for main")
    start = docs.index("```diff\n", section) + len("```diff\n")
    end = docs.index("\n```", start)
    observation = docs[start:end] + "\n"
    (DEST / "documented-observation.patch").write_text(observation)
    applied = subprocess.run(["patch", "-p1", "--batch", "--forward"],
                             cwd=DEST, input=observation, text=True, capture_output=True)
    (DEST / "patch.log").write_text(applied.stdout + applied.stderr)
    if applied.returncode:
        raise RuntimeError("documented observer patch did not apply; see private patch.log")

    stream = DEST / "src/game/em_iop_stream.c"
    sfx = DEST / "src/game/em_sfx.c"
    bank = DEST / "src/game/em_sfx_bank.c"
    replay = DEST / "src/game/em_replay.c"
    live = DEST / "src/game/em_stream_live.c"
    for p in (stream, sfx, bank):
        replace(p, "#include <stdio.h>", '#include <stdio.h>\n#include "game/em_spu2_observer_capture.h"')
    replace(stream, "    int shift = b[0] & 0xF, filter = b[0] >> 4, i;",
            "    const int32_t observer_h1 = p->s1, observer_h2 = p->s2;\n"
            "    int shift = b[0] & 0xF, filter = b[0] >> 4, i;")
    replace(stream, "        p->block[i] = (int16_t)x;\n    }\n}",
            "        p->block[i] = (int16_t)x;\n    }\n"
            "    a2_stream_block(s->samples, (int)(p - s->spu), p, observer_h1, observer_h2);\n}")
    replace(stream, "    p->nax = p->lsa = p->ssa & ~15u;",
            "    a2_stream_key_on((int)(p - s->spu));\n    p->nax = p->lsa = p->ssa & ~15u;")
    replace(stream, "        if (s->tap)\n            s->tap(s->tap_ctx, v, p->block[p->pos]);",
            "        a2_stream_voice(s->samples, v, p);\n"
            "        if (s->tap)\n            s->tap(s->tap_ctx, v, p->block[p->pos]);")
    replace(stream, "    memcpy(s->spu_ram + spu_addr, data, size);",
            "    memcpy(s->spu_ram + spu_addr, data, size);\n"
            "    a2_stream_dma(s->samples, s->ticks, channel, spu_addr, data, size);")
    replace(stream, "    atomic_init(&s->bad_rate, 0);\n    return s;",
            "    atomic_init(&s->bad_rate, 0);\n    a2_stream_created(s);\n    return s;")
    replace(stream, "    s->half_lines = end;\n    return latched(s) ? -1 : 0;",
            "    s->half_lines = end;\n    a2_stream_field(s);\n    return latched(s) ? -1 : 0;")
    replace(stream, "    n = w - r < (uint64_t)frames ? w - r : (uint64_t)frames;",
            "    n = w - r < (uint64_t)frames ? w - r : (uint64_t)frames;\n"
            "    a2_mix_range(1, r, n, (unsigned)frames);")

    replace(sfx, "        s.driver.deferred = 1;",
            "        a2_sfx_init(&s.driver);\n        s.driver.deferred = 1;")
    replace(sfx, "        s.frames_mixed += em_spu2_output_render",
            "        a2_sfx_render(s.samples, count);\n        s.frames_mixed += em_spu2_output_render")
    replace(sfx, "        for (unsigned i = 0; i < s.ring_n; ++i)\n"
                 "            em_sfx_driver_apply(&s.driver, (int)s.ring[i][0], (int)s.ring[i][1], s.ring[i][2],\n"
                 "                                s.ring[i][3]);",
            "        for (unsigned i = 0; i < s.ring_n; ++i) {\n"
            "            const uint64_t observed = a2_sfx_effective(&s.driver, s.ring[i][0], s.ring[i][2], s.ring[i][3]);\n"
            "            em_sfx_driver_apply(&s.driver, (int)s.ring[i][0], (int)s.ring[i][1], s.ring[i][2],\n"
            "                                s.ring[i][3]);\n"
            "            a2_sfx_command(&s.driver, s.samples, s.ring[i], observed);\n        }")
    replace(sfx, "    const uint64_t n = w - r < (uint64_t)frames ? w - r : (uint64_t)frames;",
            "    const uint64_t n = w - r < (uint64_t)frames ? w - r : (uint64_t)frames;\n"
            "    a2_mix_range(0, r, n, (unsigned)frames);")
    replace(bank, "                out[2 * f] += value * left;",
            "                if (a2_enabled()) a2_sfx_voice((unsigned)i, f, voice, index, fraction,\n"
            "                                                a + (b - a) * fraction);\n"
            "                out[2 * f] += value * left;")

    # Comparison alignment is deliberately separate from observing sound.
    # Boot initializes the drive table in main before em_frame_run. Step C's
    # input filter precedes counter-keyed EE stream work, though field audio
    # for previously queued work already ran at the top of this frame.
    replace(replay, '#include "game/em_replay_audio.h"',
            '#include "game/em_replay_audio.h"\n#include "game/em_stream_live.h"')
    replace(replay, "            const RecSeg *g0 = &s.segs[s.cur_seg];",
            "            if (s.off == 0 && getenv(\"EM_PS2_DRIVE_REPLAY\")) {\n"
            "                char anchor[24];\n"
            "                snprintf(anchor, sizeof anchor, \"a2seg%u\", s.cur_seg);\n"
            "                em_stream_live_drive_replay_anchor(anchor, em_frame_counter());\n"
            "            }\n"
            "            const RecSeg *g0 = &s.segs[s.cur_seg];")
    replace(live, "void em_stream_live_shutdown(void)\n{\n",
            "void em_stream_live_shutdown(void)\n{\n"
            "    if (getenv(\"EM_PS2_DRIVE_REPLAY\")) em_stream_live_drive_report(stderr);\n")
    replace(DEST / "Makefile", "COMMON  := src/main.c",
            "COMMON  := src/game/em_spu2_observer_capture.c src/main.c")
    changed = [p for p in paths if sha(DEST / p) != before[str(p)]]
    patch = []
    for p in changed:
        patch.extend(difflib.unified_diff((ROOT / p).read_text().splitlines(True),
                     (DEST / p).read_text().splitlines(True), fromfile="a/" + str(p), tofile="b/" + str(p)))
    (DEST / "private-instrumentation.patch").write_text("".join(patch))
    anchor = []
    for p in (Path("src/game/em_replay.c"), Path("src/game/em_stream_live.c")):
        anchor.extend(difflib.unified_diff((ROOT / p).read_text().splitlines(True),
                      (DEST / p).read_text().splitlines(True), fromfile="a/" + str(p), tofile="b/" + str(p)))
    (DEST / "private-replay-anchor.patch").write_text("".join(anchor))
    if build:
        with (DEST / "build.log").open("w") as log:
            result = subprocess.run(["make", "-j2", "all"], cwd=DEST, stdout=log, stderr=subprocess.STDOUT)
        if result.returncode:
            raise RuntimeError("private native build failed; see build.log")
    after = {str(p): sha(ROOT / p) for p in paths}
    if after != before:
        raise RuntimeError("canonical tracked sources changed during preparation")
    report = {
        "schema": "spu2-private-observer-build-v1",
        "head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "source_copy": str(DEST), "built": build, "canonical_sources_unchanged": True,
        "changed_private_paths": [str(p) for p in changed],
        "private_sources_sha256": {str(p): sha(DEST / p) for p in changed},
        "templates_sha256": {name: sha(ROOT / "tools" / name)
                             for name in ("prepare_spu2_observer.py", "spu2_observer_capture.c", "spu2_observer_capture.h")},
        "observation_patch_sha256": hashlib.sha256(observation.encode()).hexdigest(),
        "anchor_patch_sha256": sha(DEST / "private-replay-anchor.patch"),
        "binary_sha256": sha(DEST / "build/extermination") if build else None,
        "capture_env": "EM_SPU2_OBSERVER_DIR=<private output dir>; requires EM_REPLAY_AUDIO",
        "baseline": "same private binary with EM_SPU2_OBSERVER_DIR unset; replay anchor hook identical",
        "run_started": False}
    (DEST / "provenance.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"prepared": str(DEST), "built": build, "canonical_sources_unchanged": True,
                      "provenance": str(DEST / "provenance.json"), "run_started": False}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", action="store_true")
    parser.add_argument("--out", type=Path, default=DEFAULT_DEST,
                        help="fresh private source/build directory; existing paths are never reused")
    parser.add_argument("--finalize", type=Path)
    args = parser.parse_args()
    if args.finalize:
        finalize(args.finalize)
    else:
        prepare(args.build, args.out)


if __name__ == "__main__":
    main()
