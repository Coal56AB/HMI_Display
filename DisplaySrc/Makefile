.DEFAULT_GOAL := all
CC = gcc
CFLAGS ?= -std=c99 -Os -Wall -Wextra -Werror
CPPFLAGS ?= -IInclude -IThirdParty/tinf
PYTHON ?= python3
LDLIBS ?=
ifeq ($(OS),Windows_NT)
EXE = .exe
else
EXE =
endif
SOURCES = Src/hmi_storage.c ThirdParty/tinf/tinflate.c Src/hmi_gfx.c Src/hmi_scene.c Src/hmi_scene_generated.c Src/hmi_dirty.c Src/hmi_dynamic.c Src/hmi_ui.c
HEADERS = $(wildcard Include/*.h) $(wildcard Src/*.inc)
PROGRAMS = host_preview test_full_frames test_dirty_updates test_touch test_ui_dirty test_stats test_graph_diff test_graph_diff_external ui_preview
TARGETS = $(addprefix .build/,$(addsuffix $(EXE),$(PROGRAMS)))
.PHONY: all test references clean
all: $(TARGETS) .build/backend.o
.build:
	$(PYTHON) -c "from pathlib import Path; Path('.build').mkdir(exist_ok=True)"
.build/host_preview$(EXE): $(SOURCES) Examples/host_preview.c $(HEADERS) | .build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) Examples/host_preview.c $(LDLIBS) -o $@
.build/ui_preview$(EXE): $(SOURCES) Examples/ui_preview.c $(HEADERS) | .build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) Examples/ui_preview.c $(LDLIBS) -o $@
.build/test_%$(EXE): $(SOURCES) Tests/test_%.c $(HEADERS) | .build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) Tests/test_$*.c $(LDLIBS) -o $@
.build/test_graph_diff_external$(EXE): $(SOURCES) Tests/test_graph_diff.c $(HEADERS) | .build
	$(CC) $(CPPFLAGS) $(CFLAGS) -DHMI_EXTERNAL_ASSETS=1 $(SOURCES) Tests/test_graph_diff.c $(LDLIBS) -o $@
.build/backend.o: Examples/stm32_display_backend_example.c $(HEADERS) | .build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
test: all
	$(PYTHON) Tests/check_frames.py
	./.build/test_dirty_updates$(EXE)
	./.build/test_touch$(EXE)
	./.build/test_ui_dirty$(EXE)
	./.build/test_stats$(EXE)
	./.build/test_graph_diff$(EXE)
	./.build/test_graph_diff_external$(EXE)
references:
	$(PYTHON) Scripts/generate_profiles.py
clean:
	$(PYTHON) -c "from pathlib import Path; [p.unlink() for p in Path('.build').glob('*') if p.is_file()]"
