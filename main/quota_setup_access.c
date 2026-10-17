#include "quota_setup_access.h"
#if defined(ESP_PLATFORM)
#include "lwip/sockets.h"
#elif defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#endif
#include <string.h>

bool quota_setup_address_matches(const void *address, size_t address_size, uint32_t ap_ipv4) {
    struct sockaddr family = {0};
    const size_t family_size = offsetof(struct sockaddr, sa_family) + sizeof(family.sa_family);
    if (!address || !ap_ipv4 || address_size < family_size) return false;
    memcpy(&family, address, family_size);
    if (family.sa_family == AF_INET) {
        if (address_size < sizeof(struct sockaddr_in)) return false;
        struct sockaddr_in ipv4;
        memcpy(&ipv4, address, sizeof(ipv4));
        return ipv4.sin_addr.s_addr == ap_ipv4;
    }
#if !defined(ESP_PLATFORM) || CONFIG_LWIP_IPV6
    if (family.sa_family == AF_INET6) {
        if (address_size < sizeof(struct sockaddr_in6)) return false;
        struct sockaddr_in6 ipv6;
        memcpy(&ipv6, address, sizeof(ipv6));
        const unsigned char *bytes = (const unsigned char *)&ipv6.sin6_addr;
        static const unsigned char mapped_prefix[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255};
        return !memcmp(bytes, mapped_prefix, sizeof(mapped_prefix)) &&
               !memcmp(bytes + sizeof(mapped_prefix), &ap_ipv4, sizeof(ap_ipv4));
    }
#endif
    return false;
}
