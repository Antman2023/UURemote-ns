#pragma once
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdint.h>
#define IPVERSION 4
#define MAXTTL 255
struct ip {
    uint8_t ip_hl : 4, ip_v : 4;
    uint8_t ip_tos;
    uint16_t ip_len, ip_id, ip_off;
    uint8_t ip_ttl, ip_p;
    uint16_t ip_sum;
    struct in_addr ip_src, ip_dst;
};
