# SPDX-License-Identifier: Apache-2.0
"""Validate the frozen pair, inventory tested SDKs, and promote their exact bundle."""
import argparse
import importlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tomllib

ROOT = Path(__file__).resolve().parents[2]
PACKAGES = ('wirelink_calculator_sdk', 'wirelink_device_sdk')
ABIS = tuple(f'cp31{i}' for i in range(5))
FAMILIES = ('manylinux', 'macos-x86_64', 'macos-arm64', 'windows')


def pair(root):
    value = json.loads((root / '.github/release-pair.json').read_text())
    assert value['format'] == 'wirelink-release-pair-v1'
    assert re.fullmatch('[0-9a-f]{40}', value['wlc']['source_sha']), 'WLC must be pinned to a complete commit SHA'
    assert re.fullmatch(r'\d+\.\d+\.\d+', value['wirelink_version'])
    assert re.fullmatch(r'\d+\.\d+\.\d+', value['wlc']['rust_toolchain'])
    assert value['wlc']['version'] == value['wirelink_version']
    assert value['wlc']['codegen_contract'] == '0.13' and value['wlc']['binding_api'] == 3
    return value


def validate_source(root, tag=''):
    value = pair(root)
    version = value['wirelink_version']
    assert not tag or tag == f'v{version}', 'release tag differs from frozen version'
    header = (root / 'include/wirelink/version.h').read_text()
    assert f'#define WIRELINK_VERSION_STRING "{version}"' in header
    assert '#define WIRELINK_CODEGEN_CONTRACT_MINOR 13' in header
    assert f'project(wirelink VERSION {version} LANGUAGES C)' in (root / 'CMakeLists.txt').read_text()
    for example in ('06_bindings', '07_device_sdk'):
        project = tomllib.loads((root / 'examples' / example / 'pyproject.toml').read_text())
        assert project['project']['version'] == version
    assert (root / 'docs' / f'release-v{version}-cn.md').is_file(), 'release notes are missing'
    return value


def wheel_key(name, version):
    match = re.fullmatch(r'(wirelink_(?:calculator|device)_sdk)-' + re.escape(version)
                         + r'-(cp31[0-4])-\2-(.+)\.whl', name)
    if not match:
        raise ValueError(f'unexpected package, version or Python ABI: {name}')
    platform = match[3]
    if platform == 'win_amd64':
        family = 'windows'
    elif platform == 'macosx_13_0_x86_64':
        family = 'macos-x86_64'
    elif platform == 'macosx_13_0_arm64':
        family = 'macos-arm64'
    elif ('manylinux_2_28_x86_64' in platform.split('.')
          and all(re.fullmatch(r'manylinux_\d+_\d+_x86_64', p) for p in platform.split('.'))):
        family = 'manylinux'
    else:
        raise ValueError(f'unsupported or unqualified wheel platform: {name}')
    return match[1], match[2], family


def inventory(artifacts, version):
    wheels = list(artifacts.rglob('*.whl'))
    keys = [wheel_key(p.name, version) for p in wheels]
    expected = {(package, abi, family) for package in PACKAGES for abi in ABIS for family in FAMILIES}
    if len(keys) != len(expected) or set(keys) != expected:
        raise ValueError(f'expected exactly 40 distinct tested wheel combinations; missing={expected - set(keys)}')
    sources = list((artifacts / 'generated-sdks-manylinux_2_28-x86_64').glob('*.tar.gz'))
    if {p.name for p in sources} != {f'{package}-{version}.tar.gz' for package in PACKAGES}:
        raise ValueError('expected exactly the two manylinux-tested source distributions')
    return sorted(wheels + sources)


def metadata(root, run_id):
    value = validate_source(root)
    return dict(format='wirelink-release-v1', version=value['wirelink_version'],
                wirelink_sha=subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip(),
                wlc=value['wlc'], source_run_id=str(run_id))


def verify_bundle(directory, expected, common):
    common.verify_checksums(directory)
    actual = json.loads((directory / 'wirelink-release.json').read_text())
    if actual != expected:
        raise ValueError('bundle source, version or compiler pair differs from this candidate')
    wheels = list(directory.glob('*.whl'))
    keys = [wheel_key(p.name, expected['version']) for p in wheels]
    combinations = {(p, a, f) for p in PACKAGES for a in ABIS for f in FAMILIES}
    assert len(keys) == 40 and set(keys) == combinations
    expected_names = {p.name for p in wheels} | {f'{p}-{expected["version"]}.tar.gz' for p in PACKAGES}
    assert {p.name for p in directory.iterdir()} == expected_names | {'wirelink-release.json', 'SHA256SUMS'}
    return actual


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('prepare', 'collect', 'verify'))
    parser.add_argument('--root', type=Path, default=ROOT)
    parser.add_argument('--tag', default='')
    parser.add_argument('--run-id', default='')
    parser.add_argument('--promote-run', default='')
    parser.add_argument('--artifacts', type=Path, default=Path('artifacts'))
    parser.add_argument('--output', type=Path, default=Path('dist'))
    parser.add_argument('--wlc-source', type=Path, default=Path('.tools/wlc'))
    parser.add_argument('--publish', action='store_true')
    args = parser.parse_args()
    value = validate_source(args.root, args.tag)
    if args.command == 'prepare':
        if os.environ.get('GITHUB_OUTPUT'):
            with open(os.environ['GITHUB_OUTPUT'], 'a') as output:
                output.write(f'version={value["wirelink_version"]}\nwlc_sha={value["wlc"]["source_sha"]}\n')
        print(json.dumps(value, indent=2))
        return
    sys.path.insert(0, str(args.wlc_source.resolve() / '.github/scripts'))
    common = importlib.import_module('release_common')
    assert common.git_sha(args.wlc_source) == value['wlc']['source_sha'], 'release tooling must use the frozen WLC source'
    expected = metadata(args.root, args.promote_run or args.run_id)
    if args.command == 'collect':
        assets = inventory(args.artifacts, expected['version'])
        args.output.mkdir()
        for asset in assets:
            shutil.copy2(asset, args.output / asset.name)
        common.write_json(args.output / 'wirelink-release.json', expected)
        common.write_checksums(args.output)
    actual = verify_bundle(args.output, expected, common)
    if args.promote_run:
        common.verify_origin('starwey604/wirelink', args.promote_run, actual, actual['wirelink_sha'])
    if args.publish:
        assert args.tag == f'v{actual["version"]}'
        # Publication order is checked against the public WLC release's actual
        # provenance, not just the presence of a version-matching executable.
        import tempfile
        with tempfile.TemporaryDirectory() as temporary:
            common.run('gh', 'release', 'download', args.tag, '--repo', 'starwey604/wlc',
                       '--pattern', 'wlc-release.json', '--dir', temporary)
            compiler = json.loads((Path(temporary) / 'wlc-release.json').read_text())
            assert compiler['wlc_sha'] == actual['wlc']['source_sha']
            assert compiler['wirelink_sha'] == actual['wirelink_sha']
            for key in ('version', 'codegen_contract', 'binding_api', 'rust_toolchain'):
                assert compiler[key] == actual['wlc'][key], key
        common.publish('starwey604/wirelink', args.tag, args.output, actual['wirelink_sha'],
                       promotion=bool(args.promote_run), notes=args.root / 'docs' / f'release-{args.tag}-cn.md')


if __name__ == '__main__':
    main()
