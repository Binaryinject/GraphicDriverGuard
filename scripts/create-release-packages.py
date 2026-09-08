#!/usr/bin/env python3
import argparse
import gzip
import hashlib
import io
import json
import os
from pathlib import Path
import tarfile


PACKAGE_FILES = (
    "package.json",
    "Editor/DriverGuardBuildProcessor.cs",
    "Editor/DriverGuardSettings.cs",
    "Editor/GraphicDriverGuard.Editor.asmdef",
    "Native~/Linux/x86_64/UnityPlayer.so",
    "Native~/README.md",
    "Native~/Windows/x86_64/UnityPlayer.dll",
    "LICENSE",
    "README.md",
    "README.zh-CN.md",
    "DriverGuard.example.ini",
)


def package_files(root: Path):
    for relative in PACKAGE_FILES:
        path = root / relative
        if not path.is_file():
            raise FileNotFoundError(f"Required package file is missing: {path}")
        yield path


def add_bytes(archive: tarfile.TarFile, name: str, data: bytes, mode: int = 0o644):
    info = tarfile.TarInfo(name)
    info.size = len(data)
    info.mode = mode
    info.mtime = 0
    archive.addfile(info, io.BytesIO(data))


def write_archive(output: Path, writer):
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("wb") as raw:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode="w", format=tarfile.USTAR_FORMAT) as archive:
                writer(archive)


def create_upm(root: Path, files, output: Path):
    def write(archive):
        for path in files:
            relative = path.relative_to(root).as_posix()
            mode = 0o755 if os.access(path, os.X_OK) else 0o644
            add_bytes(archive, f"package/{relative}", path.read_bytes(), mode)

    write_archive(output, write)


def unity_meta(guid: str) -> bytes:
    return f"fileFormatVersion: 2\nguid: {guid}\n".encode("utf-8")


def create_unitypackage(root: Path, files, output: Path):
    def write(archive):
        for path in files:
            relative = path.relative_to(root).as_posix()
            asset_path = f"Packages/com.fstgame.graphic-driver-guard/{relative}"
            guid = hashlib.md5(
                f"com.fstgame.graphic-driver-guard:{relative}".encode("utf-8")
            ).hexdigest()
            add_bytes(archive, f"{guid}/asset", path.read_bytes())
            add_bytes(archive, f"{guid}/asset.meta", unity_meta(guid))
            add_bytes(archive, f"{guid}/pathname", asset_path.encode("utf-8"))

    write_archive(output, write)


def main():
    parser = argparse.ArgumentParser(description="Create Graphic Driver Guard release archives.")
    parser.add_argument("--version", required=True)
    parser.add_argument("--output-dir", type=Path, default=Path("dist"))
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    args = parser.parse_args()

    root = args.root.resolve()
    package_version = json.loads((root / "package.json").read_text(encoding="utf-8"))["version"]
    if args.version != package_version:
        raise ValueError(f"Release version {args.version} does not match package.json {package_version}")

    files = list(package_files(root))
    base_name = f"GraphicDriverGuard-{args.version}"
    create_upm(root, files, args.output_dir / f"{base_name}.tgz")
    create_unitypackage(root, files, args.output_dir / f"{base_name}.unitypackage")
    print(f"Created release packages in {args.output_dir}")


if __name__ == "__main__":
    main()
