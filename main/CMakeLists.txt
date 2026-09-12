idf_component_register(
    SRCS "main.c" "ui.c" "saves.c" "medal.c"
    INCLUDE_DIRS "."
    REQUIRES display audio_hal ble_pad nofrendo driver esp_timer nvs_flash esp_partition esp_adc
)

if(DEFINED DEMO_SECONDS)   # bench: idf.py -DDEMO_SECONDS=20 build
    target_compile_definitions(${COMPONENT_LIB} PRIVATE DEMO_SECONDS=${DEMO_SECONDS})
endif()

# Pack ROMS/ into the 'roms' partition image and flash it with `idf.py flash`.
# CONFIGURE_DEPENDS: dropping a new ROM into the folder is picked up by the next build.
set(ROMS_DIR "${CMAKE_SOURCE_DIR}/ROMS")
file(GLOB ROM_FILES CONFIGURE_DEPENDS "${ROMS_DIR}/*.zip" "${ROMS_DIR}/*.nes" "${ROMS_DIR}/art/*.png")
set(ROMS_BIN "${CMAKE_BINARY_DIR}/roms.bin")
idf_build_get_property(python PYTHON)
add_custom_command(OUTPUT "${ROMS_BIN}"
    COMMAND ${python} "${CMAKE_SOURCE_DIR}/tools/pack_roms.py" "${ROMS_DIR}" "${ROMS_BIN}"
    DEPENDS ${ROM_FILES} "${CMAKE_SOURCE_DIR}/tools/pack_roms.py" "${CMAKE_SOURCE_DIR}/tools/artconv.py"
    COMMENT "Packing ROMs from ${ROMS_DIR}")
add_custom_target(roms_bin ALL DEPENDS "${ROMS_BIN}")
esptool_py_flash_to_partition(flash "roms" "${ROMS_BIN}")
add_dependencies(flash roms_bin)
