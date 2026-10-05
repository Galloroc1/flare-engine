"""Run real engine quest tests using an existing CMake build and isolated saves."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

engine = Path(__file__).resolve().parents[1]
build = engine / "build"
subprocess.run(["cmake", "--build", str(build), "-j4"], check=True)
flags = {}
for line in (build / "CMakeFiles/flare.dir/flags.make").read_text().splitlines():
    if " = " in line:
        key, value = line.split(" = ", 1)
        flags[key] = shlex.split(value)
with tempfile.TemporaryDirectory(prefix="flare-chief-test-") as temporary:
    folder = Path(temporary)
    obj = folder / "test.o"
    binary = folder / "test"
    link = shlex.split((build / "CMakeFiles/flare.dir/link.txt").read_text())
    # Assertions must stay enabled even when the game was built in Release mode.
    compile_flags = [flag for flag in flags["CXX_FLAGS"] if flag != "-DNDEBUG"]
    subprocess.run([link[0], *flags["CXX_DEFINES"], *flags["CXX_INCLUDES"],
                    *compile_flags, "-UNDEBUG", "-c", str(engine / "tests/village_quests_integration.cpp"),
                    "-o", str(obj)], check=True)
    link = [str(obj) if token.endswith("/src/main.cpp.o") else token for token in link]
    link[link.index("-o") + 1] = str(binary)
    subprocess.run(link, cwd=build, check=True)
    environment = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy",
                       XDG_CONFIG_HOME=str(folder / "config"), XDG_DATA_HOME=str(folder / "data"))
    (folder / "config/flare").mkdir(parents=True)
    (folder / "config/flare/settings.txt").write_text("language=zh\nsetup_language=1\n")
    (folder / "data/flare/saves").mkdir(parents=True)
    result = subprocess.run([str(binary), str(engine.parent / "flare-game")],
                            cwd=build, env=environment, text=True, capture_output=True)
    (build / "village-quests-test.log").write_text(result.stdout + result.stderr)
    print(result.stdout, end="")
    if result.returncode:
        print(result.stderr)
        raise SystemExit(result.returncode)
