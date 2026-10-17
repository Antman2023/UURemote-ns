#include <cstddef>
#include <switch.h>
extern "C" int mbedtls_hardware_poll(void *, unsigned char *output, size_t len, size_t *olen) {
    randomGet(output, len);
    *olen = len;
    return 0;
}
extern "C" long long mbedtls_ms_time(void) {
    return static_cast<long long>(armTicksToNs(armGetSystemTick()) / 1000000);
}
