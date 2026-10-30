#include <stdint.h>

#if defined(_WIN32)
#define FFI_PLUGIN_EXPORT __declspec(dllexport)
#else
#define FFI_PLUGIN_EXPORT
#endif

FFI_PLUGIN_EXPORT int32_t artech_start(void);
FFI_PLUGIN_EXPORT void artech_stop(void);
FFI_PLUGIN_EXPORT int32_t artech_device_count(void);
FFI_PLUGIN_EXPORT int32_t artech_next_call(
    int32_t *line,
    char *phone_number,
    int32_t phone_number_capacity);
FFI_PLUGIN_EXPORT int32_t artech_last_error(void);
FFI_PLUGIN_EXPORT int32_t artech_last_system_error(void);
