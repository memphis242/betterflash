include(FetchContent)

# This header describes the ABI of the runtime downloaded by VoiceController.
FetchContent_Declare(betterflash_sherpa_api
    URL "https://raw.githubusercontent.com/k2-fsa/sherpa-onnx/v1.10.46/sherpa-onnx/c-api/c-api.h"
    URL_HASH SHA256=1d33430668f3ed2e1b6f86d79a7fc7eb7bcd16949f99003334f28a2705430d18
    DOWNLOAD_NO_EXTRACT TRUE
    DOWNLOAD_NAME c-api.h)
FetchContent_MakeAvailable(betterflash_sherpa_api)
add_library(betterflash_local_speech INTERFACE)
target_include_directories(betterflash_local_speech INTERFACE "${betterflash_sherpa_api_SOURCE_DIR}")
