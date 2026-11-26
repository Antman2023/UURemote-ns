#include "diagnostics.hpp"
#include "switch_compat/poll.hpp"
#include <arpa/inet.h>
#include <cstddef>
#include <cstdio>
#include <mbedtls/platform_time.h>
#include <netdb.h>
#include <poll.h>
#include <switch.h>
#include <sys/socket.h>

extern "C" int __real_poll(struct pollfd *, nfds_t, int);
extern "C" int __real_getnameinfo(const sockaddr *, socklen_t, char *, socklen_t, char *, socklen_t, int);

extern "C" void uu_log_udp_receive_buffer(int socket) {
    int size = 0;
    socklen_t length = sizeof(size);
    if (getsockopt(socket, SOL_SOCKET, SO_RCVBUF, &size, &length) != 0 || length != sizeof(size)) {
        uu::diagnostic("ice-udp-rx-buffer-unavailable");
        return;
    }
    char event[64];
    std::snprintf(event, sizeof(event), "ice-udp-rx-buffer-%d", size);
    uu::diagnostic(event);
}

extern "C" int __wrap_poll(struct pollfd *sockets, nfds_t count, int timeout) {
    // libnx forwards negative entries and input revents to the service. Some
    // service implementations reject the whole batch when a slot is ignored.
    return uu::pollActiveSockets(
        sockets, count, timeout,
        [](struct pollfd *active, std::size_t size, int timeout) {
            return __real_poll(active, static_cast<nfds_t>(size), timeout);
        },
        [](int timeout) {
            if (timeout > 0)
                svcSleepThread(int64_t(timeout) * 1000000);
            while (timeout < 0)
                svcSleepThread(3600LL * 1000000000);
        });
}

extern "C" int __wrap_getnameinfo(const sockaddr *address, socklen_t size, char *host, socklen_t hostSize,
                                  char *service, socklen_t serviceSize, int flags) {
    // ICE only needs numeric addresses and ports. Format them locally so this
    // path also works when the DNS service omits GetNameInfoRequest.
    constexpr int numericFlags = NI_NUMERICHOST | NI_NUMERICSERV | NI_DGRAM | NI_NUMERICSCOPE;
    if ((flags & ~numericFlags) || (host && !(flags & NI_NUMERICHOST)) ||
        (service && !(flags & NI_NUMERICSERV)))
        return __real_getnameinfo(address, size, host, hostSize, service, serviceSize, flags);
    if (!address || size < offsetof(sockaddr, sa_data))
        return EAI_FAIL;

    const void *ip;
    unsigned port;
    unsigned scope = 0;
    switch (address->sa_family) {
    case AF_INET: {
        if (size < sizeof(sockaddr_in))
            return EAI_FAIL;
        const auto &ipv4 = *reinterpret_cast<const sockaddr_in *>(address);
        ip = &ipv4.sin_addr;
        port = ntohs(ipv4.sin_port);
        break;
    }
    case AF_INET6: {
        if (size < sizeof(sockaddr_in6))
            return EAI_FAIL;
        const auto &ipv6 = *reinterpret_cast<const sockaddr_in6 *>(address);
        ip = &ipv6.sin6_addr;
        port = ntohs(ipv6.sin6_port);
        scope = ipv6.sin6_scope_id;
        break;
    }
    default:
        return EAI_FAMILY;
    }
    if (host) {
        char numericHost[INET6_ADDRSTRLEN];
        if (!inet_ntop(address->sa_family, ip, numericHost, sizeof(numericHost)))
            return EAI_FAIL;
        const int length = scope ? std::snprintf(host, hostSize, "%s%%%u", numericHost, scope)
                                 : std::snprintf(host, hostSize, "%s", numericHost);
        if (length < 0 || static_cast<socklen_t>(length) >= hostSize)
            return EAI_OVERFLOW;
    }
    if (service) {
        const int length = std::snprintf(service, serviceSize, "%u", port);
        if (length < 0 || static_cast<socklen_t>(length) >= serviceSize)
            return EAI_OVERFLOW;
    }
    return 0;
}

extern "C" int mbedtls_hardware_poll(void *, unsigned char *output, size_t len, size_t *olen) {
    randomGet(output, len);
    *olen = len;
    return 0;
}
extern "C" mbedtls_ms_time_t mbedtls_ms_time(void) {
    return static_cast<mbedtls_ms_time_t>(armTicksToNs(armGetSystemTick()) / 1000000);
}
