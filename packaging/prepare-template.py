"""Overlay the plugin sources onto an independent OBS Plugin Template checkout."""
import json
import shutil
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
template = Path(sys.argv[1]).resolve()
version = sys.argv[2]
spec_file = template / "buildspec.json"
spec = json.loads(spec_file.read_text(encoding="utf-8"))
spec.update(name="obs-dj-autogain", displayName="DJ AutoGain", version=version,
            author="MarcSoul", website="https://marcsoul.com")
spec["platformConfig"]["macos"]["bundleId"] = "com.marcsoul.obs-dj-autogain"
spec_file.write_text(json.dumps(spec, indent=2) + "\n", encoding="utf-8")

cmake = template / "CMakeLists.txt"
old = cmake.read_text(encoding="utf-8")
expected = "target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/plugin-main.c)"
if old.count(expected) != 1:
    raise SystemExit("OBS Plugin Template layout changed. Review the CMake overlay.")
cmake.write_text(old.replace(expected, """target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/plugin.cpp)
target_compile_features(${CMAKE_PROJECT_NAME} PRIVATE cxx_std_17)
if(MSVC)
  target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE /utf-8)
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE NOMINMAX)
endif()"""), encoding="utf-8")

for filename in ("plugin.cpp", "autogain.hpp", "loudness.hpp"):
    shutil.copy2(root / filename, template / "src" / filename)
shutil.rmtree(template / "data", ignore_errors=True)
(template / "data").mkdir()
