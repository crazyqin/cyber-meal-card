#!/usr/bin/env bash
set -euo pipefail

mode="${1:---all}"
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    echo "Usage: $0 [--all|--static|--firmware]" >&2
}

run_static_checks() {
    local actionlint_bin
    local test_dir
    local -a demo_link_sources=()
    case "$(uname -s)" in
        MINGW*|MSYS*) demo_link_sources=(tests/demo_stubs/ui_unreachable.c) ;;
    esac

    python3 tools/check_repo.py

    actionlint_bin="${ACTIONLINT_BIN:-}"
    if [[ -z "${actionlint_bin}" ]]; then
        actionlint_bin="$(command -v actionlint || true)"
    fi
    if [[ -z "${actionlint_bin}" || ! -x "${actionlint_bin}" ]]; then
        actionlint_bin="$(./tools/install-actionlint.sh)"
    fi
    "${actionlint_bin}" -color .github/workflows/*.yml

    test_dir="$(mktemp -d /tmp/ai-passport-host-tests.XXXXXX)"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_ui_pixel_math.c main/ui_pixel_math.c \
        -o "${test_dir}/test_ui_pixel_math"
    "${test_dir}/test_ui_pixel_math"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_demo_navigation.c main/demo_navigation.c \
        -o "${test_dir}/test_demo_navigation"
    "${test_dir}/test_demo_navigation"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_key_guard.c -o "${test_dir}/test_key_guard"
    "${test_dir}/test_key_guard"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Icomponents/bsp/src \
        tests/test_bsp_display_rounding.c components/bsp/src/bsp_display_rounding.c \
        -o "${test_dir}/test_bsp_display_rounding"
    "${test_dir}/test_bsp_display_rounding"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Icomponents/bsp/src \
        tests/test_bsp_es8311_sleep_check.c components/bsp/src/bsp_es8311_sleep_check.c \
        -o "${test_dir}/test_bsp_es8311_sleep_check"
    "${test_dir}/test_bsp_es8311_sleep_check"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_button.c -o "${test_dir}/test_bsp_button"
    "${test_dir}/test_bsp_button"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_lvgl_init.c components/bsp/src/bsp_display_rounding.c \
        -o "${test_dir}/test_bsp_lvgl_init"
    "${test_dir}/test_bsp_lvgl_init"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/audio_stubs -Icomponents/bsp/include -Icomponents/bsp/src \
        tests/test_bsp_audio_recovery.c components/bsp/src/bsp_es8311_sleep_check.c \
        -o "${test_dir}/test_bsp_audio_recovery"
    "${test_dir}/test_bsp_audio_recovery"
    for demo in audio low_power ble wifi; do
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
            -ffunction-sections -fdata-sections -Itests/demo_stubs -Imain \
            "tests/test_demo_${demo}_runtime.c" "${demo_link_sources[@]}" -Wl,--gc-sections \
            -o "${test_dir}/test_demo_${demo}_runtime"
        "${test_dir}/test_demo_${demo}_runtime"
    done
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_deep_sleep_contract.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_key_wake_contract.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_check_repo.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_verify_firmware.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_archive_firmware.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_install_passport_skills.py
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_quota.c main/quota_model.c -o "${test_dir}/test_quota"
    "${test_dir}/test_quota"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_quota_presentation.c main/quota_presentation.c main/quota_model.c \
        -o "${test_dir}/test_quota_presentation"
    "${test_dir}/test_quota_presentation"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_quota_settings.c main/quota_settings.c -o "${test_dir}/test_quota_settings"
    "${test_dir}/test_quota_settings"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_quota_setup_access.c main/quota_setup_access.c \
        -o "${test_dir}/test_quota_setup_access"
    "${test_dir}/test_quota_setup_access"
    if [[ -f managed_components/lvgl__lvgl/src/stdlib/builtin/lv_sprintf_builtin.c ]]; then
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -DLV_CONF_SKIP -DLV_USE_FLOAT=0 \
            -Imain -Imanaged_components/lvgl__lvgl tests/test_quota_lvgl_format.c \
            main/quota_model.c managed_components/lvgl__lvgl/src/stdlib/builtin/lv_sprintf_builtin.c \
            -o "${test_dir}/test_quota_lvgl_format"
        "${test_dir}/test_quota_lvgl_format"
    else
        echo "LVGL formatter regression: NOT RUN (run an ESP-IDF build to fetch managed dependencies)"
    fi
    local -a cjson_flags cjson_sources
    if [[ -n "${IDF_PATH:-}" && -f "${IDF_PATH}/components/json/cJSON/cJSON.c" ]]; then
        cjson_flags=(-I"${IDF_PATH}/components/json/cJSON")
        cjson_sources=("${IDF_PATH}/components/json/cJSON/cJSON.c")
    elif command -v pkg-config >/dev/null 2>&1 && pkg-config --exists libcjson; then
        read -r -a cjson_flags <<< "$(pkg-config --cflags libcjson)"
        read -r -a cjson_sources <<< "$(pkg-config --libs libcjson)"
    else
        echo "ERROR: core parser tests require ESP-IDF cJSON or libcjson-dev + pkg-config." >&2
        return 1
    fi
    (
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -DQUOTA_PARSE_TEST -Imain \
            "${cjson_flags[@]}" tests/test_quota.c \
            main/quota_model.c main/quota_parse.c main/quota_config.c \
            "${cjson_sources[@]}" -lm \
            -o "${test_dir}/test_quota_parse"
        "${test_dir}/test_quota_parse"
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
            "${cjson_flags[@]}" tests/test_quota_cos.c \
            main/quota_model.c main/quota_parse.c main/quota_config.c main/quota_cos.c \
            "${cjson_sources[@]}" -lm \
            -o "${test_dir}/test_quota_cos"
        "${test_dir}/test_quota_cos"
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
            "${cjson_flags[@]}" tests/test_tokenhub.c \
            main/quota_model.c main/quota_parse.c main/quota_config.c main/quota_cos.c \
            "${cjson_sources[@]}" -lm \
            -o "${test_dir}/test_tokenhub"
        "${test_dir}/test_tokenhub"
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
            "${cjson_flags[@]}" tests/test_quota_discovery.c \
            main/quota_model.c main/quota_parse.c main/quota_config.c main/quota_discovery.c \
            "${cjson_sources[@]}" -lm \
            -o "${test_dir}/test_quota_discovery"
        "${test_dir}/test_quota_discovery"
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
            "${cjson_flags[@]}" tests/test_collection.c \
            main/quota_model.c main/quota_parse.c main/quota_config.c main/quota_discovery.c main/quota_collection.c \
            "${cjson_sources[@]}" -lm \
            -o "${test_dir}/test_collection"
        "${test_dir}/test_collection"
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain "${cjson_flags[@]}" \
            tests/quota_contract_probe.c main/quota_model.c main/quota_parse.c main/quota_config.c \
            main/quota_discovery.c main/quota_collection.c "${cjson_sources[@]}" -lm -o "${test_dir}/quota_contract_probe"
        PYTHONDONTWRITEBYTECODE=1 python3 tests/test_collector_contract.py "${test_dir}/quota_contract_probe"
    )
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_quota_tools.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_collectors.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_collector_supervisor.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_linux_deployment.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_tokenhub_sandbox.py
    bash -n collector/linux/install.sh
    bash -n collector/linux/collector.sh
    bash -n collector/linux/runtimes.sh
    if command -v node >/dev/null 2>&1; then
        node tests/test_backup.cjs
    else
        echo "Offline backup QR: NOT RUN (Node.js required)"
    fi
    rm -rf "${test_dir}"
    echo "Host tests: PASS"
}

run_firmware_checks() (
    local validation_build_dir

    if ! command -v idf.py >/dev/null 2>&1; then
        echo "ERROR: idf.py is not available; activate ESP-IDF 5.5.3 first." >&2
        return 1
    fi

    validation_build_dir="$(mktemp -d /tmp/ai-passport-firmware.XXXXXX)"
    trap 'case "${validation_build_dir}" in /tmp/ai-passport-firmware.*) rm -rf -- "${validation_build_dir}" ;; esac' EXIT

    SDKCONFIG_DEFAULTS="${repo_root}/sdkconfig.defaults" \
        idf.py -B "${validation_build_dir}" \
        -D "SDKCONFIG=${validation_build_dir}/sdkconfig" build
    idf.py -B "${validation_build_dir}" merge-bin \
        -o "${validation_build_dir}/FoloToy-AI-Passport-full.bin"
    python3 tools/verify_firmware.py "${validation_build_dir}"
    PYTHONDONTWRITEBYTECODE=1 python3 tools/archive_firmware.py create \
        "${validation_build_dir}" --archive-root "${repo_root}/build/firmware"
    mkdir -p "${repo_root}/build"
    install -m 0644 \
        "${validation_build_dir}/FoloToy-AI-Passport-full.bin" \
        "${repo_root}/build/FoloToy-AI-Passport-full.bin"
    echo "Firmware build: PASS"
)

cd "${repo_root}"
case "${mode}" in
    --all)
        run_static_checks
        run_firmware_checks
        ;;
    --static)
        run_static_checks
        ;;
    --firmware)
        run_firmware_checks
        ;;
    *)
        usage
        exit 2
        ;;
esac
