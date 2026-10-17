idf_component_register(
    SRCS "quota_app.c" "quota_ui.c" "quota_presentation.c" "quota_model.c" "quota_parse.c" "quota_config.c" "quota_cos.c" "quota_discovery.c" "quota_collection.c" "quota_setup_access.c" "quota_net.c"
         "../assets/fonts/quota/quota_font_14.c" "../assets/fonts/quota/quota_font_16.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_event esp_hw_support esp_netif esp_timer esp_wifi nvs_flash
             esp_http_client esp_http_server mbedtls json lwip
    EMBED_TXTFILES "setup.html"
    # JavaScript must not carry EMBED_TXTFILES' appended NUL byte.
    EMBED_FILES "backup.js" "../assets/qr/qrcodegen-v1.8.0-es6.js"
)
set(quota_local_config "${PROJECT_DIR}/collector/config.local.json")
set(QUOTA_PRIVATE_KEY_FILE "${PROJECT_DIR}/../key.txt" CACHE FILEPATH "Optional local Wi-Fi defaults file")
if(EXISTS "${quota_local_config}")
    idf_build_get_property(quota_python PYTHON)
    set(quota_wifi_args)
    if(EXISTS "${QUOTA_PRIVATE_KEY_FILE}")
        list(APPEND quota_wifi_args --key-file "${QUOTA_PRIVATE_KEY_FILE}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${QUOTA_PRIVATE_KEY_FILE}")
    endif()
    execute_process(COMMAND "${quota_python}" "${PROJECT_DIR}/tools/prepare_private_cos.py"
        --config "${quota_local_config}" ${quota_wifi_args} RESULT_VARIABLE quota_defaults_result)
    if(NOT quota_defaults_result EQUAL 0)
        message(FATAL_ERROR "Local Wi-Fi/COS configuration could not be embedded (values hidden)")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${quota_local_config}" "${PROJECT_DIR}/collector/secrets.local.json"
        "${PROJECT_DIR}/tools/prepare_private_cos.py")
endif()
if(EXISTS "${PROJECT_DIR}/build/private/quota_private_cos.h")
    target_include_directories(${COMPONENT_LIB} PRIVATE "${PROJECT_DIR}/build/private")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE QUOTA_PRIVATE_COS_DEFAULTS=1)
    message(STATUS "Private device defaults enabled; this firmware contains user credentials")
endif()
