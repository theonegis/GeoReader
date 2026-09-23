#!/usr/bin/env python3
"""Normalize copied Mach-O references to libraries already inside the bundle.

macdeployqt can leave Homebrew install IDs/references in less common modules.
Never resolve a missing file from the developer machine here: the subsequent
bundle audit must reject incomplete dependency collection.
"""
import argparse
import re
import subprocess
from pathlib import Path
from audit_macos_bundle import MACHO


def relocate(app):
    app = app.resolve()
    frameworks = app / 'Contents/Frameworks'
    # Qt's deployer may copy a stripped compatibility-name library while
    # dylibbundler copies its full-version counterpart. Use a single inode for
    # each ABI so GDAL/Mapnik do not acquire duplicate process-global registries.
    libraries = list(frameworks.glob('*.dylib'))
    for library in libraries:
        if library.is_symlink():
            continue
        stem = library.name[:-6]
        candidates = [p for p in libraries if not p.is_symlink()
                      and re.fullmatch(re.escape(stem) + r'\.\d+(?:\.\d+)*\.dylib', p.name)]
        if candidates:
            canonical = max(candidates, key=lambda p: (len(p.name), p.name))
            library.unlink()
            library.symlink_to(canonical.name)
    for binary in app.rglob('*'):
        if not binary.is_file() or binary.is_symlink():
            continue
        with binary.open('rb') as f:
            if f.read(4) not in MACHO:
                continue
        ids = subprocess.run(['otool', '-D', str(binary)], capture_output=True, text=True).stdout.splitlines()[1:]
        if ids and binary.is_relative_to(frameworks):
            subprocess.run(['install_name_tool', '-id', '@rpath/' + str(binary.relative_to(frameworks)), str(binary)], check=True, capture_output=True)
        linked = subprocess.check_output(['otool', '-L', str(binary)], text=True).splitlines()[1:]
        for line in linked:
            dep = line.strip().split(' (compatibility version')[0]
            if dep in ids or dep.startswith(('/System/Library/', '/usr/lib/')):
                continue
            match = re.search(r'([^/]+\.framework/Versions/[^/]+/[^/]+)$', dep)
            relative = match.group(1) if match else Path(dep).name
            target = frameworks / relative
            if target.exists() and target.resolve().is_relative_to(frameworks):
                relative = str(target.resolve().relative_to(frameworks))
                subprocess.run(['install_name_tool', '-change', dep, '@rpath/' + relative, str(binary)], check=True, capture_output=True)
        commands = subprocess.check_output(['otool', '-l', str(binary)], text=True)
        rpaths = re.findall(r'cmd LC_RPATH\s+cmdsize \d+\s+path (.+?) \(offset', commands)
        # Never search developer-machine directories at runtime.
        for path in set(rpaths):
            if path.startswith(('/opt/', '/usr/local/', '/Users/')):
                subprocess.run(['install_name_tool', '-delete_rpath', path, str(binary)], check=True, capture_output=True)
    executable = app / 'Contents/MacOS/GeoReader'
    commands = subprocess.check_output(['otool', '-l', str(executable)], text=True)
    if 'path @executable_path/../Frameworks (offset' not in commands:
        subprocess.run(['install_name_tool', '-add_rpath', '@executable_path/../Frameworks', str(executable)], check=True, capture_output=True)
    print('Normalized bundle library references; signatures must be refreshed afterward')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('app', type=Path)
    relocate(parser.parse_args().app)
