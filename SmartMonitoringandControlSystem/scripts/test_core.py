"""Run pure controller tests on the host without ESP hardware or PlatformIO."""
from pathlib import Path
import os
import shlex
import subprocess

root = Path(__file__).resolve().parents[1]
build = root / ".pio" / "host-tests"
build.mkdir(parents=True, exist_ok=True)
compiler = shlex.split(os.environ.get("CC", "cc"))
subprocess.run(compiler + ["-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic",
    "-g", "-I", str(root / "src"),
    str(root / "test/host/test_controller_core.c"),
    str(root / "src/controller_core.c"), str(root / "src/occupancy.c"),
    str(root / "src/access_control.c"), str(root / "src/system_types.c"),
    "-o", str(build / "controller-tests")], check=True)
subprocess.run([str(build / "controller-tests")], check=True)

subprocess.run(compiler + ["-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic",
    "-g", "-I", str(root / "src"),
    str(root / "test/host/test_room_control_core.c"),
    str(root / "src/room_control_core.c"), str(root / "src/system_types.c"),
    "-o", str(build / "room-control-tests")], check=True)
subprocess.run([str(build / "room-control-tests")], check=True)
