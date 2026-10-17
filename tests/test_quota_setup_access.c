#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#endif
#include "quota_setup_access.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    const unsigned char hotspot[4] = {192, 168, 4, 1};
    const unsigned char lan[4] = {192, 168, 1, 23};
    uint32_t ap;
    memcpy(&ap, hotspot, sizeof(ap));
    struct sockaddr_in v4 = {0};
    v4.sin_family = AF_INET;
    memcpy(&v4.sin_addr, hotspot, sizeof(hotspot));
    assert(quota_setup_address_matches(&v4, sizeof(v4), ap));

    /* This is the destination reported by lwIP on IDF's IPv6 HTTP listener
     * for an ordinary request to http://192.168.4.1/. */
    struct sockaddr_in6 mapped = {0};
    mapped.sin6_family = AF_INET6;
    unsigned char *bytes = (unsigned char *)&mapped.sin6_addr;
    bytes[10] = bytes[11] = 255;
    memcpy(bytes + 12, hotspot, sizeof(hotspot));
    assert(quota_setup_address_matches(&mapped, sizeof(mapped), ap));
    struct sockaddr_storage storage = {0};
    memcpy(&storage, &mapped, sizeof(mapped));
    assert(quota_setup_address_matches(&storage, sizeof(mapped), ap));
    assert(quota_setup_address_matches(&storage, sizeof(storage), ap));

    /* Access through the station/LAN destination remains rejected for both
     * representations; never expose setup or saved-key export on that socket. */
    memcpy(&v4.sin_addr, lan, sizeof(lan));
    memcpy(bytes + 12, lan, sizeof(lan));
    assert(!quota_setup_address_matches(&v4, sizeof(v4), ap));
    assert(!quota_setup_address_matches(&mapped, sizeof(mapped), ap));
    memcpy(bytes + 12, hotspot, sizeof(hotspot));
    bytes[10] = bytes[11] = 0; /* IPv4-compatible IPv6 is not IPv4-mapped. */
    assert(!quota_setup_address_matches(&mapped, sizeof(mapped), ap));
    bytes[0] = 0xfe; bytes[1] = 0x80; /* Nor is native link-local IPv6. */
    assert(!quota_setup_address_matches(&mapped, sizeof(mapped), ap));
    memset(&mapped.sin6_addr, 0, sizeof(mapped.sin6_addr));
    assert(!quota_setup_address_matches(&mapped, sizeof(mapped), ap));

    memcpy(&v4.sin_addr, hotspot, sizeof(hotspot));
    bytes[10] = bytes[11] = 255;
    memcpy(bytes + 12, hotspot, sizeof(hotspot));
    for (size_t n = 0; n < sizeof(v4); ++n)
        assert(!quota_setup_address_matches(&v4, n, ap));
    for (size_t n = 0; n < sizeof(mapped); ++n)
        assert(!quota_setup_address_matches(&mapped, n, ap));
    assert(!quota_setup_address_matches(NULL, sizeof(storage), ap));
    assert(!quota_setup_address_matches(&v4, sizeof(v4), 0));
    v4.sin_family = AF_UNSPEC;
    assert(!quota_setup_address_matches(&v4, sizeof(v4), ap));
    puts("Hotspot socket access: IPv4/mapped IPv6, LAN isolation and truncated addresses: PASS");
    return 0;
}
