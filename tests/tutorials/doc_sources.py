# SPDX-License-Identifier: Apache-2.0
"""Keep the source listings in both tutorial languages build-backed."""
from pathlib import Path
import re


root = Path(__file__).resolve().parents[2]
pairs = {
    "getting-started": ["00_telemetry/publisher.c", "00_telemetry/subscriber.c"],
    "tutorial-rpc": ["01_rpc/client.c", "01_rpc/server.c"],
    "tutorial-rpc-async": ["01_rpc/lifecycle.c"],
    "tutorial-rpc-values": ["02_device_info/client.c", "02_device_info/server.c"],
    "tutorial-jobs": ["03_device_service/self_test.c"],
    "tutorial-integration": ["05_zephyr_udp/server.c"],
}

for chapter, sources in pairs.items():
    for suffix in ("", "-cn"):
        document = root / "docs" / f"{chapter}{suffix}.md"
        blocks = re.findall(r"```c\n(.*?)\n```", document.read_text(encoding="utf-8"), re.S)
        for source in sources:
            expected = (root / "examples" / source).read_text(encoding="utf-8").strip()
            if expected not in blocks:
                raise RuntimeError(f"{document.name} is out of sync with {source}")

# These excerpts share storage/lifecycle contracts with the compiled examples.
fragments = {
    "tutorial-rpc-values": ("c", "common/loopback_owner.h", "#define EXAMPLE_COMPLETION", "\n#endif"),
    "tutorial-integration": ("cpp", "common/tutorial_host.cpp", "wl_err_t example_udp_step", None),
}
for chapter, (language, source, start, stop) in fragments.items():
    text = (root / "examples" / source).read_text(encoding="utf-8")
    begin = text.index(start)
    end = text.index(stop, begin) if stop else len(text)
    expected = text[begin:end].strip()
    for suffix in ("", "-cn"):
        document = root / "docs" / f"{chapter}{suffix}.md"
        blocks = re.findall(r"```" + language + r"\n(.*?)\n```", document.read_text(encoding="utf-8"), re.S)
        if expected not in blocks:
            raise RuntimeError(f"{document.name} is out of sync with {source} fragment")
