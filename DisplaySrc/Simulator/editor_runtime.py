"""Run module-owned C navigation; no screen hit maps are duplicated in Python."""

import ctypes as C
import struct
from pathlib import Path
import tempfile
from PySide6.QtGui import QImage


class Event(C.Structure):
    _fields_ = [
        ("sequence", C.c_uint32),
        ("type", C.c_int),
        ("id", C.c_uint16),
        ("value", C.c_float),
        ("flags", C.c_uint32),
        ("data", C.c_uint8 * 8),
    ]


JOURNAL_HOST = r"""
#include "module_services.h"
#include "journal_app.h"
#include "journal_store.h"
static uint8_t editor_flash[JOURNAL_BANK*2];
static uint32_t editor_now(void){return now_ms;}
static int editor_read(uint32_t at,void*d,uint32_t n){if(at<JOURNAL_BASE||at-JOURNAL_BASE+n>sizeof(editor_flash))return 0;memcpy(d,editor_flash+at-JOURNAL_BASE,n);return 1;}
static int editor_write(uint32_t at,const void*d,uint32_t n){if(at<JOURNAL_BASE||at-JOURNAL_BASE+n>sizeof(editor_flash))return 0;memcpy(editor_flash+at-JOURNAL_BASE,d,n);return 1;}
static int editor_erase(uint32_t at){if(at<JOURNAL_BASE||at-JOURNAL_BASE+4096>sizeof(editor_flash))return 0;memset(editor_flash+at-JOURNAL_BASE,255,4096);return 1;}
static DisplayPlatform editor_platform={2,320,480,flush,read_asset,editor_read,editor_write,editor_erase,editor_now,telemetry_send,telemetry_reset,0};
const DisplayPlatform*module_platform=&editor_platform;
API void editor_journal_init(void){memset(editor_flash,255,sizeof(editor_flash));journal_app_init(&ui);for(int i=0;i<500;i++)journal_app_tick(now_ms);}
API void editor_journal_tick(void){journal_app_tick(now_ms);}
API void editor_journal_event(const HmiEvent*event){journal_app_event(event);}
"""


class PchRuntime:
    def __init__(self, module, services):
        self.module = Path(module)
        self.temporary = tempfile.TemporaryDirectory(prefix="hmi-navigation-")
        work = Path(self.temporary.name)
        adapter = self.module / "Simulator/native_renderer.c"
        text = adapter.read_text(encoding="utf-8")
        text += "\nAPI unsigned editor_scene(void){return hmi_scene_for_state(&ui.state);}\n"
        text += "API void editor_scene_select(unsigned n){ui.state.scene_override=(HmiSceneId)n;hmi_invalidate_all();}\n"
        text += "API void editor_advance(unsigned tick){now_ms=tick;telemetry_poll(&ui,tick); }\n"
        text += (
            "API unsigned editor_connected(void){return ui.state.telemetry_flags&1u;}\n"
        )
        self.journal = (self.module / "Platform/Common/journal_app.c").exists()
        if self.journal:
            text += JOURNAL_HOST
        text += "API void editor_close(void){if(assets){fclose(assets);assets=0;}}\n"
        wrapper = work / "navigation.c"
        wrapper.write_text(text, encoding="utf-8")
        sources = list((self.module / "Src").glob("*.c")) + [
            self.module / "ThirdParty/tinf/tinflate.c",
            wrapper,
        ]
        if self.journal:
            sources += [
                self.module / "Platform/Common/journal_app.c",
                self.module / "Platform/Common/journal_store.c",
            ]
        self.path = work / "navigation.dll"
        cc = services.compiler()
        services.execute(
            [
                cc,
                "-shared",
                "-static-libgcc",
                "-std=c99",
                "-O1",
                "-DHMI_EXTERNAL_ASSETS=1",
                "-I" + str(self.module / "Include"),
                "-I" + str(self.module / "Platform/Common"),
                "-I" + str(self.module.parent / "Firmware/Api"),
                "-I" + str(self.module / "ThirdParty/tinf"),
                *map(str, sources),
                "-o",
                str(self.path),
            ],
            self.module,
            cc,
        )
        self.lib = C.CDLL(str(self.path))
        self.lib.renderer_init.argtypes = [C.c_char_p]
        self.lib.renderer_frame.restype = C.POINTER(C.c_uint16)
        self.lib.renderer_touch.argtypes = [C.c_int, C.c_int, C.c_int, C.c_uint]
        if not self.lib.renderer_init(
            str(self.module / "Assets/hmi_assets.bin").encode()
        ):
            raise RuntimeError("Ресурсы модуля несовместимы с его C-рендерером")
        self.tick = 100
        if self.journal:
            self.lib.editor_journal_init()

    @property
    def scene(self):
        return self.lib.editor_scene()

    def select(self, index):
        self.lib.editor_scene_select(index)

    def touch(self, x, y, down):
        self.tick += 40
        self.lib.renderer_touch(x, y, int(down), self.tick)

    def advance(self, milliseconds):
        self.tick += milliseconds
        self.lib.editor_advance(self.tick)
        if self.journal:
            self.lib.editor_journal_tick()

    def feed(self, data):
        self.lib.renderer_packet(data, len(data), self.tick)

    def events(self):
        event = Event()
        result = []
        while self.lib.renderer_event(C.byref(event)):
            if self.journal:
                self.lib.editor_journal_event(C.byref(event))
            result.append(
                struct.pack(
                    "<IBBHfI8s",
                    event.sequence,
                    event.type,
                    0,
                    event.id,
                    event.value,
                    event.flags,
                    bytes(event.data),
                )
            )
        return result

    def frame(self):
        return QImage(
            C.string_at(self.lib.renderer_frame(), 320 * 480 * 2),
            320,
            480,
            640,
            QImage.Format.Format_RGB16,
        ).copy()

    def close(self):
        if getattr(self, "lib", None):
            self.lib.editor_close()
            C.windll.kernel32.FreeLibrary(C.c_void_p(self.lib._handle))
            self.lib = None
        self.temporary.cleanup()
