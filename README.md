# Основа GUI для STM32F103 и экрана 320×480

Пустой рабочий проект: STM32F103C8, ILI9486 с Raspberry Pi SPI-переходником,
XPT2046, W25Q16, UART1. После запуска экран чёрный, касания доступны через
`app_touch()` и `app_debug.touch`. Интерфейса ПЧ и его логики здесь нет.

Откройте `Firmware/BluePillHMI/MDK-ARM/BluePillHMI.uvprojx`, target **Display**.
Конфигурация дисплея и калибровка сенсора — `Config/project_config.h`.
Размещайте исходники своего GUI в `DisplaySrc`; подключайте его из `App/app.c`. `board_write_rect()` принимает RGB565,
`board_touch_sample()` читает сенсор, `board_flash_*()` работают с Flash.
UART1: PA9 TX, PA10 RX, 115200 8N1. Пины и тактирование — в `.ioc`.

Для нового GUI создайте ветку от этого коммита `init`.

Назначение папки интерфейса: [DisplaySrc/README.md](DisplaySrc/README.md).
