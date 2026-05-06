cmake_minimum_required(VERSION 3.16)

find_library(SHERPA_ONNX_CXX_LIB sherpa-onnx-cxx-api REQUIRED)
find_library(SHERPA_ONNX_C_LIB sherpa-onnx-c-api REQUIRED)
find_library(ONNXRUNTIME_LIB onnxruntime REQUIRED)
find_path(SHERPA_ONNX_INCLUDE_DIR sherpa-onnx/c-api/c-api.h REQUIRED)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(VINPUT_WINDOWS_SOURCES
    app/main.cpp
    app/mainwindow.cpp
    app/windows_control_page.cpp
    runtime/audio_recorder.cpp
    runtime/doubao_asr_client.cpp
    runtime/global_hotkey.cpp
    runtime/status_hud.cpp
    runtime/text_corrector.cpp
    runtime/text_injector.cpp
    runtime/voice_input_controller.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/pages/resources/resource_page.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/pages/hotwords/hotword_page.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/config_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/download_worker.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/gui_helpers.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/i18n_cache.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/utils/runtime_bridge.cpp
    ${CMAKE_SOURCE_DIR}/src/daemon/asr/runtime/backend_factory.cpp
    ${CMAKE_SOURCE_DIR}/src/daemon/asr/runtime/recognition_session_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/daemon/asr/backends/sherpa_offline_backend.cpp
    ${CMAKE_SOURCE_DIR}/src/daemon/asr/backends/sherpa_streaming_backend.cpp
    ${CMAKE_SOURCE_DIR}/src/daemon/asr/vad_trimmer.cpp
    ${CMAKE_SOURCE_DIR}/src/daemon/runtime/recognition_pipeline.cpp
    ${CMAKE_SOURCE_DIR}/src/daemon/postprocess/post_processor.cpp
)

set(VINPUT_WINDOWS_HEADERS
    app/mainwindow.h
    app/windows_control_page.h
    runtime/audio_recorder.h
    runtime/doubao_asr_client.h
    runtime/global_hotkey.h
    runtime/status_hud.h
    runtime/text_corrector.h
    runtime/text_injector.h
    runtime/voice_input_controller.h
)

add_executable(vinput-win WIN32
    ${VINPUT_WINDOWS_SOURCES}
    ${VINPUT_WINDOWS_HEADERS}
)

add_executable(vinput-doubao-smoke
    app/doubao_smoke_test.cpp
    runtime/doubao_asr_client.cpp
    runtime/doubao_asr_client.h
    runtime/text_corrector.cpp
    runtime/text_corrector.h
)

target_compile_definitions(vinput-win PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    VINPUT_VAD_MODEL_PATH="${CMAKE_SOURCE_DIR}/data/vad/silero_vad.onnx"
)

target_compile_definitions(vinput-doubao-smoke PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

if(MSVC)
    target_compile_options(vinput-win PRIVATE /utf-8)
    target_compile_options(vinput-doubao-smoke PRIVATE /utf-8)
endif()

target_include_directories(vinput-win PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/gui
    ${SHERPA_ONNX_INCLUDE_DIR}
)

target_include_directories(vinput-doubao-smoke PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(vinput-win PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Multimedia
    Qt6::Concurrent
    vinput-common
    ${SHERPA_ONNX_CXX_LIB}
    ${SHERPA_ONNX_C_LIB}
    ${ONNXRUNTIME_LIB}
    user32
    winhttp
)

target_link_libraries(vinput-doubao-smoke PRIVATE
    Qt6::Core
    nlohmann_json::nlohmann_json
    winhttp
)

install(TARGETS vinput-win
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
