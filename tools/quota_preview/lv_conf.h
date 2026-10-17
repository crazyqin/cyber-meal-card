#ifndef LV_CONF_H
#define LV_CONF_H
/* Same color, fonts and formatter as the device. No OS port. */
#define LV_COLOR_DEPTH 16
#define LV_USE_OS LV_OS_NONE
#define LV_USE_FLOAT 0
/* 64-bit object/style pointers cost more than the ESP32's 32-bit pointers.
 * Use a 32-bit host build to check the actual device's 24 KiB allocator budget. */
#if defined(_WIN64) || defined(__LP64__)
#define LV_MEM_SIZE (40 * 1024U)
#else
#define LV_MEM_SIZE (24 * 1024U)
#endif
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_BUILTIN
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_USE_FONT_PLACEHOLDER 1
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1
#define LV_ASSERT_HANDLER_INCLUDE <stdlib.h>
#define LV_ASSERT_HANDLER exit(2);
#define LV_USE_THORVG_INTERNAL 0
#define LV_BUILD_EXAMPLES 0
#endif
