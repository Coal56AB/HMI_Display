from pathlib import Path
import PySide6

root = Path(SPECPATH).parent
a = Analysis(
    [str(root / "Simulator/desktop_simulator.py")],
    pathex=[str(root / "Simulator")],
    binaries=[(str(root / "Release/Native/hmi_renderer.dll"), ".")],
    datas=[(str(root / name), ".") for name in (
        "Simulator/uart_simulator.html", "Simulator/uart_bridge.js",
        "Release/Native/renderer-build.json", "Assets/hmi_assets.bin")],
    hiddenimports=["serial", "serial.tools.list_ports"],
)
# Avoid unrelated DLLs found on the developer's PATH shadowing Windows APIs.
a.binaries = [entry for entry in a.binaries
    if Path(entry[0]).name.lower() not in ("icuuc.dll", "icudt78.dll", "ucrtbase.dll")
    and not Path(entry[0]).name.lower().startswith("api-ms-win-")]
# Use the runtime supplied with the Qt version being packaged.
qt = Path(PySide6.__file__).parent
for name in ("VCRUNTIME140.dll", "VCRUNTIME140_1.dll"):
    a.binaries = [entry for entry in a.binaries if entry[0].lower() != name.lower()]
    a.binaries.append((name, str(qt / name), "BINARY"))
pyz = PYZ(a.pure)
exe = EXE(pyz, a.scripts, [], exclude_binaries=True, name="PCH_Simulator", console=False)
coll = COLLECT(exe, a.binaries, a.datas, name="PCH_Simulator")
