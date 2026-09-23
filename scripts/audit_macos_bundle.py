#!/usr/bin/env python3
"""Fail if any Mach-O binary in the app requires a non-system external library."""
import argparse
import subprocess
from pathlib import Path

MACHO = {b'\xcf\xfa\xed\xfe', b'\xce\xfa\xed\xfe', b'\xfe\xed\xfa\xcf', b'\xfe\xed\xfa\xce', b'\xca\xfe\xba\xbe', b'\xbe\xba\xfe\xca'}
def audit(app):
    app = app.resolve()
    executable = app / 'Contents/MacOS/GeoReader'
    frameworks = app / 'Contents/Frameworks'
    failures = []
    commands = subprocess.check_output(['otool', '-l', str(executable)], text=True)
    if 'path @executable_path/../Frameworks (offset' not in commands:
        failures.append('Main executable lacks the bundled Frameworks runtime search path')
    count = 0
    for binary in app.rglob('*'):
        if not binary.is_file() or binary.is_symlink():
            continue
        with binary.open('rb') as f:
            if f.read(4) not in MACHO:
                continue
        count += 1
        # otool -L includes a dylib's own LC_ID_DYLIB; it is an identity,
        # not a runtime dependency. Only LC_LOAD_* references need resolving.
        ids = subprocess.run(['otool', '-D', str(binary)], capture_output=True, text=True).stdout.splitlines()[1:]
        linked = subprocess.check_output(['otool', '-L', str(binary)], text=True).splitlines()[1:]
        for line in linked:
            dep = line.strip().split(' (compatibility version')[0]
            if dep in ids or dep.startswith(('/System/Library/', '/usr/lib/')):
                continue
            candidates = []
            if dep.startswith('@rpath/'):
                candidates.append(frameworks / dep[len('@rpath/'):])
            elif dep.startswith('@loader_path/'):
                candidates.append(binary.parent / dep[len('@loader_path/'):])
            elif dep.startswith('@executable_path/'):
                candidates.append(executable.parent / dep[len('@executable_path/'):])
            else:
                failures.append(f'{binary.relative_to(app)} -> external {dep}')
                continue
            if not any(p.exists() and p.resolve().is_relative_to(app) for p in candidates):
                failures.append(f'{binary.relative_to(app)} -> unresolved {dep}')
    if failures:
        raise SystemExit('\n'.join(failures))
    print(f'PASS: {count} Mach-O binaries use only app-bundled or macOS system libraries')
if __name__ == '__main__':
    p = argparse.ArgumentParser(); p.add_argument('app', type=Path)
    audit(p.parse_args().app)
