set(BOOTCORE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../app/x20_boot/bundle.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../app/x20_boot/gzip.c
    ${CMAKE_CURRENT_SOURCE_DIR}/miniz/miniz.c
    ${CMAKE_CURRENT_SOURCE_DIR}/libfdt/fdt.c
    ${CMAKE_CURRENT_SOURCE_DIR}/libfdt/fdt_check.c
    ${CMAKE_CURRENT_SOURCE_DIR}/libfdt/fdt_ro.c
    ${CMAKE_CURRENT_SOURCE_DIR}/libfdt/fdt_rw.c
    ${CMAKE_CURRENT_SOURCE_DIR}/libfdt/fdt_sw.c
    ${CMAKE_CURRENT_SOURCE_DIR}/libfdt/fdt_wip.c
    ${CMAKE_CURRENT_SOURCE_DIR}/libfdt/fdt_empty_tree.c
    ${CMAKE_CURRENT_SOURCE_DIR}/libfdt/fdt_strerror.c
    ${CMAKE_CURRENT_SOURCE_DIR}/libfdt/fdt_addresses.c)
add_library(bootcore STATIC ${BOOTCORE_SOURCES})
target_include_directories(bootcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/miniz/include ${CMAKE_CURRENT_SOURCE_DIR}/libfdt)
target_compile_definitions(bootcore PRIVATE MINIZ_NO_MALLOC NDEBUG)
target_compile_options(bootcore PRIVATE -ffunction-sections -fdata-sections -fno-strict-aliasing)
set(BOOTCORE_SOURCES ${BOOTCORE_SOURCES} PARENT_SCOPE)
