#pragma once
// Include after SCTP and native socket types, in both the C stack and C++ adapter.
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
#define UU_SCTP_LAYOUT_ASSERT static_assert
#else
#define UU_SCTP_LAYOUT_ASSERT _Static_assert
#endif

#ifndef HAVE_SCONN_LEN
#error Horizon SCTP requires HAVE_SCONN_LEN for all consumers
#endif
UU_SCTP_LAYOUT_ASSERT(offsetof(struct sockaddr_conn, sconn_len) == offsetof(struct sockaddr, sa_len),
                      "SCTP address length must share the native socket offset");
UU_SCTP_LAYOUT_ASSERT(offsetof(struct sockaddr_conn, sconn_family) == offsetof(struct sockaddr, sa_family),
                      "SCTP address family must share the native socket offset");
UU_SCTP_LAYOUT_ASSERT(sizeof(((struct sockaddr_conn *)0)->sconn_family) ==
                          sizeof(((struct sockaddr *)0)->sa_family),
                      "SCTP and native address families must have equal width");
UU_SCTP_LAYOUT_ASSERT(sizeof(((struct sockaddr_conn *)0)->sconn_len) == sizeof(uint8_t),
                      "Horizon address length is one byte");
UU_SCTP_LAYOUT_ASSERT(sizeof(struct sockaddr_conn) <= sizeof(union sctp_sockstore),
                      "SCTP address must fit its destination union");
UU_SCTP_LAYOUT_ASSERT(sizeof(struct sockaddr_conn) <= UINT8_MAX,
                      "SCTP address size must fit the length field");
#undef UU_SCTP_LAYOUT_ASSERT
