# SPDX-License-Identifier: Apache-2.0
"""Extend only four application files; unchanged generation/owner infrastructure."""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile
from device_service import check_demo


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--wlc', required=True, type=Path)
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--generator', default='Ninja')
    parser.add_argument('--c-compiler')
    args = parser.parse_args()
    root = args.source.resolve()
    original = root / 'examples/03_device_service'
    with tempfile.TemporaryDirectory(prefix='wirelink-new-rpc-') as temporary:
        work = Path(temporary)
        example = work / 'example'
        shutil.copytree(original, example)
        (work / 'common').mkdir()
        shutil.copyfile(root / 'examples/common/loopback_owner.h', work / 'common/loopback_owner.h')
        before = {p.name: digest(p) for p in example.iterdir() if p.is_file()}
        for name, addition in [
            ('device.wl', '\nmessage GetBuildLabelRequest @id(140) {}\nmessage GetBuildLabelResponse @id(141) { required string<31> label @id(1); }\n'),
            ('services.bind.wl', '\nrpc GetBuildLabel { request=GetBuildLabelRequest; response=GetBuildLabelResponse; }\n'),
        ]:
            path = example / name
            path.write_text(path.read_text() + addition)
        path = example / 'device_service.c'
        text = path.read_text()
        text = text.replace('/* extension: handlers */', '''static int32_t get_build_label(void *user, const get_build_label_request_t *request,
    get_build_label_response_value_t *response) {
  (void)user; (void)request;
  response->has_label = true; response->label.length = 11;
  memcpy(response->label.data, "development", 11); return 0;
}
''')
        text = text.replace('/* extension: registration */', 'config->on_rpc_get_build_label = get_build_label;')
        path.write_text(text)
        path = example / 'client.c'
        text = path.read_text().replace('/* extension: completions */', 'EXAMPLE_COMPLETION(get_build_label, get_build_label_response)')
        text = text.replace('  /* extension: client */', '''  get_build_label_request_value_t label = {0};
  CALL(get_build_label, label); SUCCESS(get_build_label);
  EXAMPLE_REQUIRE(get_build_label_value.label.length == 11 && memcmp(get_build_label_value.label.data, "development", 11) == 0);
  puts("new GetBuildLabel RPC: OK");''')
        path.write_text(text)
        changed = {name for name, old in before.items() if digest(example / name) != old}
        assert changed == {'device.wl', 'services.bind.wl', 'device_service.c', 'client.c'}, changed
        assert before == {name: digest(original / name) for name in before}
        (work / 'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.21)
project(current_rpc_extension LANGUAGES C)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(WIRELINK_INSTALL OFF CACHE BOOL "" FORCE)
set(WIRELINK_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WIRELINK_WLC_EXECUTABLE "{args.wlc.resolve().as_posix()}" CACHE FILEPATH "" FORCE)
add_subdirectory("{root.as_posix()}" wirelink)
add_subdirectory(example)
''')
        build = work / 'build'
        command = [args.cmake, '-S', str(work), '-B', str(build), '-G', args.generator, '-DCMAKE_BUILD_TYPE=Release']
        if args.c_compiler:
            command.append('-DCMAKE_C_COMPILER=' + args.c_compiler)
        subprocess.run(command, check=True, timeout=30)
        subprocess.run([args.cmake, '--build', str(build), '--target', 'wirelink_device_service_demo', '--config', 'Release', '--parallel', '2'], check=True, timeout=90)
        candidates = [build / 'example' / suffix for suffix in ['wirelink_device_service_demo', 'wirelink_device_service_demo.exe', 'Release/wirelink_device_service_demo.exe']]
        check_demo(next(path for path in candidates if path.is_file()), extended=True)
        print('New RPC acceptance: exactly four application files changed; infrastructure unchanged')


if __name__ == '__main__':
    main()
