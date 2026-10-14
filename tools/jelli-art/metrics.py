"""Prometheus text-format metrics for /metrics, scraped by prom-nuc.

Deliberately dependency-free: the exposition format is simple and the studio
keeps its runtime deps at Pillow only. Route labels stay bounded because every
served path is a fixed name (no ids reach the labels).
"""
import threading
import time

BUCKETS = (0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1, 2.5, 5, 10)


def _escape(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")


class Metrics:
    def __init__(self):
        self._lock = threading.Lock()
        self._started = time.monotonic()
        self._requests: dict[tuple[str, str, int], int] = {}
        self._duration = {"sum": 0.0, "count": 0, "buckets": [0] * len(BUCKETS)}
        self._active = 0

    def begin(self):
        with self._lock:
            self._active += 1

    def end(self, method: str, route: str, status: int | None, seconds: float):
        with self._lock:
            self._active -= 1
            key = (method, route, int(status or 0))
            self._requests[key] = self._requests.get(key, 0) + 1
            self._duration["sum"] += seconds
            self._duration["count"] += 1
            buckets = self._duration["buckets"]
            for i, bound in enumerate(BUCKETS):
                if seconds <= bound:
                    buckets[i] += 1
                    break

    def render(self) -> str:
        with self._lock:
            requests = sorted(self._requests.items())
            duration = dict(self._duration)
            active = max(self._active, 0)
            uptime = round(time.monotonic() - self._started)
        out = []
        name = "jelli_art_http_requests_total"
        out.append(f"# HELP {name} HTTP requests handled.")
        out.append(f"# TYPE {name} counter")
        for (method, route, status), count in requests:
            out.append(f'{name}{{method="{_escape(method)}",route="{_escape(route)}",status="{status}"}} {count}')
        dur = "jelli_art_http_request_duration_seconds"
        out.append(f"# HELP {dur} Request duration in seconds.")
        out.append(f"# TYPE {dur} histogram")
        cumulative = 0
        for bound, count in zip(BUCKETS, duration["buckets"]):
            cumulative += count
            out.append(f'{dur}_bucket{{le="{bound}"}} {cumulative}')
        out.append(f'{dur}_bucket{{le="+Inf"}} {duration["count"]}')
        out.append(f'{dur}_sum {duration["sum"]}')
        out.append(f'{dur}_count {duration["count"]}')
        active_name = "jelli_art_http_active_requests"
        out.append(f"# HELP {active_name} Requests currently in flight.")
        out.append(f"# TYPE {active_name} gauge")
        out.append(f"{active_name} {active}")
        up = "jelli_art_process_uptime_seconds"
        out.append(f"# HELP {up} Seconds since the process started.")
        out.append(f"# TYPE {up} gauge")
        out.append(f"{up} {uptime}")
        return "\n".join(out) + "\n"
