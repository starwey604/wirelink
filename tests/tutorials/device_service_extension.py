# SPDX-License-Identifier: Apache-2.0
"""Extend only three application files; unchanged generation/owner infrastructure."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile


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
        for name in ('loopback_owner.h', 'example_check.h'):
            shutil.copyfile(root / 'examples/common' / name, work / 'common' / name)
        before = {p.name: p.read_bytes() for p in example.iterdir() if p.is_file()}
        path = example / 'device.wl'
        text = path.read_text() + '\nmessage GetBuildLabelRequest @id(140) {}\nmessage GetBuildLabelResponse @id(141) { string<31> label @id(1); }\nrpc GetBuildLabel(GetBuildLabelRequest) -> GetBuildLabelResponse;\n'
        text = text.replace('node device_server {', 'node device_server {\n  serve GetBuildLabel;')
        text = text.replace('node device_client {', 'node device_client {\n  call GetBuildLabel;')
        path.write_text(text)
        path = example / 'device_service.c'
        text = path.read_text()
        text = text.replace('/* extension: handlers */', '''int32_t device_server_on_get_build_label(device_server_node_t *self, const get_build_label_request_t *request,
    get_build_label_response_t *response) {
  (void)self; (void)request;
  response->label.length = 11;
  memcpy(response->label.data, "development", 11); return 0;
}
''')
        path.write_text(text)
        path = example / 'client.c'
        text = path.read_text().replace('/* extension: completions */', 'EXAMPLE_COMPLETION(get_build_label, get_build_label_response)')
        text = text.replace('  /* extension: client */', '''  get_build_label_request_t label = {0};
  CALL(get_build_label, label); SUCCESS(get_build_label);
  EXAMPLE_REQUIRE(get_build_label_value.label.length == 11 && memcmp(get_build_label_value.label.data, "development", 11) == 0);
  puts("new GetBuildLabel RPC: OK");''')
        path.write_text(text)
        changed = {name for name, old in before.items() if (example / name).read_bytes() != old}
        assert changed == {'device.wl', 'device_service.c', 'client.c'}, changed
        assert before == {name: (original / name).read_bytes() for name in before}
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
        executable = next(path for path in candidates if path.is_file())
        result = subprocess.run([executable], capture_output=True, text=True, check=True, timeout=15)
        assert 'new GetBuildLabel RPC: OK' in result.stdout, result.stdout
        assert 'ordinary completion and retained results passed' in result.stdout, result.stdout
        print('New RPC acceptance: exactly three application files changed; infrastructure unchanged')


if __name__ == '__main__':
    main()
