# Public ps5-payload-dev SDK + mihawk-99 platform layer; no proprietary SDK.
set(PS5_PAYLOAD_SDK "$ENV{PS5_PAYLOAD_SDK}" CACHE PATH "Public PS5 payload SDK installation")
if(NOT EXISTS "${PS5_PAYLOAD_SDK}/toolchain/prospero.cmake")
    message(FATAL_ERROR "Set PS5_PAYLOAD_SDK to the pinned SDK installed by tools/ps5_probe/bootstrap.sh")
endif()
include("${PS5_PAYLOAD_SDK}/toolchain/prospero.cmake")
# Native title linking uses the RADV recipe, not the SDK's payload CRT.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
