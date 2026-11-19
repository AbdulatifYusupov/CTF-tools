cmake_minimum_required(VERSION 3.21)
project(StegoTool VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTOMOC ON)

# ---- Dependencies -----------------------------------------------------------
# ML-KEM appeared in OpenSSL 3.5. Ubuntu 24.04 ships 3.0, so you will likely need
# a custom build: cmake -DOPENSSL_ROOT_DIR=/opt/openssl-3.5 ...
find_package(OpenSSL 3.5 REQUIRED)
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)
find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Concurrent)

find_package(SndFile CONFIG QUIET)
if(TARGET SndFile::sndfile)
    set(SNDFILE_TARGET SndFile::sndfile)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SNDFILE REQUIRED IMPORTED_TARGET sndfile)
    set(SNDFILE_TARGET PkgConfig::SNDFILE)
endif()

# ---- Core library (no Qt dependency, so it is easy to test) -------------------
add_library(stego_core STATIC
    core/crypto/Crypto.cpp
    core/stego/Pipeline.cpp
    core/stego/Prng.cpp
    core/stego/SlotCodec.cpp
    core/stego/AdaptiveLsb.cpp
    core/stego/AudioIO.cpp
    core/stego/AudioLsb.cpp
    core/stego/BlockDct.cpp
    core/ecc/ReedSolomon.cpp
    core/analysis/Metrics.cpp
)
target_include_directories(stego_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core)
target_link_libraries(stego_core PUBLIC
    OpenSSL::Crypto
    ${OpenCV_LIBS}
    ${SNDFILE_TARGET}
)
target_include_directories(stego_core PUBLIC ${OpenCV_INCLUDE_DIRS})
target_compile_options(stego_core PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# ---- Worker (Qt Core + Concurrent, no widgets: testable headless) ------------
add_library(stego_worker STATIC ui/StegoWorker.cpp ui/StegoWorker.hpp)
target_include_directories(stego_worker PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/ui)
target_link_libraries(stego_worker PUBLIC stego_core Qt6::Core Qt6::Concurrent)

# ---- GUI --------------------------------------------------------------------
add_executable(stego_gui
    ui/main.cpp
    # ui/MainWindow.cpp
    # ui/CompareWidget.cpp
    # ui/StegoWorker.cpp
)
target_link_libraries(stego_gui PRIVATE stego_worker Qt6::Widgets)

# ---- Tests ------------------------------------------------------------------
enable_testing()
add_executable(crypto_roundtrip tests/crypto_roundtrip.cpp)
target_link_libraries(crypto_roundtrip PRIVATE stego_core)
add_test(NAME crypto_roundtrip COMMAND crypto_roundtrip)

add_executable(stego_worker_test tests/stego_worker_test.cpp)
target_link_libraries(stego_worker_test PRIVATE stego_worker)
add_test(NAME stego_worker_test COMMAND stego_worker_test)

add_executable(audio_lsb_roundtrip tests/audio_lsb_roundtrip.cpp)
target_link_libraries(audio_lsb_roundtrip PRIVATE stego_core)
add_test(NAME audio_lsb_roundtrip COMMAND audio_lsb_roundtrip)

add_executable(reed_solomon_test tests/reed_solomon_test.cpp)
target_link_libraries(reed_solomon_test PRIVATE stego_core)
add_test(NAME reed_solomon_test COMMAND reed_solomon_test)

add_executable(block_dct_roundtrip tests/block_dct_roundtrip.cpp)
target_link_libraries(block_dct_roundtrip PRIVATE stego_core)
add_test(NAME block_dct_roundtrip COMMAND block_dct_roundtrip)

add_executable(metrics_test tests/metrics_test.cpp)
target_link_libraries(metrics_test PRIVATE stego_core)
add_test(NAME metrics_test COMMAND metrics_test)

add_executable(adaptive_lsb_roundtrip tests/adaptive_lsb_roundtrip.cpp)
target_link_libraries(adaptive_lsb_roundtrip PRIVATE stego_core)
add_test(NAME adaptive_lsb_roundtrip COMMAND adaptive_lsb_roundtrip)
