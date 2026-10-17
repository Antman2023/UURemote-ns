#include <switch.h>

// libcurl's CONNECT_ONLY API promises nonblocking I/O. Enforce that contract at
// the public libnx boundary, after the TLS handshake, without relying on curl's
// private backend layout. Only linked on Switch (and with a fake service in tests).
extern "C" Result __real_sslConnectionRead(SslConnection *, void *, u32, u32 *);
extern "C" Result __real_sslConnectionWrite(SslConnection *, const void *, u32, u32 *);

extern "C" Result __wrap_sslConnectionRead(SslConnection *connection, void *buffer, u32 size, u32 *read) {
    *read = 0;
    const Result rc = sslConnectionSetIoMode(connection, SslIoMode_NonBlocking);
    if (R_FAILED(rc))
        return rc;
    return __real_sslConnectionRead(connection, buffer, size, read);
}
extern "C" Result __wrap_sslConnectionWrite(SslConnection *connection, const void *buffer, u32 size,
                                            u32 *written) {
    *written = 0;
    const Result rc = sslConnectionSetIoMode(connection, SslIoMode_NonBlocking);
    if (R_FAILED(rc))
        return rc;
    return __real_sslConnectionWrite(connection, buffer, size, written);
}
