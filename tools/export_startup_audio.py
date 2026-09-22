#!/usr/bin/env python3
"""Export the five startup cues from the user's local disc extraction.

Unlike the older single-sample SFX registry this preserves every note, delta,
SPU pitch register, stereo volume register and ADSR/effect flag. No game data
is embedded here. Outputs belong in ignored assets/startup_audio/.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import struct
from pathlib import Path

CUES = (5, 0x5DC, 0x5DD, 0x5DE, 0x5DF)


def scripts(data, pos):
    """The A0 subset used by these cues; reject unimplemented instructions."""
    events, wait = [], 0
    limit = min(len(data), pos + 4096)
    while pos < limit:
        if data[pos:pos + 3] == b"\xff\x2f\x00":
            return events
        if data[pos] != 0xA0 or pos + 4 >= limit:
            raise ValueError(f"unsupported startup script opcode at {pos:#x}")
        events.append(dict(note=data[pos + 1], velocity=data[pos + 2],
                           program=data[pos + 3], wait=wait))
        pos += 4
        delta = 0
        for _ in range(5):
            if pos >= limit:
                raise ValueError("truncated script delta")
            b = data[pos]
            pos += 1
            delta = (delta << 7) | (b & 127)
            if not b & 128:
                break
        else:
            raise ValueError("oversized script delta")
        wait += delta
    raise ValueError("startup script lacks end marker")


def export(root: Path, out: Path):
    spec = importlib.util.spec_from_file_location("local_audio_export",
                                                 root / "tools/audio_export.py")
    audio = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(audio)
    elf = audio.ElfImage(root / "config/SCUS_971.12")
    source = root / "extract/chunk00/f05_id05.bin"
    data = source.read_bytes()
    container = audio.parse_container(data)
    if container is None:
        raise ValueError("invalid global SShd container")
    groups = {}
    for i, bank in enumerate(container["banks"]):
        groups.setdefault(bank["type"], []).append((i, bank))
    u16 = lambda p: struct.unpack_from("<H", data, p)[0]
    u32 = lambda p: struct.unpack_from("<I", data, p)[0]
    e16 = lambda p: struct.unpack("<H", elf.read(p, 2))[0]
    samples, cues = {}, []
    out.mkdir(parents=True, exist_ok=True)
    for cue in CUES:
        table = 0x25ECA0 if cue < 0x5DC else 0x261570
        index = cue if cue < 0x5DC else cue - 0x5DC
        group, bi, sg, si = struct.unpack("4b", elf.read(table + index * 4, 4))
        row, bank = groups[group][bi]
        hd = bank["hd"]
        pos = audio.parse_script_offsets(data, hd)[sg][si]
        defaults = hd + u32(hd + 0x20)
        # All 48 default track channels must agree for this fixed export.
        channel = data[defaults + 0x10:defaults + 0x20]
        for slot in range(48):
            other = data[defaults + 0x10 + 16 * slot:defaults + 0x20 + 16 * slot]
            if any(other[j] != channel[j] for j in (3, 4, 12, 14)):
                raise ValueError("track-dependent bank defaults need runtime support")
        result = dict(id=cue, record=[group, bi, sg, si], bank=row, events=[])
        for event in scripts(data, pos):
            region = hd + u32(hd + 0x24)
            if event["program"] > u16(region):
                raise ValueError("invalid program index")
            program = region + u16(region + 2 + event["program"] * 2)
            note = event["note"]
            if data[program] != 255 or not data[program + 6] <= note <= data[program + 7]:
                raise ValueError("startup cue needs unsupported program mapping")
            tone = program + 8 + 16 * (note - data[program + 6])
            center, fine = data[tone + 2], struct.unpack_from("b", data, tone + 3)[0]
            # 00115850 sets bend=64 before 00117918. Use the actual table,
            # never a floating exponential or the old exporter's bend=0.
            distance = abs(note - center)
            octave, semitone = divmod(distance, 12)
            index = (semitone if note >= center else 12 - semitone) * 16 + fine + 0xD0
            pitch = e16(0x241D70 + 2 * index)
            pitch = pitch << octave if note >= center else pitch >> (octave + 1)
            pitch = pitch * 44100 // 48000
            pan = e16(0x242630 + 2 * (data[tone + 12] >> 2))
            velocity = data[hd + u32(hd + 0x14) + event["velocity"] + 2]
            gain = (channel[14] * channel[3] * data[tone + 11] * velocity
                    * data[program + 1] * data[defaults]) >> 27
            left = (((gain * (pan >> 8) * 4096) >> 19) & 65535) >> 1
            right = (((gain * (pan & 255) * 4096) >> 19) & 65535) >> 1
            if data[tone + 10] or data[tone + 15] & 0x22:
                raise ValueError("startup tone needs sweep/modulation/noise support")
            raw = audio.vag_block_at(data, bank["body_base"] + (u16(tone + 4) << 3))
            if not raw or raw[-15] & 3 != 1:
                raise ValueError("startup tone is not a finite one-shot")
            key = hashlib.sha256(raw).hexdigest()
            if key not in samples:
                sample_id = len(samples)
                name = f"sample_{sample_id:02d}.wav"
                audio.write_wav(out / name, audio.decode_adpcm(raw), 48000, 1)
                samples[key] = dict(id=sample_id, file=name, adpcm_sha256=key)
            event.update(sample=samples[key]["id"], pitch=pitch, left=left, right=right,
                         adsr1=u16(tone + 6), adsr2=u16(tone + 8), flags=data[tone + 15],
                         tick=(event["wait"] + 7) // 8)
            result["events"].append(event)
        cues.append(result)
    manifest = ["EMSA 1", f"samples {len(samples)}"]
    for sample in samples.values():
        manifest.append(f"sample {sample['id']} {sample['file']}")
    manifest.append(f"cues {len(cues)}")
    for cue in cues:
        manifest.append(f"cue {cue['id']} {len(cue['events'])}")
        for e in cue["events"]:
            manifest.append("event " + " ".join(str(e[k]) for k in
                ("wait", "sample", "pitch", "left", "right", "adsr1", "adsr2", "flags")))
    (out / "startup_audio.txt").write_text("\n".join(manifest) + "\n")
    report = dict(source="extract/chunk00/f05_id05.bin", source_sha256=hashlib.sha256(data).hexdigest(),
                  sequencer="001152D8: 0x1E0000 / 60 per VBlank, delta << 12",
                  pitch="00115850/00117918, D_00241D70, bend 64, integer 44100/48000",
                  volume="001179E0/00117BA0, D_00242630, stereo, track gains 4096",
                  renderer_limit="Native PCM rendering is dry linear interpolation; SPU2 ADSR, Gaussian interpolation and effects remain unimplemented.",
                  samples=list(samples.values()), cues=cues)
    (out / "provenance.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Exported {len(cues)} cues, {sum(len(c['events']) for c in cues)} notes, "
          f"{len(samples)} samples to {out}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--decomp-root", required=True, type=Path)
    parser.add_argument("--out", type=Path, default=Path("assets/startup_audio"))
    args = parser.parse_args()
    export(args.decomp_root.resolve(), args.out.resolve())
