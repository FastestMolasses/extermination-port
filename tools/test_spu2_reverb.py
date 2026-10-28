#!/usr/bin/env python3
"""Fast synthetic reverb cases plus six real source-mutation controls."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    folder = ROOT / "build/spu2-output-reverb"
    folder.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="check-", dir=folder))
    source = (ROOT / "src/game/em_spu2_reverb.c").read_text()
    mutations = {
        "crossfeed": ("p->diff_read[channel ^ 1u]", "p->diff_read[channel]"),
        "effect-wrap": ("r->end - r->start + 1", "r->end - r->start + 2"),
        "negative-rounding": ("return -((-product + 32767) / 32768);", "return product / 32768;"),
        "return-volume": ("multiply(upsampled, r->return_volume[side])",
                          "multiply(upsampled, (int16_t)(r->return_volume[side] / 2))"),
        "resample-filter": ("10246, 16384, 10246", "10246, 8192, 10246"),
        "disabled-writes": ("if (r->write_enabled)\n        r->ram",
                            "if (1)\n        r->ram"),
    }
    paths = []
    try:
        for name in ["baseline", *mutations]:
            text = source
            if name != "baseline":
                old, new = mutations[name]
                assert source.count(old) == 1, f"{name}: mutation anchor changed"
                text = source.replace(old, new)
            src, exe = work / f"{name}.c", work / name
            paths.extend([src, exe])
            src.write_text(text)
            subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                            "-Werror", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                            "-I", str(ROOT / "src/game"), str(src),
                            str(ROOT / "tests/spu2_reverb_test.c"), "-o", str(exe)], check=True)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            if name == "baseline":
                assert result.returncode == 0, result.stderr
                print(result.stdout.strip())
            else:
                assert result.returncode != 0, f"{name}: deliberate mistake escaped"
                assert "Assertion failed:" in result.stderr, (
                    f"{name}: failed for an unexpected reason", result.stderr)
                print(f"mutation {name}: rejected")
    finally:
        for path in paths:
            if path.exists():
                path.unlink()
        work.rmdir()


if __name__ == "__main__":
    main()
