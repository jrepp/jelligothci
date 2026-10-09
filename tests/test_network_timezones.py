"""Check firmware POSIX rules against the host tzdb at fixed DST/year boundaries."""
from datetime import datetime, timezone
import os
import subprocess
import sys
import time
from zoneinfo import ZoneInfo

regions = ("UTC", "America/New_York", "America/Chicago", "America/Denver",
           "America/Phoenix", "America/Los_Angeles", "Europe/London", "Europe/Berlin",
           "Asia/Tokyo", "Asia/Kolkata", "Australia/Sydney")
start = int(datetime(2026, 1, 1, tzinfo=timezone.utc).timestamp())
for region in regions:
    rule = subprocess.check_output([sys.argv[1], region], text=True).strip()
    os.environ["TZ"] = rule
    time.tzset()
    zone = ZoneInfo(region)
    # Every hour of 2026–27 includes both sides of every supported DST transition.
    for seconds in range(start, start + 730 * 86400, 3600):
        expected = datetime.fromtimestamp(seconds, zone)
        actual = time.localtime(seconds)
        assert (actual.tm_year, actual.tm_yday, actual.tm_hour, actual.tm_min) == (
            expected.year, expected.timetuple().tm_yday, expected.hour, expected.minute), region
print("Timezone presets match host tzdb across 2026–27 DST and year boundaries.")
