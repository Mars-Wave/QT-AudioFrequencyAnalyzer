cmake_minimum_required(VERSION 3.16)

project(AudioAnalyzerApp VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Multimedia)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(AudioAnalyzerApp
    main.cpp
)

qt_add_qml_module(AudioAnalyzerApp
    URI AudioAnalyzer
    VERSION 1.0
    QML_FILES
        Main.qml
        InputSelection.qml
        Visualizer.qml
        AudioToggles.qml
    SOURCES
        src/AnalyzerInterface.h
        src/AnalyzerInterface.cpp
        src/AudioEngine.h
        src/AudioEngine.cpp
        src/inputs/MicrophoneInput.h
        src/inputs/MicrophoneInput.cpp
        src/processing/ThreadedAudioFrequencyAnalyzer.h
        src/processing/ThreadedAudioFrequencyAnalyzer.cpp
        src/processing/AudioFFTworker.h
        src/processing/AudioFFTworker.cpp
        src/processing/AudioRMSworker.h
        src/processing/AudioRMSworker.cpp
        src/processing/ArithmeticCircularQueue.h
        src/pch.h
        src/inputs/FileInput.h
        src/inputs/IpAudioInput.h
)

target_precompile_headers(AudioAnalyzerApp PRIVATE src/pch.h)

target_compile_options(AudioAnalyzerApp PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/FI${CMAKE_CURRENT_SOURCE_DIR}/src/FastQStrings.h>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-include${CMAKE_CURRENT_SOURCE_DIR}/src/FastQStrings.h>
    $<$<CXX_COMPILER_ID:MSVC>:/FI${CMAKE_CURRENT_SOURCE_DIR}/src/Constexprs.h>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-include${CMAKE_CURRENT_SOURCE_DIR}/src/Constexprs.h>
    $<$<CXX_COMPILER_ID:MSVC>:/FI${CMAKE_CURRENT_SOURCE_DIR}/src/debugOverride.h>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-include${CMAKE_CURRENT_SOURCE_DIR}/src/debugOverride.h>
)

target_link_libraries(AudioAnalyzerApp PRIVATE Qt6::Quick Qt6::Multimedia)
