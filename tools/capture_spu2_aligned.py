#!/usr/bin/env python3
"""Capture an input-aligned original SPU2 mix and its EE observations on macOS.

Use the decomp virtualenv. Launch/ownership/cleanup belong exclusively to
pcsx2_session.ForkSession; replay and the 48 kHz PCM16 cut use ForkPlayer.
All outputs are private disc-derived data in a NEW ignored build directory.
No images, emulator implementation, register interpretation, or per-voice
isolation are involved. EE voice age and exchange replies are sampled at loop
boundaries, not at the actual SPU2 key-on or envelope update.
"""
from __future__ import annotations

import argparse
from contextlib import ExitStack
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import signal
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
VOICE_ADDR, VOICE_STRIDE, VOICE_COUNT = 0x27CCC0, 0x6A, 48
REPLY_ADDR, REPLY_SIZE = 0x2817C0, 0x200
SEQ_ADDR, SEQ_SIZE = 0x282150, 0x40
LANE_ADDR, LANE_STRIDE, LANE_COUNT = 0x281FD0, 0x60, 3
VOICE_WORDS = {
    "state": 0, "note": 2, "owner": 6, "release": 8, "serial": 0xA,
    "sustain": 0xC, "kind": 0x1A, "age": 0x1C, "priority": 0x1E,
    "alloc": 0x20, "bank_handle": 0x22, "program": 0x3E,
}


