"""Flash layout A (ADR-013) invariants that need no SDK or board.

The pet's save survives only while nvs keeps ESP-IDF's default offset and size, and
the two apps can only find each other while both projects agree on the table and
on each other's project names.
"""
from pathlib import Path
import csv
import re
import unittest

REPOSITORY = Path(__file__).resolve().parents[1]
TABLE = REPOSITORY / "ports/esp32/partitions.csv"
FLASH_BYTES = 16 * 1024 * 1024
TABLE_OFFSET, TABLE_BYTES = 0x8000, 0x1000


def partitions():
    rows = [row for row in csv.reader(TABLE.read_text().splitlines())
            if row and not row[0].lstrip().startswith("#")]
    return [{"name": row[0].strip(), "type": row[1].strip(), "subtype": row[2].strip(),
             "offset": int(row[3], 16), "size": int(row[4], 16)} for row in rows]


class LayoutTests(unittest.TestCase):
    def setUp(self):
        self.table = partitions()
        self.by_name = {entry["name"]: entry for entry in self.table}

    def test_names_are_unique_and_complete(self):
        self.assertEqual(len(self.by_name), len(self.table))
        self.assertEqual(set(self.by_name),
                         {"nvs", "phy_init", "factory", "ota_0", "otadata", "agent_nvs", "model", "art"})

    def test_the_pet_save_keeps_the_default_offset_and_size(self):
        self.assertEqual((self.by_name["nvs"]["offset"], self.by_name["nvs"]["size"]), (0x9000, 0x6000))
        self.assertEqual((self.by_name["phy_init"]["offset"], self.by_name["phy_init"]["size"]), (0xF000, 0x1000))

    def test_partitions_are_aligned_ordered_and_inside_flash(self):
        end = TABLE_OFFSET + TABLE_BYTES
        for entry in self.table:
            with self.subTest(name=entry["name"]):
                alignment = 0x10000 if entry["type"] == "app" else 0x1000
                self.assertEqual(entry["offset"] % alignment, 0)
                self.assertEqual(entry["size"] % 0x1000, 0)
                self.assertGreaterEqual(entry["offset"], end, "overlaps the previous partition")
                end = entry["offset"] + entry["size"]
        self.assertLessEqual(end, FLASH_BYTES)

    def test_apps_and_boot_selection(self):
        factory, agent, otadata = (self.by_name[name] for name in ("factory", "ota_0", "otadata"))
        self.assertEqual((factory["type"], factory["subtype"], factory["offset"]), ("app", "factory", 0x10000))
        self.assertEqual((agent["type"], agent["subtype"]), ("app", "ota_0"))
        self.assertEqual((otadata["type"], otadata["subtype"], otadata["size"]), ("data", "ota", 0x2000))
        # The pet was about 5.5 MB with its artwork when layout A was chosen.
        self.assertGreaterEqual(factory["size"], 7 * 1024 * 1024)
        self.assertGreaterEqual(agent["size"], 4 * 1024 * 1024)
        self.assertEqual([entry["name"] for entry in self.table if entry["type"] == "app"],
                         ["factory", "ota_0"])

    def test_the_agent_keeps_its_settings_apart_from_the_pet_save(self):
        agent_nvs = self.by_name["agent_nvs"]
        self.assertEqual((agent_nvs["type"], agent_nvs["subtype"]), ("data", "nvs"))
        self.assertGreaterEqual(agent_nvs["size"], 0x3000, "NVS needs at least three pages")


class ProjectAgreementTests(unittest.TestCase):
    def test_both_projects_use_this_table_with_rollback(self):
        pet = (REPOSITORY / "ports/esp32/sdkconfig.defaults").read_text()
        agent = (REPOSITORY / "ports/esp32-agent/sdkconfig.defaults").read_text()
        self.assertIn('CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"', pet)
        self.assertIn('CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="../esp32/partitions.csv"', agent)
        for text in (pet, agent):
            self.assertIn("CONFIG_PARTITION_TABLE_CUSTOM=y", text)
            self.assertIn("CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y", text)

    def test_each_app_names_the_other_by_its_project(self):
        def project(path):
            return re.search(r"^project\((\w+)\)", (REPOSITORY / path).read_text(), re.M).group(1)

        pet, agent = project("ports/esp32/CMakeLists.txt"), project("ports/esp32-agent/CMakeLists.txt")
        self.assertEqual((pet, agent), ("jelligotchi", "jelli_agent"))
        switch = (REPOSITORY / "ports/esp32/main/app_switch.c").read_text()
        self.assertIn(f'#define AGENT_PROJECT "{agent}"', switch)
        boot = (REPOSITORY / "ports/esp32-agent/main/boot.c").read_text()
        self.assertIn(f'#define PET_PROJECT "{pet}"', boot)
        self.assertIn(f'"$build_dir/{agent}.bin"', (REPOSITORY / "scripts/esp").read_text())


if __name__ == "__main__":
    unittest.main()
