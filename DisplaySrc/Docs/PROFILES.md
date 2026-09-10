# STM32F103 profiles

`HMI_LITE=0, HMI_EXTERNAL_ASSETS=0` (default): full internal resources for host
preview and larger MCUs. Original scene/glyph hashes remain unchanged.

`HMI_LITE=1, HMI_EXTERNAL_ASSETS=0`: reduced 10 px regular font, all 192 characters,
four coverage levels. Tiny decorative lines and baked demonstration traces are
removed; all 47 scenes, the touch controller and live graph rendering remain.
Resource pages remain 4096-byte raw DEFLATE streams. One decompression page and
48 retained 64-byte lines avoid repeatedly inflating pages when table accesses
interleave. No heap or framebuffer; the resource image format is unchanged.

`HMI_LITE=0, HMI_EXTERNAL_ASSETS=1`: all original fonts and graphics, resource image
`Assets/hmi_assets.bin` read through `HmiAssetReader`, with a 64-line, 4096-byte
LRU cache. Call `hmi_storage_init(reader,user)` before initializing/rendering the UI.
The reader receives byte offsets relative to the start of the image and must
return nonzero only after exactly the requested bytes have been copied.
Initialization validates version, size and CRC32 of the full payload.
`hmi_storage_error()` reports subsequent transport errors; abort rendering/recover
in your application after such errors. The interface is synchronous and single-threaded.

ExternalFlash HmiUi loads pre-rendered RGB565 backgrounds for all 47 scenes.
The appended raster directory maps 60 strips per scene to shared lossless
PackBits streams (320x8 pixels each). The controller copies/fills decoded pixel
runs; it does not execute geometry or rasterize static captions on this path.
State-dependent backgrounds use the corresponding ready scene variant. Live
values, selector backgrounds and telemetry overlays remain separate layers.

The 2304-byte decoded geometry cache is retained only for legacy Lite, freeing
RAM for UART telemetry in the board project. The external resource cache remains
64x64 bytes plus 128 glyph metrics. Its pixel buffer is 2560 RGB565 pixels (5 KiB),
not a framebuffer. Full frames use 60 LCD transfers, dirty rectangles retain
clipping and cancellation between transfers. The procedural path remains the
host reference and the low-level legacy API; production uses HmiUi.

Generate backgrounds with `python Scripts/generate_rasters.py` after changing
static layout or replacement anchors. `generate_profiles.py` also invokes it.
The image includes original font/vector resources followed by the raster tail;
PCH1 length and CRC cover the complete image. Reflash both firmware and SPI NOR
when regenerating, since the compiled length/CRC and raster offsets must agree.
`Scripts/test_rasters.py` in the parent project compares 141 UI frames and 103
transitions byte-for-byte to procedural output, including live telemetry overlays.

Both nondefault profiles use virtual address tokens internally. They are not
memory-mapped flash addresses and must never be dereferenced directly. Resource
access goes through the helpers in `hmi_storage.h`; ordinary RAM strings remain
directly readable. The LCD transport is independent of the asset reader.

Compile all `Src/*.c`, plus `ThirdParty/tinf/tinflate.c`, and add `include` and
`ThirdParty/tinf` to include paths. Set `NDEBUG` for embedded release builds
(tinf's public input bounds/error checks remain active).

To share a board configuration with the library, define `HMI_USE_PROJECT_CONFIG`
and add the directory containing `project_config.h` to the include paths.
`hmi_config.h` includes that header before selecting defaults. Compiler-forced
pre-inclusion is unnecessary; standalone builds still use the defaults above.

`make references` / `python Scripts/generate_profiles.py` reproducibly regenerate
both profiles from `Assets/asset_source.json.gz`. `pack_assets.py` is the lower
level generator used in a temporary directory; after importing a new legacy export
with `pack_assets.py --import-dir PATH`, run `generate_profiles.py` again.
Install `zopfli==0.4.3` in the host Python environment before regeneration. Lite
uses Zopfli (15 iterations) for the same raw DEFLATE pages: 29417 bytes including
the index instead of 31404, leaving Flash space for local UI invalidation.
Firmware builds use the checked-in generated assets and do not need Python/Zopfli.

The parent CubeMX/Keil project provides the ILI9486/RPi SPI adapter, XPT2046,
SPI NOR reader/UART installer and protocol callback example. This library contains
no STM32 HAL dependencies. Settings and event history are stored in RAM only.
