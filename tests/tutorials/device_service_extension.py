# SPDX-License-Identifier: Apache-2.0
"""Add a thirteenth RPC in a temporary example copy, rebuild, run all cases.

Only schema, shared service binding, business handler/registration and the
client use case change. Transport, owner loop, CMake and other services do not.
"""
import argparse
import hashlib
import pathlib
import shutil
import subprocess
import tempfile

from device_service import check_pair

REQUEST_RESPONSE = """
message GetBuildLabelRequest @id(140) {}
message GetBuildLabelResponse @id(141) { required string<31> label @id(1); }
"""
BINDING = """
rpc GetBuildLabel { request = GetBuildLabelRequest; response = GetBuildLabelResponse; }
"""
HANDLER = """static int32_t get_build_label(void *context,
                              const get_build_label_request_value_t *request,
                              get_build_label_response_value_t *response) {
  (void)context;
  (void)request;
  response->has_label = true;
  response->label.length = 11;
  memcpy(response->label.data, "development", 11);
  return 0;
}
"""
CLIENT = """  get_build_label_request_value_t label_request;
  get_build_label_response_value_t label_response;
  get_build_label_request_value_clear(&label_request);
  CHECK(device_client_endpoint_get_build_label_sync(&endpoint, &label_request,
        &label_response, 1500).status == WL_RPC_SUCCESS);
  CHECK(label_response.label.length == 11 &&
        memcmp(label_response.label.data, "development", 11) == 0);
  puts("new GetBuildLabel RPC: OK");
"""


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=pathlib.Path)
    parser.add_argument("--wlc", required=True, type=pathlib.Path)
    parser.add_argument("--asio", required=True, type=pathlib.Path)
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--generator", default="Ninja")
    parser.add_argument("--c-compiler")
    parser.add_argument("--cxx-compiler")
    args = parser.parse_args()
    root = args.source.resolve()
    original = root / "examples/03_device_service"
    with tempfile.TemporaryDirectory(prefix="wirelink-new-rpc-") as directory:
        work = pathlib.Path(directory)
        example = work / "example"
        shutil.copytree(original, example)
        before = {p.name: digest(p) for p in example.iterdir() if p.is_file()}
        for name, addition in [("device.wl", REQUEST_RESPONSE), ("services.bind.wl", BINDING)]:
            path = example / name
            path.write_text(path.read_text(encoding="utf-8") + addition, encoding="utf-8")
        path = example / "device_service.c"
        text = path.read_text(encoding="utf-8")
        assert text.count("/* extension: handlers */") == 1
        assert text.count("/* extension: registration */") == 1
        text = text.replace("/* extension: handlers */", HANDLER)
        text = text.replace("/* extension: registration */", "config->on_get_build_label = get_build_label;")
        path.write_text(text, encoding="utf-8")
        path = example / "client.c"
        text = path.read_text(encoding="utf-8")
        assert text.count("  /* extension: client */") == 1
        path.write_text(text.replace("  /* extension: client */", CLIENT), encoding="utf-8")
        changed = {name for name, old in before.items() if digest(example / name) != old}
        assert changed == {"device.wl", "services.bind.wl", "device_service.c", "client.c"}, changed
        assert before == {name: digest(original / name) for name in before}, "source tree changed"

        # Use the real installed-style helpers and real example CMake unchanged.
        # BUILD_TESTING is off here to avoid recursively registering this test.
        project = f'''cmake_minimum_required(VERSION 3.21)
project(new_rpc_acceptance LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(WIRELINK_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WIRELINK_BUILD_PLATFORM ON CACHE BOOL "" FORCE)
set(WIRELINK_BUILD_ASIO_UDP_ADAPTER ON CACHE BOOL "" FORCE)
set(WIRELINK_WLC_AUTO_DOWNLOAD OFF CACHE BOOL "" FORCE)
set(WIRELINK_WLC_EXECUTABLE "{args.wlc.resolve().as_posix()}" CACHE FILEPATH "" FORCE)
set(WIRELINK_ASIO_INCLUDE_DIR "{args.asio.resolve().as_posix()}" CACHE PATH "" FORCE)
add_subdirectory("{root.as_posix()}" wirelink)
add_library(wirelink_tutorial_host STATIC "{root.as_posix()}/examples/common/tutorial_host.cpp")
target_include_directories(wirelink_tutorial_host PUBLIC "{root.as_posix()}/examples/common")
target_link_libraries(wirelink_tutorial_host PUBLIC Wirelink::asio_udp Wirelink::platform)
add_subdirectory(example)
'''
        (work / "CMakeLists.txt").write_text(project, encoding="utf-8")
        build = work / "build"
        command = [args.cmake, "-S", str(work), "-B", str(build), "-G", args.generator,
                   "-DCMAKE_BUILD_TYPE=Release"]
        for key, value in [("C", args.c_compiler), ("CXX", args.cxx_compiler)]:
            if value:
                command.append(f"-DCMAKE_{key}_COMPILER={value}")
        subprocess.run(command, check=True, timeout=60)
        subprocess.run([args.cmake, "--build", str(build), "--config", "Release", "--parallel", "2"],
                       check=True, timeout=120)
        def binary(role):
            name = f"device_service_{role}"
            for path in (build / "example" / name, build / "example" / f"{name}.exe",
                         build / "example/Release" / name,
                         build / "example/Release" / f"{name}.exe"):
                if path.is_file():
                    return path
            raise RuntimeError(f"missing {name} executable")
        check_pair(binary("client"), binary("server"), extended=True)
        print("New RPC acceptance: exactly 4 application files changed; infrastructure unchanged")


if __name__ == "__main__":
    main()
