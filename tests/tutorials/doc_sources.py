# SPDX-License-Identifier: Apache-2.0
"""Keep the complete C listings in both tutorial languages build-backed."""
from pathlib import Path
import re


root = Path(__file__).resolve().parents[2]
pairs = {
    "tutorial-rpc": ["01_rpc/client.c", "01_rpc/server.c"],
    "tutorial-rpc-values": ["02_device_info/client.c", "02_device_info/server.c"],
    "tutorial-rpc-async": ["01_rpc/client_async.c"],
    "tutorial-rpc-deferred": ["01_rpc/server_deferred.c"],
}
for chapter, sources in pairs.items():
    for suffix in ("", "-cn"):
        document = root / "docs" / f"{chapter}{suffix}.md"
        blocks = re.findall(r"```c\n(.*?)\n```", document.read_text(encoding="utf-8"), re.S)
        for source in sources:
            expected = (root / "examples" / source).read_text(encoding="utf-8").strip()
            if expected not in blocks:
                raise RuntimeError(f"{document.name} is out of sync with {source}")
