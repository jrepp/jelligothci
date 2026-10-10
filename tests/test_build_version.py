"""Exercise VERSION changes in an existing CMake build, without reconfiguration."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class BuildVersionTest(unittest.TestCase):
    def test_version_change_reconfigures_existing_build(self):
        repository = Path(__file__).resolve().parent.parent
        with tempfile.TemporaryDirectory(prefix="jelli-version-") as directory:
            source = Path(directory) / "source"
            build = Path(directory) / "build"
            source.mkdir()
            # Everything the core configure step reads: sources plus the content
            # data and the CMake modules that generate code from it.
            for name in ("cmake", "content", "core", "include", "tests", "tools"):
                shutil.copytree(repository / name, source / name)
            shutil.copy(repository / "CMakeLists.txt", source / "CMakeLists.txt")
            version = source / "VERSION"
            version.write_text("1.2.3\n")
            subprocess.run(
                ["cmake", "-S", str(source), "-B", str(build),
                 "-DJELLI_BUILD_SDL=OFF", "-DBUILD_TESTING=OFF"], check=True,
            )
            subprocess.run(["cmake", "--build", str(build), "--parallel", "2"], check=True)
            version.write_text("1.2.4\n")
            # No configure command here: the build must detect the changed input.
            subprocess.run(["cmake", "--build", str(build), "--parallel", "2"], check=True)
            self.assertIn(
                "CMAKE_PROJECT_VERSION:STATIC=1.2.4",
                (build / "CMakeCache.txt").read_text(),
            )


if __name__ == "__main__":
    unittest.main()
