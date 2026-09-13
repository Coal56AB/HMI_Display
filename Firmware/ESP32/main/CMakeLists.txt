# Shared LCD/touch platform; applications supply their own services and dependencies.
set(hmi_sources main.cpp hmi_board.cpp)
set(hmi_includes . ../../Api)
set(hmi_requires driver esp_timer)
set(hmi_defines "")
# A branch-specific extension owns its GUI and application, not this platform.
set(hmi_extension "${CMAKE_CURRENT_LIST_DIR}/../../../DisplaySrc/esp32/CMakeLists.txt")
if(EXISTS "${hmi_extension}" AND NOT HMI_TEMPLATE)
    include("${hmi_extension}")
else()
    list(APPEND hmi_sources template_demo.c)
endif()
idf_component_register(SRCS ${hmi_sources} INCLUDE_DIRS ${hmi_includes}
                       REQUIRES ${hmi_requires})
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_definitions(${COMPONENT_LIB} PRIVATE ${hmi_defines})
