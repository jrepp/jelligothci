"""Container health probe: exit 0 when Jelli Art answers /healthz on its port."""
import os
import sys
import urllib.request

try:
    port = os.environ.get("JELLI_PORT", "8765")
    with urllib.request.urlopen(f"http://127.0.0.1:{port}/healthz", timeout=4) as response:
        sys.exit(0 if response.status == 200 else 1)
except OSError:
    sys.exit(1)
