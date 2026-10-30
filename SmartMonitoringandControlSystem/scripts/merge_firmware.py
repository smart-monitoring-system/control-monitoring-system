"""Generate a complete bootloader/partition/application image for Wokwi."""
Import("env")
from pathlib import Path
import subprocess

def merge_image(source, target, env):
    build = Path(env.subst("$BUILD_DIR"))
    tool = Path(env.PioPlatform().get_package_dir("tool-esptoolpy")) / "esptool.py"
    images = [(str(offset), env.subst(path)) for offset, path in env.get("FLASH_EXTRA_IMAGES", [])]
    images.append((env.subst("$ESP32_APP_OFFSET"), str(build / "firmware.bin")))
    command = [env.subst("$PYTHONEXE"), str(tool), "--chip", "esp32", "merge_bin",
               "-o", str(build / "firmware-merged.bin")]
    for offset, path in images:
        command.extend([offset, path])
    subprocess.check_call(command)

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_image)