def utc():
    return datetime.now(timezone.utc).isoformat()


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def revision(path):
    return subprocess.check_output(
        ["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()


def fresh_output(path, decomp):
    """Reject aliases/escapes and preexisting destinations before any writes."""
    path = path.absolute()
    roots = (ROOT / "build", decomp / "build")
    if any(root.is_symlink() for root in roots):
        raise ValueError("build roots must not be symlinks")
    if any(part.is_symlink() for part in (path, *path.parents)):
        raise ValueError("output and its ancestors must not be symlinks")
    path = path.resolve()
    if not any(root.resolve() in path.parents for root in roots):
        raise ValueError("output must be a descendant of port/decomp build/")
    if path.exists():
        raise ValueError("output already exists; choose a fresh directory")
    return path


def parse_voices(raw, reply):
    if len(raw) != VOICE_STRIDE * VOICE_COUNT or len(reply) != REPLY_SIZE:
        raise ValueError("short EE voice/reply read")
    return [
        {"voice": voice, **{
            key: struct.unpack_from("<H", raw, voice * VOICE_STRIDE + off)[0]
            for key, off in VOICE_WORDS.items()},
         "envx_reply": struct.unpack_from("<i", reply, 4 * voice)[0],
         "stream_reply": struct.unpack_from("<i", reply, 4 * (48 + voice))[0]}
        for voice in range(VOICE_COUNT)
    ]


def parse_stream(seq, lanes):
    if len(seq) != SEQ_SIZE or len(lanes) != LANE_STRIDE * LANE_COUNT:
        raise ValueError("short EE stream read")
    if seq[7] == 2 and seq[8] >= LANE_COUNT:
        raise ValueError("invalid active stream lane")
    return {"phase": seq[7], "lane": seq[8], "mono": seq[0xB],
            "active": list(seq[4:7]),
            "cue": list(struct.unpack_from("<3i", seq, 0x28)), "lanes": [
        {"lane": lane, "state": lanes[lane * LANE_STRIDE],
         "load": lanes[lane * LANE_STRIDE + 3],
         "voice": struct.unpack_from("<i", lanes, lane * LANE_STRIDE + 4)[0],
         "voice_mask": struct.unpack_from("<Q", lanes, lane * LANE_STRIDE + 8)[0],
         "base_sector": struct.unpack_from("<I", lanes, lane * LANE_STRIDE + 0x2C)[0],
         "sector": struct.unpack_from("<i", lanes, lane * LANE_STRIDE + 0x30)[0],
         "count": struct.unpack_from("<i", lanes, lane * LANE_STRIDE + 0x34)[0],
         "volume_f32_bits": struct.unpack_from("<I", lanes, lane * LANE_STRIDE + 0x58)[0]}
        for lane in range(LANE_COUNT)]}


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, default=str) + "\n")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--recording", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--decomp", type=Path, default=ROOT.parent / "Extermination")
    ap.add_argument("--ticks", type=int, default=2400)
    ap.add_argument("--title-delay", type=int, choices=(0,), default=0)
    ap.add_argument("--lock-wait", type=float, default=30)
    ap.add_argument("--validate-only", action="store_true")
    args = ap.parse_args(argv)
    if args.ticks <= 0 or args.lock_wait < 0:
        ap.error("ticks must be positive and lock-wait nonnegative")
    decomp = args.decomp.resolve()
    try:
        out = fresh_output(args.out, decomp)
    except ValueError as error:
        ap.error(str(error))
    recording = args.recording.resolve(strict=True)
    sys.path.insert(0, str(decomp / "tools"))
    sys.path.insert(0, str(decomp / "tools/video_compare"))
    import emrec
    import ps2_fork
    from pcsx2_session import ForkSession
    from record_drive_replay import extract

    rec = emrec.read(recording)
    session = ForkSession(
        None, log_dir=out / "session", align=False, mtvu=False, renderer=13,
        lock=decomp / "build/.pcsx2.lock", lock_wait=args.lock_wait,
        rtc=ps2_fork.RTC, label="port-spu2-aligned-original")
    if session.visible or session.lock != decomp / "build/.pcsx2.lock":
        raise RuntimeError("expected hidden owned session under decomp run lock")
    if args.validate_only:
        print(json.dumps({"valid": True, "title_delay": args.title_delay,
                          "ticks": args.ticks, "output": str(out)}))
        return 0
    # Hash before launch, so the receipt identifies what this run consumed.
    provenance = {
        "recording": str(recording), "recording_sha256": digest(recording),
        "port_revision": revision(ROOT), "decomp_revision": revision(decomp),
        "tool_sha256": digest(Path(__file__)),
        "wrapper_sha256": digest(decomp / "tools/video_compare/ps2_fork.py"),
        "session_wrapper_sha256": digest(decomp / "tools/pcsx2_session.py"),
        "drive_extract_sha256": digest(ROOT / "tools/record_drive_replay.py"),
        "boot_iso": str(session.iso), "boot_iso_sha256": digest(session.iso),
        "probe_elf": str(decomp / "config/SCUS_971.12"),
        "probe_elf_sha256": digest(decomp / "config/SCUS_971.12"),
        "emulator": str(session.emulator), "emulator_sha256": digest(session.emulator),
        "requested_ticks": args.ticks, "requested_title_delay": args.title_delay,
        "mtvu": False, "renderer": 13, "rtc": ps2_fork.RTC,
        "hidden": True, "lock": str(session.lock),
    }
    out.mkdir(parents=True, exist_ok=False)
    player = ps2_fork.ForkPlayer(
        rec, out, 1000000, audio=True, field_k=0, extra_k=(),
        max_ticks=args.ticks, mtvu=False, renderer=13, title_delay=args.title_delay)
    player.arm = lambda cap: None
    player.capture_buffer = lambda cap, row: None
    player.capture = lambda k, cap, row: None
    observations, drive_samples, drive_observation_indices = [], [], []
    receipt = {"schema": 1, "started": utc(), "provenance": provenance,
               "isolation": "full mixed SPU2 output; no isolated voice reference",
               "key_on_boundary": "EE kind/age/serial at loop top, not exact SPU2 KON",
               "core_registers": {
                   "available": False,
                   "reason": "no documented typed SPU2 core/master/MMIX accessor in decomp wrappers; IOP RAM reads are not MMIO evidence"},
               "status": "running"}
    write_json(out / "receipt.json", receipt)

    def on_term(signum, frame):
        raise KeyboardInterrupt(f"signal {signum}")

    previous = signal.signal(signal.SIGTERM, on_term)
    try:
        with ExitStack() as files:
            voice_file = files.enter_context((out / "voices.bin").open("xb"))
            reply_file = files.enter_context((out / "replies.bin").open("xb"))
            stream_file = files.enter_context((out / "stream_states.bin").open("xb"))
            journal = files.enter_context((out / "observations.jsonl").open("x"))
            original_sample = player.sample

            def sample():
                row = original_sample()
                stage = ("play" if "title" in player.info else
                         "title" if "boot" in player.info else "boot")
                raw = player.rd(VOICE_ADDR, VOICE_STRIDE * VOICE_COUNT)
                reply = player.rd(REPLY_ADDR, REPLY_SIZE)
                seq = player.rd(SEQ_ADDR, SEQ_SIZE)
                lanes = player.rd(LANE_ADDR, LANE_STRIDE * LANE_COUNT)
                state = player.c.call("state")
                obs = {
                    "observation": len(observations), "stage": stage,
                    "row": len(player.rows) if stage == "play" else None,
                    **{k: row[k] for k in ("counter", "vsync", "area", "sub", "entry",
                                          "bd8", "s8d", "t9", "tb", "movie")},
                    "tap_samples": player.audio_samples() if "audio_start" in player.info else None,
                    **{k: state[k] for k in ("ee_cycle", "iop_cycle")},
                    "agent_vsync": state["vsync"],
                    "frame_idx": player.rd(ps2_fork.FRAME_IDX, 4)[0],
                    "field": player.rd(ps2_fork.FIELD_ADDR, 4)[0],
                    "voices": parse_voices(raw, reply),
                    "stream": parse_stream(seq, lanes),
                }
                voice_file.write(raw)
                reply_file.write(reply)
                stream_file.write(seq + lanes)
                observations.append(obs)
                # sample() is called twice at title/play transitions without a
                # field advance. These must not inflate the seek-field count.
                if drive_samples and drive_samples[-1][0] == row["counter"]:
                    drive_samples[-1] = (row["counter"], seq, lanes)
                    drive_observation_indices[-1] = obs["observation"]
                else:
                    drive_samples.append((row["counter"], seq, lanes))
                    drive_observation_indices.append(obs["observation"])
                journal.write(json.dumps(obs, separators=(",", ":")) + "\n")
                journal.flush()
                return row

            player.sample = sample
            with session as original:
                player.c = original._client
                receipt["hello"] = player.c.call("hello")
                receipt["owned_pid"] = session.pid
                write_json(out / "receipt.json", receipt)
                player.c.call("probe_add", pc=player.post_pc, action="stop",
                              label="aligned audio replay pad")
                player.c.call("probe_add", pc=ps2_fork.MOVIE_DRIVER, action="stop",
                              label="aligned audio replay movie")
                try:
                    player._drive(original._inst)
                finally:
                    if "audio_start" in player.info:
                        player.info["audio_stop"] = player.c.call("audio_tap", action="stop")
                if player.info["title"]["title_delay"] != args.title_delay:
                    raise RuntimeError("accepted title delay differs from request")
                player._collect_audio(original._inst)
                # Preserve the small timing marks, not a second copy of the tap.
                marks = Path(str(original._inst.root / "audio_tap.wav") + ".marks")
                (out / "audio_tap.marks").write_bytes(marks.read_bytes())
        receipt["status"] = "complete"
    except BaseException as error:
        receipt["status"] = "failed"
        receipt["error"] = f"{type(error).__name__}: {error}"
        raise
    finally:
        signal.signal(signal.SIGTERM, previous)
        player.pool.shutdown()
        receipt["finished"] = utc()
        receipt["session_closed"] = session._fs is None and not session._locked
        receipt["info"], receipt["notes"] = player.info, player.notes
        emrec.write(out / "ps2.rec", {"source": "pcsx2-fork", "audio": "fork-tap",
                                    "recording": str(recording), "title_delay": 0}, player.rows)
        first = player.info.get("play", {}).get("first_samples")
        for obs in observations:
            index = obs["row"]
            if index is not None and index < len(player.rows):
                row, extra = player.rows[index], player.extra[index]
                if row["counter"] != obs["counter"] or extra["tap_samples"] != obs["tap_samples"]:
                    receipt["status"] = "failed"
                    receipt["observation_mismatch"] = index
                obs.update({k: row[k] for k in ("step", "seg", "off", "ph", "af")})
            elif index is not None:
                obs["stage"] = "terminal"
                obs["row"] = None
            if first is not None and obs["tap_samples"] is not None:
                obs["af"] = obs["tap_samples"] - first
        with (out / "observations.jsonl").open("w") as stream:
            for obs in observations:
                stream.write(json.dumps(obs, separators=(",", ":")) + "\n")
        reads, omitted = extract(drive_samples, 1)
        by_counter = {observations[i]["counter"]: observations[i]
                      for i in drive_observation_indices}
        annotated = []
        for counter, sector, count, fields in reads:
            obs = by_counter[counter]
            annotated.append({"counter": counter, "sector": sector, "count": count,
                              "fields": fields, **{k: obs.get(k) for k in
                                  ("observation", "stage", "row", "seg", "off", "af")}})
        write_json(out / "drive_reads.json", {
            "counter_domain": "original; map native counters explicitly before replay",
            "extractor": "tools/record_drive_replay.py:extract",
            "sample_count": len(drive_samples), "reads": annotated, "omitted": omitted})
        with (out / "original.reads").open("w") as stream:
            stream.write("# ORIGINAL counters: counter sector count fields; map before native replay\n")
            for read in reads:
                stream.write("%d %d %d %d\n" % read)
        receipt["rows"] = len(player.rows)
        receipt["observations"] = len(observations)
        receipt["drive_reads"] = len(reads)
        receipt["drive_omitted"] = omitted
        receipt["raw_layout"] = {
            "voices.bin": {"record_bytes": VOICE_STRIDE * VOICE_COUNT,
                           "address": VOICE_ADDR, "voice_stride": VOICE_STRIDE},
            "replies.bin": {"record_bytes": REPLY_SIZE, "address": REPLY_ADDR},
            "stream_states.bin": {"record_bytes": SEQ_SIZE + LANE_STRIDE * LANE_COUNT,
                                  "sequence_address": SEQ_ADDR, "sequence_bytes": SEQ_SIZE,
                                  "lanes_address": LANE_ADDR, "lane_stride": LANE_STRIDE},
            "index": "record index equals observations.jsonl observation, including boot/title/terminal",
        }
        for name in ("audio.wav", "ps2.rec", "observations.jsonl", "original.reads",
                     "voices.bin", "replies.bin", "stream_states.bin", "audio_tap.marks"):
            path = out / name
            if path.is_file():
                receipt.setdefault("output_sha256", {})[name] = digest(path)
        write_json(out / "receipt.json", receipt)
    print(json.dumps({k: receipt[k] for k in
                      ("status", "rows", "observations", "drive_reads", "session_closed")}))
    return 0 if receipt["status"] == "complete" and receipt["session_closed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
