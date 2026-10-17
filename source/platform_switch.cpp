#include <cstddef>
#include <mbedtls/platform_time.h>
#include <switch.h>
extern "C" int mbedtls_hardware_poll(void *, unsigned char *output, size_t len, size_t *olen) {
    randomGet(output, len);
    *olen = len;
    return 0;
}
extern "C" mbedtls_ms_time_t mbedtls_ms_time(void) {
    return static_cast<mbedtls_ms_time_t>(armTicksToNs(armGetSystemTick()) / 1000000);
}
