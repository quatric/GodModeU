#ifndef GMU_COMPAT_NETWORK_H
#define GMU_COMPAT_NETWORK_H

/* libogc's net_* socket calls (which return -errno) on Cafe OS's BSD sockets (nsysnet). The network
 * itself is brought up by ftp_net_start() (ftp.c). */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include "ftp.h"

static inline int gmu_net_ret(int r) { return r < 0 ? -(errno ? errno : EIO) : r; }

#define net_socket(d, t, p)            gmu_net_ret(socket(d, t, p))
#define net_bind(s, a, l)              gmu_net_ret(bind(s, a, l))
#define net_listen(s, b)               gmu_net_ret(listen(s, b))
#define net_accept(s, a, l)            gmu_net_ret(accept(s, a, (socklen_t *)(l)))
#define net_connect(s, a, l)           gmu_net_ret(connect(s, a, l))
#define net_recv(s, b, l, f)           gmu_net_ret(recv(s, b, l, f))
#define net_send(s, b, l, f)           gmu_net_ret(send(s, b, l, f))
#define net_close(s)                   close(s)
#define net_shutdown(s, h)             shutdown(s, h)
#define net_setsockopt(s, l, o, v, n)  gmu_net_ret(setsockopt(s, l, o, v, n))
#define net_poll(p, n, t)              gmu_net_ret(poll(p, n, t))
#define pollsd pollfd

/* libogc passes IOS's non-blocking flag (4) to net_fcntl */
static inline int net_fcntl(int s, int cmd, int flags)
{
    if (cmd == F_SETFL) flags = (flags & 4) ? O_NONBLOCK : 0;
    return gmu_net_ret(fcntl(s, cmd, flags));
}

static inline unsigned net_gethostip(void) { return ftp_host_ip(); }

#endif
