#!/usr/bin/env python3
"""Build GDAL's supported GeoReader formats with built-in HDF4; no end-user SDK.
The source version must match the GDAL ABI used to compile Mapnik and GeoReader.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import urllib.request

GDAL_SHA256 = {
    '3.13.3': '5e0c388d83da2d686cc00a40272882432cdb54edff43d4af173e532844a0a0ea',
    '3.12.4': '68844ae29557b7efae4292c3b4cb3a3b8a79d14b765b89c5a7b17cbae7fa715a',
}
HDF_SHA256 = '6dc3b8af610526788bf78fb3982b25a80abfc94e37ce0c3ae2929b5e9c937093'
ROOT = Path(__file__).resolve().parent.parent


def fetch(url, destination, checksum):
    if not destination.exists():
        temporary = destination.with_suffix('.download')
        urllib.request.urlretrieve(url, temporary)
        temporary.replace(destination)
    if hashlib.sha256(destination.read_bytes()).hexdigest() != checksum:
        raise RuntimeError('Source checksum mismatch: ' + str(destination))


def extract(archive, parent):
    with tarfile.open(archive) as stream:
        for item in stream.getmembers():
            if not (parent / item.name).resolve().is_relative_to(parent.resolve()):
                raise RuntimeError('Unsafe archive path')
        stream.extractall(parent)


def build(version, prefix, jobs, deployment_target):
    if version not in GDAL_SHA256:
        raise RuntimeError('No verified source hash for GDAL ' + version)
    deps = ROOT / '.test-deps'
    deps.mkdir(exist_ok=True)
    hdf_archive = deps / 'hdf4.tar.gz'
    gdal_archive = deps / ('gdal.tar.gz' if version == '3.13.3' else f'gdal-{version}.tar.gz')
    fetch('https://github.com/HDFGroup/hdf4/archive/refs/tags/hdf4.3.1.tar.gz', hdf_archive, HDF_SHA256)
    fetch(f'https://github.com/OSGeo/gdal/releases/download/v{version}/gdal-{version}.tar.gz', gdal_archive, GDAL_SHA256[version])
    hdf_source, gdal_source = deps / 'hdf4-hdf4.3.1', deps / f'gdal-{version}'
    if not hdf_source.exists(): extract(hdf_archive, deps)
    if not gdal_source.exists(): extract(gdal_archive, deps)
    common = ['-DCMAKE_BUILD_TYPE=Release']
    if prefix: common += [f'-DCMAKE_PREFIX_PATH={prefix}']
    if deployment_target: common += [f'-DCMAKE_OSX_DEPLOYMENT_TARGET={deployment_target}']
    hdf_build, hdf_install = deps / 'hdf4-runtime-build', deps / 'hdf4'
    commands = [
        ['cmake','-S',str(hdf_source),'-B',str(hdf_build),'-G','Ninja',*common,
         f'-DCMAKE_INSTALL_PREFIX={hdf_install}','-DBUILD_SHARED_LIBS=ON','-DBUILD_TESTING=OFF',
         '-DHDF4_BUILD_TOOLS=OFF','-DHDF4_BUILD_EXAMPLES=OFF','-DHDF4_ENABLE_NETCDF=OFF',
         '-DHDF4_ENABLE_SZIP_SUPPORT=OFF','-DCMAKE_POLICY_VERSION_MINIMUM=3.5'],
        ['cmake','--build',str(hdf_build),'--parallel',str(jobs)],
        ['cmake','--install',str(hdf_build)],
    ]
    gdal_build, gdal_install = deps / 'gdal-minimal-build', deps / 'gdal-minimal'
    options = ['-DGDAL_BUILD_OPTIONAL_DRIVERS=OFF','-DOGR_BUILD_OPTIONAL_DRIVERS=OFF',
               '-DGDAL_USE_EXTERNAL_LIBS=OFF','-DGDAL_USE_INTERNAL_LIBS=WHEN_NO_EXTERNAL',
               '-DBUILD_APPS=OFF','-DBUILD_TESTING=OFF','-DBUILD_PYTHON_BINDINGS=OFF',
               '-DBUILD_JAVA_BINDINGS=OFF','-DBUILD_CSHARP_BINDINGS=OFF']
    options += ['-DGDAL_USE_'+name+'=ON' for name in ['TIFF','GEOTIFF','HDF4','HDF5','NETCDF','SQLITE3','GEOS','PNG','JPEG']]
    options += ['-DGDAL_ENABLE_DRIVER_'+name+'=ON' for name in ['GTIFF','HDF4','HDF5','NETCDF','PNG','JPEG']]
    options += ['-DOGR_ENABLE_DRIVER_'+name+'=ON' for name in ['GPKG','SHAPE','GEOJSON']]
    commands += [
        ['cmake','-S',str(gdal_source),'-B',str(gdal_build),'-G','Ninja',*common,*options,
         f'-DCMAKE_INSTALL_PREFIX={gdal_install}',f'-DHDF4_ROOT={hdf_install}'],
        ['cmake','--build',str(gdal_build),'--parallel',str(jobs)],
        ['cmake','--install',str(gdal_build)],
    ]
    for command in commands: subprocess.run(command, check=True)
    profile = {'gdal':version,'hdf4':'builtin','raster':['GTiff','HDF4','HDF5','netCDF','PNG','JPEG','VRT','MEM'],
               'vector':['ESRI Shapefile','GeoJSON','GPKG']}
    (gdal_install/'share/georeader-driver-profile.json').write_text(json.dumps(profile,indent=2)+'\n')
    print('Runtime ready at '+str(gdal_install))


def install_runtime(prefix):
    """Replace the private vcpkg GDAL runtime with the matching reduced build.

    Import libraries/headers retain their ABI. Never mutate a system prefix.
    Run before the platform's relocation/dependency scanning step.
    """
    target = Path(prefix).resolve()
    if not target.is_relative_to(ROOT / 'vcpkg_installed'):
        raise RuntimeError('--install-runtime is restricted to this checkout/vcpkg_installed')
    gd = ROOT / '.test-deps/gdal-minimal'
    def libraries(base):
        return [p for folder in ('bin', 'lib') for p in (base/folder).glob('*')
                if p.is_file() and (p.suffix in ('.dll', '.dylib') or '.so' in p.name)]
    gdal_libraries = [p for p in libraries(gd) if 'gdal' in p.name.lower()]
    if not gdal_libraries or not any((target/p.parent.name/p.name).exists() for p in gdal_libraries):
        raise RuntimeError('No matching vcpkg GDAL runtime filename; refusing ABI replacement')
    for item in gdal_libraries + libraries(ROOT / '.test-deps/hdf4'):
        destination = target / item.parent.name / item.name
        destination.parent.mkdir(parents=True, exist_ok=True)
        # Keep aliases as separate copies here; packaging deduplicates/relocates.
        if destination.is_symlink(): destination.unlink()
        shutil.copy2(item.resolve(), destination)
    shutil.copy2(gd/'share/georeader-driver-profile.json',target/'share/georeader-driver-profile.json')


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--version', choices=GDAL_SHA256)
    parser.add_argument('--dependency-prefix', default=os.environ.get('GEOREADER_VCPKG_RUNTIME_ROOT'))
    parser.add_argument('--jobs',type=int,default=6)
    parser.add_argument('--deployment-target')
    parser.add_argument('--install-runtime', action='store_true')
    args=parser.parse_args()
    version=args.version
    if not version and args.dependency_prefix:
        prefix=Path(args.dependency_prefix)
        for header in (prefix/'include/gdal_version.h',prefix/'include/gdal/gdal_version.h'):
            if header.exists():
                match=re.search(r'#define\s+GDAL_RELEASE_NAME\s+"([^"]+)"',header.read_text())
                if match:version=match[1];break
    if not version:version=subprocess.check_output(['gdal-config','--version'],text=True).strip()
    build(version,args.dependency_prefix,args.jobs,args.deployment_target)
    if args.install_runtime:
        if not args.dependency_prefix: raise RuntimeError('--install-runtime requires --dependency-prefix')
        install_runtime(args.dependency_prefix)
