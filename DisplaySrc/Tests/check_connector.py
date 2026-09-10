"""Run the actual PySide2 attach button against a disposable compatible project."""
from pathlib import Path
import os,sys,tempfile,hashlib,xml.etree.ElementTree as ET
os.environ['QT_QPA_PLATFORM']='offscreen'
MODULE=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(MODULE/'Scripts'))
from connect_project import Window
from keil_module import connect
from PySide2 import QtWidgets
root=MODULE.parent
source=root/'Firmware/BluePillHMI/MDK-ARM/BluePillHMI.uvprojx'
original=source.read_bytes()
assert (MODULE/'Scripts/keil_module.py').read_bytes()==(root/'Scripts/Build/keil_module.py').read_bytes()
app=QtWidgets.QApplication([])
with tempfile.TemporaryDirectory(prefix='display-qt-') as tmp:
    project=Path(tmp)/'selected.uvprojx'
    connect(MODULE,source,'ExternalFlash',project)
    window=Window();window.path.setText(str(project));window.inspect()
    assert window.apply.isEnabled()
    assert window.backup.isChecked()
    window.apply.click()
    assert 'Готово' in window.info.toPlainText(),window.info.toPlainText()
    assert project.with_name(project.name+'.bak').is_file()
    first=project.read_bytes();window.apply.click();assert first==project.read_bytes()
    tree=ET.parse(str(project));names=[n.text for n in tree.iter('FileName')]
    assert 'display_module.c' in names and 'display_empty.c' not in names
    bad=Path(tmp)/'bad';bad.mkdir();(bad/'module.json').write_text('{"apiVersion":999}')
    try:connect(bad,project,'ExternalFlash')
    except ValueError:pass
    else:raise AssertionError('Incompatible module accepted')
    assert project.read_bytes()==first
    other=Path(tmp)/'without-backup.uvprojx'
    connect(MODULE,source,'ExternalFlash',other)
    window.path.setText(str(other));window.inspect();window.backup.setChecked(False)
    window.apply.click()
    assert 'Готово' in window.info.toPlainText(),window.info.toPlainText()
    assert not other.with_name(other.name+'.bak').exists()
    assert 'Резервная копия:' not in window.info.toPlainText()
    window.close()
assert source.read_bytes()==original
print('Connector PASS: Qt attach, backup, repeat attach, incompatible API refusal; source template unchanged')
