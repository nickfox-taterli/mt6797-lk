cmake_minimum_required(VERSION 3.20)
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/toolchains/arm32.cmake")
project(x20_lk LANGUAGES C ASM)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "Build under out/")
endif()
set(ARM_FLAGS -mcpu=cortex-a53 -marm -mfloat-abi=soft -ffreestanding
    -fno-builtin -fno-common -fno-stack-protector -fno-pic
    -ffunction-sections -fdata-sections -fno-strict-aliasing -Os)
add_subdirectory(lib)
target_compile_options(bootcore PRIVATE ${ARM_FLAGS})
target_include_directories(bootcore BEFORE PRIVATE include/freestanding)
set(RUNTIME arch/arm/start.S arch/arm/cache.S arch/arm/mmu.c kernel/main.c
    platform/mt6797/platform.c platform/mt6797/emmc.c
    app/x20_boot/boot.c app/x20_boot/fdt.c app/x20_boot/rescue.c
    lib/libc/memcpy.c lib/libc/memmove.c lib/libc/memset.c
    lib/libc/memcmp.c lib/libc/memchr.c lib/libc/strlen.c
    lib/libc/strnlen.c lib/libc/strcmp.c lib/libc/strncmp.c
    lib/libc/strchr.c lib/libc/strrchr.c)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
foreach(name emmc tf rescue)
  if(name STREQUAL emmc)
    add_executable(${name} ${RUNTIME})
  else()
    add_executable(${name} EXCLUDE_FROM_ALL ${RUNTIME})
  endif()
  set(out "${CMAKE_BINARY_DIR}/${name}")
  set_target_properties(${name} PROPERTIES OUTPUT_NAME lk.elf RUNTIME_OUTPUT_DIRECTORY "${out}")
  target_compile_options(${name} PRIVATE ${ARM_FLAGS} -Wall -Wextra -Werror)
  target_include_directories(${name} BEFORE PRIVATE include/freestanding)
  target_compile_definitions(${name} PRIVATE X20_BUILD_ID="x20-lk/${name}")
  if(name STREQUAL tf)
    target_compile_definitions(${name} PRIVATE X20_ROOT_TF=1)
  elseif(name STREQUAL rescue)
    target_compile_definitions(${name} PRIVATE X20_RAM_RESCUE=1)
  endif()
  target_link_libraries(${name} PRIVATE bootcore gcc)
  target_link_options(${name} PRIVATE ${ARM_FLAGS} -nostdlib -static
      -Wl,--gc-sections,--build-id=none,-Map=${out}/lk.map
      -T${CMAKE_CURRENT_SOURCE_DIR}/arch/arm/x20.ld)
  set_property(TARGET ${name} APPEND PROPERTY LINK_DEPENDS
      ${CMAKE_CURRENT_SOURCE_DIR}/arch/arm/x20.ld
      ${CMAKE_CURRENT_SOURCE_DIR}/tools/mtk_header.py)
  add_custom_command(TARGET ${name} POST_BUILD
      COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${name}> ${out}/lk.raw
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/mtk_header.py
          ${out}/lk.raw ${out}/lk.bin --target ${name}
      COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${name}>
      VERBATIM)
endforeach()
