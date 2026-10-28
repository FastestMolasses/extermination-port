#!/usr/bin/env python3
"""Synthetic observer equivalence and history checks; native macOS arm64.

Prepares no game assets and launches no game. Makes a fresh private source
copy, compiles canonical and private audio sources, compares their PCM and
pointer-normalized state bytes, then checks sparse source histories. All
generated files stay under ignored build/spu2-a2/observer-test.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import uuid
from prepare_spu2_observer import finalize, prepare, private_path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/spu2-a2/observer-test"
ROW = struct.Struct("<QII4h6H3fQII")

FIXTURE = r'''
#include "game/em_iop_stream.h"
#include "game/em_sfx_bank.h"
#ifdef OBSERVED
#include "game/em_spu2_observer_capture.h"
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t counter;
uint32_t em_frame_counter(void) { return counter; }
uint64_t em_frame_vblanks(void) { return counter; }

static void command(EmSfxDriver *d, uint64_t frame, unsigned c,
                    unsigned v, uint32_t a, uint32_t b)
{
#ifdef OBSERVED
    const uint64_t mask = a2_sfx_effective(d, c, a, b);
#else
    (void)frame;
#endif
    em_sfx_driver_apply(d, (int)c, (int)v, a, b);
#ifdef OBSERVED
    const uint32_t words[4] = {c,v,a,b};
    a2_sfx_command(d, frame, words, mask);
#endif
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    FILE *out = fopen(argv[1], "wb");
    assert(out);
    int16_t pcm[84];
    for (unsigned i=0;i<84;i++) pcm[i]=(int16_t)((int)i*311-12000);
    EmSfxSample sample = {56,28,pcm};
    EmSfxRegistry registry = {.sample_count=1,.samples=&sample};
    EmSfxDriver driver;
    em_sfx_driver_init(&driver,&registry,0);
#ifdef OBSERVED
    a2_sfx_init(&driver);
#endif
    for (unsigned v=4;v<6;v++) {
        command(&driver,0,1,v,0x3000+v,0x1000+v);
        command(&driver,0,3,v,0x000f,0x1fc1);
        command(&driver,0,5,v,0,0);
        command(&driver,0,6,v,v==4?0x1801:0x3fff,0);
    }
    command(&driver,0,10,0,0x30,0);
    command(&driver,0,11,0,0x30,0); /* ignored in the same exchange */
    unsigned cursor=0;
    const unsigned sizes[]={1,27,29,113,7,263,8,501,319,257};
    for (unsigned i=0;i<sizeof sizes/sizeof sizes[0];i++) {
        float data[2*501]={0};
        if (i==4) command(&driver,cursor,6,4,0x0801,0);
        if (i==7) command(&driver,cursor,11,0,0x20,0);
        if (i==8) command(&driver,cursor,10,0,0x20,0);
#ifdef OBSERVED
        a2_sfx_render(cursor,sizes[i]);
#endif
        em_sfx_driver_render(&driver,data,sizes[i],48000);
        assert(fwrite(data,sizeof(float),sizes[i]*2,out)==sizes[i]*2);
#ifdef OBSERVED
        a2_mix_range(0,cursor,sizes[i],sizes[i]);
#endif
        cursor+=sizes[i];
        counter++;
    }
    driver.registry=NULL;
    for (unsigned v=0;v<48;v++) {
        driver.voices[v].sample=NULL; driver.voices[v].next_sample=NULL;
    }
    assert(fwrite(&driver,1,sizeof driver,out)==sizeof driver);

    EmIopStream *s=em_iop_stream_create();
    assert(s);
    uint8_t *ram=em_iop_stream_iop_ram(s);
    for (unsigned i=0;i<0x800;i+=16) {
        ram[0x10000+i]=0x02; ram[0x10000+i+1]=2;
        for (unsigned j=2;j<16;j++) ram[0x10000+i+j]=(uint8_t)(i+j*7);
    }
    assert(em_iop_stream_001157F0(s,0x3e,0x00010200,0x10000008,0x00010000)>0);
    assert(em_iop_stream_001157F0(s,0x40,1,0,0x28001800)>0);
    assert(em_iop_stream_001157F0(s,0x41,1,0,72000)>0);
    assert(em_iop_stream_001157F0(s,0x42,1,0,0)>0);
    assert(em_iop_stream_001157F0(s,0x16,0,0x1200,0x2300)>0);
    uint64_t last=0;
    for (unsigned i=0;i<5;i++) {
        if (i==3) {
            assert(em_iop_stream_drv_push(s,0x1011,1,0,0)==0);
            assert(em_iop_stream_drv_consume(s)==0);
        }
        assert(em_iop_stream_field(s)==0);
        uint64_t now=0;
        em_iop_stream_clock(s,NULL,NULL,&now);
        float data[2*802]={0};
        assert(now-last<=802);
        em_iop_stream_mix(s,data,(int)(now-last),48000);
        assert(fwrite(data,sizeof(float),(now-last)*2,out)==(now-last)*2);
        last=now; counter++;
    }
    assert(fwrite(em_iop_stream_driver(s),1,sizeof(EmIopDriver),out)==sizeof(EmIopDriver));
    for (unsigned v=0;v<48;v++)
        assert(fwrite(em_iop_stream_spu_voice(s,(int)v),1,sizeof(EmIopSpuVoice),out)==sizeof(EmIopSpuVoice));
    em_iop_stream_destroy(s);
    assert(fclose(out)==0);
    return 0;
}
'''


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run_fixture(private):
    OUT.mkdir(parents=True, exist_ok=True)
    guard = OUT / "guard-fixture"
    guard.mkdir(exist_ok=True)
    alias = guard / "alias"
    metadata = guard / "capture.json"
    original_hash = sha(ROOT / "Makefile")
    guard_checks = 0
    try:
        alias.symlink_to(ROOT / "src", target_is_directory=True)
        metadata.symlink_to(ROOT / "Makefile")
        operations = (
            lambda: private_path(OUT, fresh=True),
            lambda: private_path(alias / "game"),
            lambda: private_path(ROOT / "build/../src"),
            lambda: finalize(guard),
        )
        for operation in operations:
            try:
                operation()
            except ValueError:
                guard_checks += 1
            else:
                raise AssertionError("unsafe observer output path was accepted")
        assert sha(ROOT / "Makefile") == original_hash
    finally:
        # Named, absolute generated files only; no recursive cleanup.
        alias.unlink()
        metadata.unlink()
        guard.rmdir()
    source = OUT / "fixture.c"
    source.write_text(FIXTURE)
    names = ("em_iop_stream.c", "em_stream_lanes_original.c", "em_sfx_bank.c")
    for name, base in (("canonical", ROOT), ("private", private)):
        command = ["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                   "-ffp-contract=off", "-I" + str(base / "src"), str(source)]
        command += [str(base / "src/game" / n) for n in names]
        if name == "private":
            command += ["-DOBSERVED", str(private / "src/game/em_spu2_observer_capture.c")]
        command += ["-o", str(OUT / name)]
        result = subprocess.run(command, capture_output=True, text=True)
        (OUT / f"{name}-compile.log").write_text(result.stdout + result.stderr)
        assert result.returncode == 0, f"compile failed: {name}"
    env = os.environ.copy()
    env.pop("EM_SPU2_OBSERVER_DIR", None)
    env.pop("EM_SPU2_OUTPUT_TEST", None)
    capture = OUT / "capture"
    capture.mkdir(exist_ok=True)
    for name, binary, observe in (("canonical", "canonical", False),
                                  ("disabled", "private", False),
                                  ("enabled", "private", True)):
        current = env.copy()
        if observe:
            current["EM_SPU2_OBSERVER_DIR"] = str(capture)
            current["EM_REPLAY_AUDIO"] = str(OUT / "unused.wav")
        result = subprocess.run([str(OUT / binary), str(OUT / f"{name}.data")],
                                env=current, capture_output=True, text=True)
        (OUT / f"{name}-run.log").write_text(result.stdout + result.stderr)
        assert result.returncode == 0, f"fixture failed: {name}"
    hashes = {n: sha(OUT / f"{n}.data") for n in ("canonical", "disabled", "enabled")}
    assert len(set(hashes.values())) == 1, "observer changed PCM or model state"
    events = [json.loads(line) for line in (capture / "events.jsonl").read_text().splitlines()]
    keys = [e for e in events if e["type"] == "sfx_command" and e["words"][0] == 11]
    assert [e["effective_mask"] for e in keys] == [0, 32]
    assert any(e["type"] == "stream_dma" for e in events)
    assert any(e["type"] == "stream_block" for e in events)
    assert any(e["type"] == "stream_register" for e in events)
    # Blocks loaded by KON apply before that frame; advancing off a block
    # occurs after output, so ordinary block loads apply on the next frame.
    # Reconstruct the four samples independently from the complete decoded
    # block blobs, including previous-block history.
    keyons = [e for e in events if e["type"] == "stream_register" and
              e["kind"] == "switch" and e["reg"] & 0xfffe == 0x1500]
    blobs = (capture / "sources.bin").read_bytes()
    blocks, prior = {}, {}
    for event in events:
        if event["type"] != "stream_block":
            continue
        voice = event["voice"]
        keyed = any(e["frame"] == event["frame"] and e["seq"] > event["seq"] and
                    e["reg"] & 1 == voice // 24 and e["value"] >> (voice % 24) & 1
                    for e in keyons)
        values = list(struct.unpack_from("<28h", blobs, event["pcm_offset"]))
        history = [0, 0, 0] if keyed else prior[voice][-3:]
        blocks.setdefault(voice, []).append(
            (event["frame"] + (0 if keyed else 1), event["address"], history + values))
        prior[voice] = values
    data = (capture / "voice-frames.bin").read_bytes()
    previous, counts = {}, [0, 0]
    # Independent history oracle: sample values are a synthetic ramp, and
    # each key-on starts with zero history. Fractional frames do not shift it.
    histories, indices = {}, {}
    rekey_frame = 1 + 27 + 29 + 113 + 7 + 263 + 8 + 501
    for row in ROW.iter_unpack(data):
        frame, domain, voice = row[:3]
        key = domain, voice
        assert frame > previous.get(key, -1), "duplicate/unordered voice frame"
        previous[key] = frame
        counts[domain] += 1
        if domain:
            available = [entry for entry in blocks[voice] if entry[0] <= frame]
            _, address, history = available[-1]
            position = row[16] - address * 28 // 16
            assert 0 <= position < 28
            assert list(row[3:7]) == history[position:position + 4]
            assert row[15] == history[position + 3]
            continue
        index = row[16]
        if key not in histories or (voice == 5 and frame == rekey_frame):
            histories[key] = [0, 0, 0, index * 311 - 12000]
        else:
            at = indices[key]
            while at != index:
                at += 1
                if at >= 84:
                    at -= 28
                histories[key] = histories[key][1:] + [at * 311 - 12000]
        indices[key] = index
        assert list(row[3:7]) == histories[key], "captured history is incorrect"
    assert min(counts) > 0
    report = {"schema": "spu2-observer-synthetic-test-v1", "pass": True,
              "source": str(source), "source_sha256": sha(source),
              "prepared_sources_sha256": sha(OUT / "prepared-source-provenance.json"),
              "pcm_and_state_sha256": hashes, "rows_per_domain": counts,
              "private_path_rejection_cases": guard_checks,
              "checks": ["canonical/private-disabled/private-enabled exact PCM and state",
                         "SFX loop history and pitch changes", "same-exchange ignored KOFF",
                         "later accepted KOFF and rekey", "stream DMA/block/register records",
                         "stream history across decoded blocks with after-output phase advance",
                         "strictly increasing per-voice frame positions"],
              "game_run": False, "original_payload": False}
    (OUT / "receipt.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"pass": True, "receipt": str(OUT / "receipt.json"),
                      "rows_per_domain": counts, "game_run": False}))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    private = OUT / ("source-copy-" + uuid.uuid4().hex)
    removed = []
    try:
        prepare(False, private)
        (OUT / "prepared-source-provenance.json").write_bytes((private / "provenance.json").read_bytes())
        (OUT / "private-instrumentation.patch").write_bytes((private / "private-instrumentation.patch").read_bytes())
        run_fixture(private)
    finally:
        # This exact fresh directory was generated above. Enumerate every
        # absolute file, unlink it by name, then remove only empty directories.
        if private.exists():
            paths = sorted(private.rglob("*"), key=lambda p: len(p.parts), reverse=True)
            if any(path.is_symlink() for path in paths):
                raise ValueError("unexpected link in generated source copy")
            for path in paths:
                if path.is_file():
                    removed.append(str(path))
                    path.unlink()
                elif path.is_dir():
                    path.rmdir()
            private.rmdir()
        for binary in (OUT / "canonical", OUT / "private"):
            if binary.is_file() and not binary.is_symlink():
                removed.append(str(binary))
                binary.unlink()
        (OUT / "cleanup.json").write_text(json.dumps({"removed_generated_files": removed,
                                                    "retained": "logs, receipt, provenance, patch, synthetic data"}, indent=2) + "\n")


if __name__ == "__main__":
    main()
