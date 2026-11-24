cmake_minimum_required(VERSION 3.16)
project(xorbrute VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Concurrent)
find_package(Qt6 OPTIONAL_COMPONENTS Widgets Test)

enable_testing()

# --------------------------------------------------------------------------
# Core Library
# --------------------------------------------------------------------------
set(CORE_SOURCES
    src/core/Candidate.h
    src/core/EncodingUtils.h
    src/core/EncodingUtils.cpp
    src/core/Scorer.h
    src/core/Scorer.cpp
    src/core/XorEngine.h
    src/core/XorEngine.cpp
)

add_library(xor_core STATIC ${CORE_SOURCES})
target_include_directories(xor_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/core)
target_link_libraries(xor_core PUBLIC
    Qt6::Core
    Qt6::Concurrent
)

# --------------------------------------------------------------------------
# CLI Target
# --------------------------------------------------------------------------
add_executable(xorbrute_cli
    src/cli/cli_main.cpp
)
target_link_libraries(xorbrute_cli PRIVATE
    xor_core
    Qt6::Core
)

# --------------------------------------------------------------------------
# GUI Target (if Qt6 Widgets found)
# --------------------------------------------------------------------------
if(Qt6Widgets_FOUND)
    set(GUI_SOURCES
        src/app/main.cpp
        src/gui/MainWindow.h
        src/gui/MainWindow.cpp
        src/gui/CandidateTableModel.h
        src/gui/CandidateTableModel.cpp
        src/gui/DropAreaTextEdit.h
        src/gui/DropAreaTextEdit.cpp
        src/gui/ResultViewerDialog.h
        src/gui/ResultViewerDialog.cpp
        src/gui/SettingsDialog.h
        src/gui/SettingsDialog.cpp
        src/gui/Theme.h
        src/gui/Theme.cpp
    )

    # Check if main.cpp exists before building GUI
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/app/main.cpp")
        add_executable(xorbrute ${GUI_SOURCES})
        target_include_directories(xorbrute PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src/core
            ${CMAKE_CURRENT_SOURCE_DIR}/src/gui
        )
        target_link_libraries(xorbrute PRIVATE
            xor_core
            Qt6::Core
            Qt6::Widgets
            Qt6::Concurrent
        )
    endif()
endif()

# --------------------------------------------------------------------------
# Tests Target (if Qt6 Test found)
# --------------------------------------------------------------------------
if(Qt6Test_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    add_subdirectory(tests)
endif()
