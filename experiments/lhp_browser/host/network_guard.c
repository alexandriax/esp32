/* Host test seam: this executable must never create network sockets or resolve
 * hosts, even if a fixture exposes a gap in the document asset filter.
 * LWS's local wakeup socketpair and ordinary file I/O remain available.
 * This is a test guard, not a sandbox for untrusted HTML or local file access.
 */
#include <errno.h>
#include <netdb.h>
#include <stddef.h>
#include <sys/socket.h>

size_t moss_probe_network_attempts;

int socket(int domain, int type, int protocol)
{
    (void)domain; (void)type; (void)protocol;
    moss_probe_network_attempts++;
    errno = EACCES;
    return -1;
}

int getaddrinfo(const char *node, const char *service, const struct addrinfo *hints,
                struct addrinfo **result)
{
    (void)node; (void)service; (void)hints;
    moss_probe_network_attempts++;
    if (result) *result = NULL;
    return EAI_FAIL;
}
