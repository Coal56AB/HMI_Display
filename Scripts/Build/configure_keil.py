"""Generate a disposable Keil project without modifying the platform template."""
from pathlib import Path
import argparse
from keil_module import connect
ROOT=Path(__file__).resolve().parents[2]
def configure(module, output=None):
    if not (Path(module)/'module.json').exists(): module=ROOT/'Firmware/Api'
    source=ROOT/'Firmware/BluePillHMI/MDK-ARM/BluePillHMI.uvprojx'
    output=output or ROOT/'.build/Keil/BluePillHMI.uvprojx'
    return connect(module,source,'ExternalFlash',output)
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--display-dir',type=Path,default=ROOT/'DisplaySrc');a=p.parse_args();print(configure(a.display_dir))
