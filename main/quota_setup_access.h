#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Accept only the hotspot's IPv4 destination, including its IPv4-mapped IPv6
 * representation on a dual-stack HTTP socket. ap_ipv4 is in network byte order.
 * Unknown families, truncated addresses and native IPv6 fail closed. */
bool quota_setup_address_matches(const void *address, size_t address_size, uint32_t ap_ipv4);
