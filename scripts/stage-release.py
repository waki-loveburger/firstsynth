#!/usr/bin/env python3
"""Collect release files in a locally synchronized Google Drive folder."""
import argparse
import hashlib
import re
import shutil
from pathlib import Path

EXTENSIONS = {'mac': {'.dmg'}, 'windows': {'.exe', '.msi', '.zip'}}

def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''): h.update(chunk)
    return h.hexdigest()

def release_dir(root, version):
    if not re.fullmatch(r'\d+\.\d+\.\d+', version): raise ValueError('Expected X.Y.Z version')
    return Path(root) / 'releases' / version

def names_version(name, version):
    # 1.0.8 or the release-zip shorthand v1.08, not as part of 1.0.80 / 11.0.8
    major_minor, patch = version.rsplit('.', 1)
    return any(re.search(rf'(?<!\d)(?<!\d\.){re.escape(token)}(?!\.?\d)', name)
               for token in (version, 'v' + major_minor + patch))

def artifacts(folder, platform):
    # Sort by name, not Path: WindowsPath compares case-insensitively, so the order
    # (and the checksum text finalize compares) would differ between Mac and Windows.
    return sorted((p for p in folder.iterdir() if p.is_file() and p.suffix.lower() in EXTENSIONS[platform]),
                  key=lambda p: p.name)

def manifest(path, files, base):
    text = ''.join(f'{digest(p)}  {p.relative_to(base).as_posix()}\n' for p in files)
    temp = path.with_suffix('.tmp')
    temp.write_bytes(text.encode('utf-8'))  # LF on Windows too, for shasum -c
    temp.replace(path)

def collect(root, version, platform, sources):
    folder = release_dir(root, version) / platform
    sources = [Path(p) for p in sources]
    if not sources: raise ValueError('No input artifacts')
    for src in sources:
        if not src.is_file() or src.suffix.lower() not in EXTENSIONS[platform]:
            raise ValueError(f'Unsupported artifact: {src}')
        if not names_version(src.name, version):
            raise ValueError(f'Filename must identify version {version}: {src.name}')
        dst = folder / src.name
        if dst.exists() and digest(src) != digest(dst): raise ValueError(f'Refusing different existing file: {dst}')
    folder.mkdir(parents=True, exist_ok=True)
    for src in sources:
        dst = folder / src.name
        if not dst.exists():
            with dst.open('xb') as out, src.open('rb') as inp: shutil.copyfileobj(inp, out)
        if digest(src) != digest(dst): raise ValueError(f'Copy verification failed: {dst}')
        print(f'Verified: {dst}')
    manifest(folder/'SHA256SUMS.txt', artifacts(folder, platform), folder)

def finalize(root, version):
    base = release_dir(root, version)
    files = []
    for platform in EXTENSIONS:
        folder = base / platform
        found = artifacts(folder, platform) if folder.is_dir() else []
        if not found: raise ValueError(f'Missing {platform} artifacts')
        checksum = folder/'SHA256SUMS.txt'
        expected = ''.join(f'{digest(p)}  {p.name}\n' for p in found)
        if not checksum.exists() or checksum.read_text(encoding='utf-8') != expected:
            raise ValueError(f'Platform checksum missing or stale: {folder}')
        files.extend(found)
    manifest(base/'SHA256SUMS.txt', files, base)
    print(f'Verified both platforms: {base}')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True, help='Drive easyandnice/firstsynth folder')
    parser.add_argument('--version', required=True)
    parser.add_argument('--platform', choices=EXTENSIONS)
    parser.add_argument('--finalize', action='store_true', help='Verify both synced platforms and write combined manifest')
    parser.add_argument('files', nargs='*', type=Path)
    args = parser.parse_args()
    try:
        if args.finalize: finalize(args.root, args.version)
        elif args.platform: collect(args.root, args.version, args.platform, args.files)
        else: parser.error('--platform or --finalize is required')
    except (ValueError, OSError) as error: parser.exit(1, f'{error}\n')
