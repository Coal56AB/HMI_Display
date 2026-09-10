# Самостоятельный модуль ПЧ

Эту папку можно скопировать отдельно от родительского проекта.
Для генерации ресурсов, C-рендерера и запуска симулятора папка `Firmware` не нужна.

## Папки

- `Src`, `Include` — общий для STM32 и ПК C99-код отрисовки, касаний и протокола.
- `Assets` — исходные и собранные ресурсы.
- `Simulator` — весь интерфейс ПК, модель ПЧ, UART и адаптер C-рендерера.
- `Scripts` — генерация ресурсов, подготовка шрифтов и сборка симулятора.
- `Tests` — проверки рендерера, команд, состояния и журнала.
- `Platform/Stm32` — адаптер к общей прошивке и постоянный журнал ПЧ.
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
`Include/display_module.h` — версия 1 контракта общей платформы.
Смысл событий, протокол UART и таблица записей журнала остаются внутри модуля.

[Протокол ПЧ](Docs/UART_TELEMETRY.md) · [Отрисовка](Docs/RASTER_RENDERING.md)
