if(NOT LO_TARGET_PLATFORM STREQUAL "ps5" OR NOT LO_TARGET_ISA STREQUAL "x86_64")
    message(FATAL_ERROR "The PS5 probe requires cmake/toolchains/ps5.cmake and the public SDK")
endif()
set(PS5_VULKAN_DIR "$ENV{PS5_VULKAN_DIR}" CACHE PATH "Pinned PS5_Vulkan checkout")
set(PS5_RADV_ROOT "${PS5_VULKAN_DIR}/.deps/native/radv-release")
if(NOT EXISTS "${PS5_RADV_ROOT}/include/vulkan/vulkan.h")
    message(FATAL_ERROR "Build pinned RADV first with tools/ps5_probe/bootstrap.sh")
endif()
add_library(LoPS5ProbeObjects OBJECT main.cpp memory.cpp vulkan.cpp)
target_include_directories(LoPS5ProbeObjects PRIVATE "${PS5_RADV_ROOT}/include")
target_compile_features(LoPS5ProbeObjects PRIVATE cxx_std_20)
target_compile_options(LoPS5ProbeObjects PRIVATE -Wall -Wextra -Wpedantic
    -Wno-missing-field-initializers -fno-omit-frame-pointer -ffunction-sections -fdata-sections)
set(probe_out "${CMAKE_BINARY_DIR}/ps5-probe")
add_custom_command(OUTPUT "${probe_out}/eboot.elf"
    COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/link.sh" "${probe_out}"
        "${PS5_VULKAN_DIR}" "${PS5_PAYLOAD_SDK}" $<TARGET_OBJECTS:LoPS5ProbeObjects>
    DEPENDS LoPS5ProbeObjects $<TARGET_OBJECTS:LoPS5ProbeObjects>
        link.sh dependencies.env param.json
        "${PS5_RADV_ROOT}/lib/libvulkan_radeon.ps5.a"
    COMMAND_EXPAND_LISTS VERBATIM)
add_custom_target(LoPS5Probe ALL DEPENDS "${probe_out}/eboot.elf")
