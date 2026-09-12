# Nofrendo NES core as carried in retro-go (GPL-2.0 / LGPL-2 headers, see COPYING).
# Trimmed to the four mappers the ROM set uses; no retro-go glue.
file(GLOB NOFRENDO_SRCS nes/*.c mappers/*.c)
idf_component_register(
    SRCS ${NOFRENDO_SRCS}
    INCLUDE_DIRS "."
)
target_compile_options(${COMPONENT_LIB} PRIVATE -O3 -Wno-array-bounds -Wno-format -Wno-unused-function -Wno-unused-variable -Wno-implicit-fallthrough)
