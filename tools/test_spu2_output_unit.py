#!/usr/bin/env python3
"""Fast, synthetic SPU2 stage tests and deliberate production-code mistakes.

No emulator, disc assets, audio device, or waveform claim. Every mutant must
compile successfully and then fail the same unmodified assertions.
"""
from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/spu2-output/unit"
DSP = "src/game/em_spu2_dsp.c"
REVERB = "src/game/em_spu2_reverb.c"
OUTPUT = "src/game/em_spu2_output.c"


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    flags = ["cc", "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
             "-ffp-contract=off", "-fsanitize=address,undefined",
             "-fno-omit-frame-pointer", "-Isrc", "-Isrc/game"]
    groups = [
        ("dsp", ["tests/spu2_dsp_test.c", DSP],
         ["EM_SPU2_MUTATE_GAUSSIAN", "EM_SPU2_MUTATE_ADSR", "EM_SPU2_MUTATE_VOLUME"]),
        ("effects", ["tests/spu2_effects_test.c", "src/game/em_spu2_effects.c",
                     REVERB], []),
        ("output", ["tests/spu2_output_test.c", OUTPUT, DSP, REVERB,
                    "src/game/em_sfx_bank.c"], ["EM_SPU2_OUTPUT_MUTATE_EON"]),
    ]
    results = []
    for group, sources, mutants in groups:
        for mutant in [None, *mutants]:
            name = group + ("-" + mutant.lower() if mutant else "")
            binary = OUT / name
            command = flags + ([f"-D{mutant}"] if mutant else []) + sources + ["-o", str(binary)]
            built = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
            (OUT / f"{name}-build.log").write_text(built.stdout + built.stderr)
            if built.returncode:
                raise RuntimeError(f"{name}: compilation failed; see {OUT / (name + '-build.log')}")
            ran = subprocess.run([str(binary)], cwd=ROOT, capture_output=True, text=True)
            (OUT / f"{name}.log").write_text(ran.stdout + ran.stderr)
            # A sanitizer crash does not count as detecting a semantic mutation.
            sanitizer = any(word in ran.stderr for word in
                            ("AddressSanitizer", "UndefinedBehaviorSanitizer", "runtime error:"))
            accepted = not sanitizer and (ran.returncode == 1 if mutant else ran.returncode == 0)
            results.append({"name": name, "mutant": mutant, "exit_code": ran.returncode,
                            "passed": accepted})
            (OUT / "results.json").write_text(json.dumps(results, indent=2) + "\n")
            if not accepted:
                raise RuntimeError(f"{name}: unexpected result; see {OUT / (name + '.log')}")
            print(f"{name}: {'mistake rejected' if mutant else 'PASS'}")
            binary.unlink()
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError) as exc:
        print(f"SPU2 unit tests: {exc}", file=sys.stderr)
        raise SystemExit(1)
