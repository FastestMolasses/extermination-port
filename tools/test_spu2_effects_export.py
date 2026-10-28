#!/usr/bin/env python3
"""Synthetic output-scope guards; no original files or game assets needed."""
import importlib.util
from pathlib import Path
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("effect_export", ROOT / "tools/export_spu2_effects.py")
exporter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(exporter)


def denied(path, root):
    try:
        exporter.destinations(path, root)
    except ValueError:
        return
    raise AssertionError("unsafe output path was accepted")


def main():
    build = ROOT / "build/spu2-output-reverb"
    build.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="scope-", dir=build)).resolve()
    repo, outside = work / "private", work / "outside"
    assets = repo / "assets"
    for path in (repo, outside, assets):
        path.mkdir()
    original = outside / "user-file"
    original.write_text("must remain intact")
    try:
        output = assets / "effects.emrv"
        expected = (output, output.with_suffix(".json"))
        assert exporter.destinations(output, repo) == expected
        denied(outside / "effects.emrv", repo)
        denied(assets / ".." / ".." / "outside" / "effects.emrv", repo)
        denied(assets / "effects.json", repo)
        # Each leaf is checked separately, including a dangling leaf link.
        output.symlink_to(original)
        denied(output, repo)
        output.unlink()
        metadata = output.with_suffix(".json")
        metadata.symlink_to(original)
        denied(output, repo)
        metadata.unlink()
        metadata.symlink_to(outside / "does-not-exist")
        denied(output, repo)
        metadata.unlink()
        nested = assets / "nested"
        nested.symlink_to(outside, target_is_directory=True)
        denied(nested / "effects.emrv", repo)
        nested.unlink()
        assets.rmdir()
        assets.symlink_to(outside, target_is_directory=True)
        denied(output, repo)
        assets.unlink()
        assets.mkdir()
        alias = work / "alias"
        alias.symlink_to(repo, target_is_directory=True)
        denied(alias / "assets/effects.emrv", alias)
        alias.unlink()
        assert original.read_text() == "must remain intact"
        assert sorted(p.name for p in outside.iterdir()) == ["user-file"]
        print("spu2_effects_export: PASS (binary/JSON leaves, links, traversal, private root)")
    finally:
        # Every removed path is a named absolute test artifact.
        for path in (output, output.with_suffix(".json"), assets / "nested", work / "alias"):
            if path.is_symlink() or path.is_file():
                path.unlink()
        if assets.is_symlink():
            assets.unlink()
        elif assets.is_dir():
            assets.rmdir()
        original.unlink()
        outside.rmdir()
        repo.rmdir()
        work.rmdir()


if __name__ == "__main__":
    main()
