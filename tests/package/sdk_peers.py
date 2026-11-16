# SPDX-License-Identifier: Apache-2.0
"""Generate test-only servers with independent C symbols and identical wire IDs."""
from pathlib import Path
import re
import subprocess
import tempfile


def generate_peer(compiler: Path, schema: Path, name: str, output: Path) -> None:
    source = schema.read_text()
    names = re.findall(r"\b(?:message|enum)\s+(\w+)\s+@id", source)
    mapping = {value: "Peer" + value for value in names}
    # Preserve string literals, defaults, field numbers and route IDs. Prefix
    # only type identifiers so a server and its SDK can share one executable.
    source = re.sub(
        r'"(?:\\.|[^"\\])*"|[A-Za-z_]\w*',
        lambda match: mapping.get(match[0], match[0]), source)
    rpcs = re.findall(r"\brpc\s+(\w+)\s*\(", source)
    source += f"\nnode {name}_peer {{\n  io = external;\n  handlers = dynamic;\n"
    source += "".join(f"  serve {rpc};\n" for rpc in rpcs) + "}\n"
    with tempfile.TemporaryDirectory(prefix="wirelink-sdk-peer-") as temporary:
        peer = Path(temporary) / "peer.wl"
        peer.write_text(source)
        subprocess.run([str(compiler), "compile", str(peer), "--out-dir", str(output)],
                       check=True)
