# PlatformIO post-build script: firmware.hex -> firmware.uf2, then check the UF2.
# The build fails (and the .uf2 is removed) if the check does not pass.
import os
import subprocess

Import("env")  # noqa: F821 (provided by PlatformIO/SCons)

UF2_FAMILY = "0xADA52840"  # Adafruit nRF52840 family, accepted by the T1000-E bootloader


def hex_to_uf2(source, target, env):
    hex_path = target[0].get_abspath()
    uf2_path = os.path.splitext(hex_path)[0] + ".uf2"
    python = env.subst("$PYTHONEXE")
    core_dir = env.PioPlatform().get_package_dir("framework-arduinoadafruitnrf52")
    uf2conv = os.path.join(core_dir, "tools", "uf2conv", "uf2conv.py")
    uf2check = os.path.join(env.subst("$PROJECT_DIR"), "tools", "uf2check.py")

    # Addresses come from the .hex (i.e. from the linker script), not from a -b option,
    # so the check below verifies what the linker actually produced.
    # -W: uf2conv.py from the core has an old regex literal that Python 3.12+ warns about.
    subprocess.run(
        [python, "-W", "ignore::SyntaxWarning", uf2conv, hex_path,
         "--convert", "--family", UF2_FAMILY, "--output", uf2_path],
        check=True,
        stdout=subprocess.DEVNULL,
    )
    if subprocess.run([python, uf2check, uf2_path]).returncode != 0:
        os.remove(uf2_path)
        return 1
    return 0


env.AddPostAction("$BUILD_DIR/${PROGNAME}.hex", hex_to_uf2)  # noqa: F821
