set(VINPUT_CONFIG_SOURCES
    config/core_config_defaults.cpp
    config/core_config_store.cpp
    config/core_config_normalize.cpp
    config/core_config_validate.cpp
    config/core_config_resolve.cpp
    config/core_config_json.cpp
    config/config_router.cpp
    scene/postprocess_scene.cpp
    i18n.cpp
    utils/path_utils.cpp
    utils/sandbox.cpp
    utils/file_utils.cpp
)

if(NOT WIN32)
    list(APPEND VINPUT_CONFIG_SOURCES
        config/vinput_config.cpp
    )
endif()

add_library(vinput-config STATIC
    ${VINPUT_CONFIG_SOURCES}
)

add_library(vinput-common STATIC
    dbus/error_info.cpp
    ${VINPUT_CONFIG_SOURCES}
    asr/recognition_result.cpp
    asr/model_manager.cpp
    registry/registry_fetch.cpp
    registry/registry_models.cpp
    registry/registry_cache.cpp
    registry/registry_i18n.cpp
    registry/registry_scripts.cpp
    llm/adapter_manager.cpp
    utils/download_progress.cpp
    utils/downloader.cpp
    utils/process_utils.cpp
)

set_target_properties(vinput-common PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

set_target_properties(vinput-config PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

target_include_directories(vinput-common PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_include_directories(vinput-config PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(vinput-common PUBLIC
    nlohmann_json::nlohmann_json
    CURL::libcurl
    LibArchive::LibArchive
    OpenSSL::Crypto
)

target_link_libraries(vinput-config PUBLIC
    nlohmann_json::nlohmann_json
)

if(NOT WIN32)
    target_link_libraries(vinput-common PUBLIC
        Fcitx5::Config
        ${GETTEXT_LIBRARIES}
    )

    target_link_libraries(vinput-config PUBLIC
        Fcitx5::Config
        ${GETTEXT_LIBRARIES}
    )
endif()
