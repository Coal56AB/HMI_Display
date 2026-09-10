# Music-box extension. Firmware/ESP32 is reusable unchanged in templates.
set(music_box_dir "${CMAKE_CURRENT_LIST_DIR}")
list(APPEND hmi_sources "${music_box_dir}/../src/music_box.c")
list(APPEND hmi_includes "${music_box_dir}" "${music_box_dir}/music" "${music_box_dir}/../src")
list(APPEND hmi_defines MUSIC_BOX_BAND=16 MUSIC_BOX_MIDI_INTERVAL_MS=50)
list(APPEND hmi_sources "${music_box_dir}/controller_link.cpp" "${music_box_dir}/controller_app.cpp")
list(APPEND hmi_sources "${music_box_dir}/esp_link.cpp")
list(APPEND hmi_sources "${music_box_dir}/song_store.cpp")
list(APPEND hmi_defines MUSIC_BOX_CONTROLLER=1 MUSIC_BOX_PIXEL_MIDI=1)
list(APPEND hmi_sources
    "${music_box_dir}/music/music.cpp"
    "${music_box_dir}/live_controller.cpp"
    "${music_box_dir}/midi_app.cpp"
    "${music_box_dir}/usb_midi_host.cpp")
list(APPEND hmi_requires usb)
