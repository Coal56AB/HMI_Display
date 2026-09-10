# PCH-1000 HMI C renderer

- Change only the C99 HMI renderer and its editor project metadata.
- Do not change Android application files from this repository task.
- Keep the logical display at 320x480 RGB565.
- Prefer drawing primitives. Raster masks are allowed only for icons and schematic symbols; recolor one mask at runtime instead of duplicating colored images.
- Keep `editor_project.json` synchronized with visible renderer changes so the Android app can show the before/after preview.
- Build with `make -j2` and run `make test` before finishing.
- Preserve `-std=c99 -Wall -Wextra -Werror` compatibility and do not add a full framebuffer to production code.
