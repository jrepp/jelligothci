# /// script
# requires-python = ">=3.15,<3.16"
# dependencies = ["lz4==4.4.5"]
# ///
"""Optional pinned reference-encoder interoperability corpus, outside offline tests.

Run with scripts/uv run --script tests/test_jellipak_lz4.py VERIFIER.
This validates the C decoder against Python LZ4's independent encoder and CRCs;
it does not establish art fidelity, final pack savings, or device performance.
"""
from pathlib import Path
import random
import subprocess
import sys
import tempfile

import lz4
import lz4.block
from test_jellipak_cli import pack


def main():
    verifier = Path(sys.argv[1]).resolve()
    rng = random.Random(0x4A50414B)
    blocks = []
    for size in (1, 4, 5, 12, 13, 15, 16, 255, 256, 270, 4096, 16384, 65536):
        samples = (
            b"A" * size,
            (bytes(range(256)) * (size // 256 + 1))[:size],
            rng.randbytes(size),
            bytes(rng.randrange(4) for _ in range(size)),
        )
        for decoded in samples:
            for mode in ("default", "high_compression"):
                stored = lz4.block.compress(decoded, mode=mode, store_size=False)
                blocks.append((1, stored, decoded))
    with tempfile.TemporaryDirectory(prefix="jellipak-lz4-") as directory:
        path = Path(directory) / "corpus.jpak"
        path.write_bytes(pack(blocks))
        subprocess.run([str(verifier), str(path)], check=True, timeout=30)
    print(f"reference_lz4={lz4.__version__} library={lz4.library_version_string()} "
          f"blocks={len(blocks)} seed=0x4a50414b")


if __name__ == "__main__":
    main()
