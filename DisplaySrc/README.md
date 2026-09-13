# Самостоятельный модуль ПЧ

Эту папку можно скопировать отдельно от родительского проекта.
Для генерации ресурсов, C-рендерера и запуска симулятора папка `Firmware` не нужна.

## Папки

- `Src`, `Include` — общий для STM32, ESP32 и ПК C99-код отрисовки, касаний и протокола.
- `Assets` — исходные и собранные ресурсы.
- `Simulator` — весь интерфейс ПК, модель ПЧ, UART и адаптер C-рендерера.
- `Scripts` — генерация ресурсов, подготовка шрифтов и сборка симулятора.
- `Tests` — проверки рендерера, команд, состояния и журнала.
- `Platform/Common` — общий адаптер к API прошивки и постоянный журнал ПЧ.
- `esp32` — подключение модуля к ESP32-S3, ресурсы и UART.
- `ThirdParty` — используемый декодер tinf с лицензией; это зависимость рендерера.
- `Docs` — документация именно этого интерфейса.
- `Release` — собранные DLL/EXE, не входят в Git.

## Без родительского проекта

Нужны Python 3, зависимости `requirements.txt` и MinGW GCC.
Скрипт ищет GCC в PATH, в `CC` или по стандартному для этого компьютера пути `C:/mingw64/bin/gcc.exe`.

```text
python -m pip install -r requirements.txt
python Scripts/generate_rasters.py
python Scripts/build_native_renderer.py
Simulator/START.cmd
```

Для EXE: `python Scripts/build_simulator.py`.
Результат: `Release/Simulator/PCH_Simulator/`; переносить всю папку с `_internal`.

Проверки: `mingw32-make -j2 test PYTHON=python`, затем `python Tests/check_uart.py`,
`python Tests/check_rendering.py`, `python Tests/check_review.py`, `python Tests/check_journal.py`,
`python Tests/check_storage.py`, `python Tests/check_desktop.py`.

## Подключение к STM32

`module.json` перечисляет исходники, include-каталоги, defines и файл ресурсов.
`Firmware/Api/display_api.h` — контракт API v2, принадлежащий платформе.
Адаптер использует только этот API; исходники платы не меняются.
Смысл событий, протокол UART и таблица записей журнала остаются внутри модуля.

[Протокол ПЧ](Docs/UART_TELEMETRY.md) · [Отрисовка](Docs/RASTER_RENDERING.md)

## Подключение к ESP32-S3

Из корня репозитория запустите `BUILD_ESP32.cmd`. Для сборки нужен PlatformIO;
подключение дисплея и сенсора описано в [проекте ESP32](../Firmware/ESP32/README.md).
Ресурсы интерфейса включены в прошивку. После изменения ресурсов сначала выполните
`python DisplaySrc/Scripts/generate_rasters.py` из корня репозитория.

По умолчанию симулятор подключается к COM-порту встроенного USB ESP32-S3.
В `esp32/config.h` можно выбрать внешний UART через `PCH_USE_USB=0`.
UART ПЧ: 115200 бит/с, 8N1, TX — GPIO1, RX — GPIO2.
Соедините TX с RX другого устройства, RX с TX и общий GND.
Настройки находятся в `esp32/config.h`. Интерфейс сохраняет размер 320×480.

Сборка и загрузка: `BUILD_ESP32.cmd -Upload -Port COM5` (укажите свой порт).
Для проверки одного дисплея и сенсора: `BUILD_ESP32.cmd -Environment template`.

[Интеграция платформ и проверки](Docs/PLATFORMS.md).

## Подключить к существующему проекту Keil

Запустите `CONNECT.cmd` (Python 3.7 и `Scripts/requirements-connect.txt`).
Выберите совместимый `.uvprojx`, конфигурацию сборки и нажмите «Подключить интерфейс».
Скрипт автономен: ему нужен только этот DisplaySrc и выбранный проект с API v2.
Будут заменены группа DisplaySrc, include-пути и defines; пустая реализация исключается.
C-файлы платформы не меняются. Галочка «Создавать резервную копию (.bak)»
включена по умолчанию; её можно отключить.

Обычная сборка через корневой BUILD создаёт отдельный временный проект и вообще
не меняет исходный `.uvprojx`. Интеграционный тест окна: `Tests/check_connector.py`
под Python 3.7/PySide2 (для этого теста рядом нужна общая платформа).

## В HMI-Editor

Откройте модуль в HMI-Editor: стенд управления появится в панели «Симулятор модуля». Заряд, пуск, энкодер, внешние сигналы и уставки работают с экраном редактора. Пауза останавливает сессию, снятие отметки «Связь с контроллером» разрывает обмен. Для встроенного режима нужна рабочая копия с Firmware/Api и MinGW GCC.
