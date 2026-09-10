"""PySide2 GUI for connecting this DisplaySrc to a compatible Keil project."""
import sys, json, shutil
from pathlib import Path
from PySide2 import QtCore, QtWidgets
from keil_module import targets, connect
MODULE = Path(__file__).resolve().parents[1]

class Window(QtWidgets.QWidget):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Подключение интерфейса к проекту")
        self.resize(720, 440)
        layout=QtWidgets.QVBoxLayout(self)
        spec=json.loads((MODULE/'module.json').read_text(encoding='utf-8'))
        title=QtWidgets.QLabel("Интерфейс: {} · API {}".format(spec['name'],spec['apiVersion']))
        title.setStyleSheet("font-size: 18px; font-weight: bold")
        layout.addWidget(title)
        label=QtWidgets.QLabel(str(MODULE));label.setWordWrap(True);layout.addWidget(label)
        row=QtWidgets.QHBoxLayout();self.path=QtWidgets.QLineEdit();self.path.setPlaceholderText("Выберите файл проекта .uvprojx")
        browse=QtWidgets.QPushButton("Обзор…");row.addWidget(self.path);row.addWidget(browse);layout.addLayout(row)
        form=QtWidgets.QFormLayout();self.target=QtWidgets.QComboBox();form.addRow("Конфигурация сборки",self.target);layout.addLayout(form)
        self.info=QtWidgets.QPlainTextEdit();self.info.setReadOnly(True);layout.addWidget(self.info)
        self.backup=QtWidgets.QCheckBox("Создавать резервную копию (.bak)");self.backup.setChecked(True);layout.addWidget(self.backup)
        self.apply=QtWidgets.QPushButton("Подключить интерфейс");self.apply.setEnabled(False);layout.addWidget(self.apply)
        browse.clicked.connect(self.choose);self.path.editingFinished.connect(self.inspect)
        self.apply.clicked.connect(self.attach)
        self.info.setPlainText("Будут подключены исходники, include-пути и параметры компиляции модуля. "
            "C-код платформы не изменяется. Создание резервной копии можно отключить галочкой.")
    def choose(self):
        path,_=QtWidgets.QFileDialog.getOpenFileName(self,"Проект Keil","","Keil project (*.uvprojx)")
        if path:self.path.setText(path);self.inspect()
    def inspect(self):
        self.apply.setEnabled(False);self.target.clear()
        try:
            path=Path(self.path.text()).resolve()
            self.target.addItems(targets(path))
            spec=json.loads((MODULE/'module.json').read_text(encoding='utf-8'))
            self.info.setPlainText("Подключаемые исходники:\n"+"\n".join(spec['firmware']['sources'])+
                "\n\nInclude-пути:\n"+"\n".join(spec['firmware']['includes'])+
                "\n\nDefines: "+", ".join(spec['firmware'].get('defines',[])))
            self.apply.setEnabled(self.target.count()>0)
        except Exception as error:self.info.setPlainText(str(error))
    def attach(self):
        self.apply.setEnabled(False)
        try:
            project=Path(self.path.text()).resolve()
            # Validate into a temporary file before touching the selected project.
            import tempfile
            with tempfile.TemporaryDirectory(prefix='display-connect-') as tmp:
                connect(MODULE,project,self.target.currentText(),Path(tmp)/project.name)
            backup=project.with_name(project.name+".bak")
            if self.backup.isChecked():
                shutil.copy2(str(project),str(backup))
            connect(MODULE,project,self.target.currentText())
            backup_note="\nРезервная копия: "+str(backup) if self.backup.isChecked() else ""
            self.info.appendPlainText("\nГотово. Исходники платформы не изменены."+backup_note+
                "\nОткройте проект заново в Keil и выполните сборку.")
        except Exception as error:self.info.appendPlainText("\nОшибка: "+str(error))
        finally:self.apply.setEnabled(True)

if __name__=='__main__':
    app=QtWidgets.QApplication(sys.argv)
    window=Window();window.show();sys.exit(app.exec_())
