#!/usr/bin/env python3
"""Create a minimal Widgets bundle from a native executable's dependency closure.

Only explicit runtime plugins are roots. Qt modules, GDAL, Mapnik and their
transitive libraries are copied once, with no developer headers, QML, database
plugins or unrelated frameworks. Missing dependencies fail packaging.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess


def run(*args):
    return subprocess.check_output(args, text=True).strip()


def bundle(source_app, destination, test_runtime=False, search_roots=()):
    source_app, destination = source_app.resolve(), destination.resolve()
    if destination.exists() or destination == source_app:
        raise SystemExit('Destination must be a new app path')
    contents = destination / 'Contents'
    frameworks = contents / 'Frameworks'
    plugins = contents / 'PlugIns'
    resources = contents / 'Resources'
    for directory in (frameworks, plugins, resources, contents / 'MacOS'):
        directory.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source_app / 'Contents/Info.plist', contents / 'Info.plist')
    for name in ('georeader.icns', 'LICENSE', 'THIRD_PARTY_NOTICES.md'):
        original = source_app / 'Contents/Resources' / name
        if not original.exists() and name != 'georeader.icns':
            original = Path(__file__).resolve().parent.parent / name
        if original.exists():
            shutil.copy2(original, resources / name)
    qt_plugins = Path(run('qtpaths', '--plugin-dir'))
    runtime_root = os.environ.get('GEOREADER_VCPKG_RUNTIME_ROOT')
    gdal_prefix = Path(os.environ['GEOREADER_GDAL_PREFIX']) if os.environ.get('GEOREADER_GDAL_PREFIX') else (Path(runtime_root) if runtime_root else Path(run('brew', '--prefix', 'gdal')))
    proj_prefix = Path(runtime_root) if runtime_root else Path(run('brew', '--prefix', 'proj'))
    mapnik_prefix = Path(runtime_root) if runtime_root else Path(run('brew', '--prefix', 'mapnik'))
    search = [Path(p).resolve() for p in search_roots]
    search += [source_app / 'Contents/Frameworks', gdal_prefix / 'lib', mapnik_prefix / 'lib', Path('/opt/homebrew/lib')]
    selected, queued, origins, edges = {}, [], {}, []
    qt_frameworks = set()

    def add(source, target=None):
        source = source.resolve(strict=True)
        if source in selected:
            return selected[source]
        if target is None:
            match = re.search(r'(.+?/([^/]+\.framework))/(.+)$', str(source))
            if match:
                framework_source, name, relative = Path(match[1]), match[2], match[3]
                target = frameworks / name / relative
                qt_frameworks.add(frameworks / name)
                version = relative.split('/')[1]
                for resource_name in ('Resources',):
                    original = framework_source / 'Versions' / version / resource_name
                    copy = frameworks / name / 'Versions' / version / resource_name
                    if original.exists() and not copy.exists():
                        shutil.copytree(original, copy, symlinks=True)
                for link, value in ((frameworks/name/'Versions/Current', version),
                                    (frameworks/name/name.removesuffix('.framework'), 'Versions/Current/'+name.removesuffix('.framework')),
                                    (frameworks/name/'Resources', 'Versions/Current/Resources')):
                    link.parent.mkdir(parents=True, exist_ok=True)
                    if not link.is_symlink() and not link.exists():
                        link.symlink_to(value)
            else:
                target = frameworks / source.name
        if target in origins and origins[target] != source:
            raise RuntimeError(f'Conflicting library destinations: {source}, {origins[target]}')
        origins[target] = source
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        target.chmod(target.stat().st_mode | 0o200)
        selected[source] = target
        queued.append(source)
        return target

    main_source = source_app / 'Contents/MacOS/GeoReader'
    executable = add(main_source, contents / 'MacOS/GeoReader')
    roots = ['platforms/libqcocoa.dylib', 'tls/libqsecuretransportbackend.dylib', 'iconengines/libqsvgicon.dylib']
    if test_runtime:
        roots += ['platforms/libqoffscreen.dylib']
    for relative in roots:
        add(qt_plugins / relative, plugins / relative)
    mapnik_input = source_app / 'Contents/PlugIns/mapnik/input'
    if not mapnik_input.is_dir():
        mapnik_input = mapnik_prefix / 'lib/mapnik/input'
    required_mapnik = ['gdal+ogr.input', 'geojson.input', 'shape.input']
    if not (mapnik_input / 'gdal+ogr.input').exists():
        required_mapnik = ['ogr.input', 'geojson.input', 'shape.input']
    for name in required_mapnik:
        add(mapnik_input / name, plugins / 'mapnik/input' / name)
    scientific = source_app / 'Contents/PlugIns/gdal/gdal_HDF4.dylib'
    builtin_hdf4 = (gdal_prefix / 'share/georeader-driver-profile.json').exists()
    if not builtin_hdf4:
        if not scientific.exists():
            raise RuntimeError('HDF4 plugin missing: build with GEOREADER_GDAL_PLUGIN_DIR')
        add(scientific, plugins / 'gdal/gdal_HDF4.dylib')

    def expand(value, owner):
        return Path(value.replace('@loader_path', str(owner.parent)).replace('@executable_path', str(main_source.parent)))

    for source in queued:
        target = selected[source]
        load_commands = run('otool', '-l', str(source))
        rpaths = re.findall(r'cmd LC_RPATH\s+cmdsize \d+\s+path (.+?) \(offset', load_commands)
        ids = run('otool', '-D', str(source)).splitlines()[1:]
        links = run('otool', '-L', str(source)).splitlines()[1:]
        for line in links:
            dep = line.strip().split(' (compatibility version')[0]
            if dep in ids or dep.startswith(('/System/Library/', '/usr/lib/')):
                continue
            candidates = []
            if dep.startswith('@rpath/'):
                suffix = dep[len('@rpath/'):]
                candidates = [expand(p, source) / suffix for p in rpaths]
                candidates += [p / suffix for p in search]
            elif dep.startswith('@'):
                candidates = [expand(dep, source)]
            else:
                candidates = [Path(dep)]
            candidates += [p / Path(dep).name for p in search]
            if Path(dep).name.startswith('libgdal.') and os.environ.get('GEOREADER_GDAL_PREFIX'):
                # The replacement must retain the exact GDAL ABI/soname requested.
                candidates = [gdal_prefix / 'lib' / Path(dep).name]
            found = next((p for p in candidates if p.is_file()), None)
            if found is None:
                raise RuntimeError(f'Cannot resolve {source.name} -> {dep}')
            dependency = add(found)
            reference = '@rpath/' + str(dependency.relative_to(frameworks))
            subprocess.run(['install_name_tool', '-change', dep, reference, str(target)], check=True, capture_output=True)
            edges.append([str(target.relative_to(contents)), str(dependency.relative_to(contents))])
        if ids:
            identity = '@rpath/' + str(target.relative_to(frameworks)) if target.is_relative_to(frameworks) else '@rpath/' + target.name
            subprocess.run(['install_name_tool', '-id', identity, str(target)], check=True, capture_output=True)
        for path in set(rpaths):
            subprocess.run(['install_name_tool', '-delete_rpath', path, str(target)], check=True, capture_output=True)
        # Symbols used by dynamic linking remain; only local/debug symbols go.
        subprocess.run(['strip', '-S', '-x', str(target)], check=True, capture_output=True)
    subprocess.run(['install_name_tool', '-add_rpath', '@executable_path/../Frameworks', str(executable)], check=True, capture_output=True)
    shutil.copytree(gdal_prefix / 'share/gdal', resources / 'gdal', symlinks=False)
    (resources / 'proj').mkdir()
    for name in ('proj.db', 'proj.ini'):
        shutil.copy2(proj_prefix / 'share/proj' / name, resources / 'proj' / name)
    if not test_runtime and any('QtTest.framework' in str(p) for p in selected.values()):
        raise RuntimeError('Release packaging requires GEOREADER_BUILD_TESTS=OFF')
    # Do not advertise macOS 15 if a local developer library was built for a
    # newer OS. The release triplets compile all dependencies for macOS 15.
    import plistlib
    plist_path = contents / 'Info.plist'
    plist = plistlib.loads(plist_path.read_bytes())
    minimum = tuple(map(int, plist.get('LSMinimumSystemVersion', '15.0').split('.')))
    for target in selected.values():
        commands = run('otool', '-l', str(target))
        for block in commands.split('Load command'):
            if 'cmd LC_BUILD_VERSION\n' not in block and 'cmd LC_VERSION_MIN_MACOSX\n' not in block:
                continue
            match = re.search(r'\b(?:minos|version) (\d+\.\d+(?:\.\d+)?)', block)
            if match:
                minimum = max(minimum, tuple(map(int, match[1].split('.'))))
    if minimum > (15, 0, 0):
        raise RuntimeError('A runtime library requires newer than macOS 15: '+str(minimum))
    plist['LSMinimumSystemVersion'] = '.'.join(map(str, minimum))
    plist_path.write_bytes(plistlib.dumps(plist))
    (resources / 'qt.conf').write_text('[Paths]\nPlugins = PlugIns\n')
    # Qt resolves plugin paths relative to Contents by default for app bundles.
    for target in selected.values():
        subprocess.run(['codesign', '--force', '--sign', '-', str(target)], check=True, capture_output=True)
    for framework in qt_frameworks:
        subprocess.run(['codesign', '--force', '--sign', '-', str(framework)], check=True, capture_output=True)
    subprocess.run(['codesign', '--force', '--deep', '--sign', '-', str(destination)], check=True, capture_output=True)
    subprocess.run(['codesign', '--verify', '--deep', '--strict', str(destination)], check=True, capture_output=True)
    manifest = {'binaryCount': len(selected), 'testRuntime': test_runtime, 'minimumMacOS': plist['LSMinimumSystemVersion'], 'qtPlugins': roots,
                'mapnikPlugins': required_mapnik, 'dependencies': edges,
                'bytes': sum(p.stat().st_size for p in destination.rglob('*') if p.is_file() and not p.is_symlink()),
                'libraries': sorted(str(p.relative_to(contents)) for p in selected.values())}
    destination.with_suffix('.dependencies.json').write_text(json.dumps(manifest, indent=2)+'\n')
    print(json.dumps({k: v for k, v in manifest.items() if k not in ('dependencies', 'libraries')}, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path)
    parser.add_argument('--test-runtime', action='store_true')
    parser.add_argument('--search', action='append', default=[])
    args = parser.parse_args()
    bundle(args.source, args.destination, args.test_runtime, args.search)
