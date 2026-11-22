#include "nanoad.h"
#include <netinet/tcp.h>
#include <net/if.h>
#include <sys/ioctl.h>

#ifndef SO_ORIGINAL_DST
#define SO_ORIGINAL_DST 80
#endif
#ifndef SO_BINDTODEVICE
#define SO_BINDTODEVICE 25
#endif
#define SNI_BUFFER_SIZE 8192
#define SNI_HANDSHAKE_TIMEOUT 8000
#define SNI_IDLE_TIMEOUT 300000

typedef struct { size_t read, write; unsigned char data[SNI_BUFFER_SIZE]; } Buffer;
typedef struct {
    int fd[2];
    int connecting;
    int decided;
    int eof[2];
    int shutdown_sent[2];
    uint64_t activity;
    Buffer c2s, s2c;
} Connection;

static uint16_t sni16(const unsigned char *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t sni24(const unsigned char *p) { return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2]; }

/* 1 = decided, 0 = need more data. A missing/unsupported SNI fails open. */
static int parse_client_hello(const unsigned char *data, size_t length, char domain[NA_MAX_DOMAIN + 1], int *blocked_name) {
    *blocked_name = 0;
    domain[0] = 0;
    if (length < 5) return 0;
    if (data[0] != 22 || data[1] != 3) return 1;
    size_t record_length = sni16(data + 3);
    if (record_length > SNI_BUFFER_SIZE - 5) return 1;
    if (length < record_length + 5) return 0;
    if (record_length < 4 || data[5] != 1) return 1;
    size_t hello_length = sni24(data + 6);
    if (hello_length + 4 > record_length) return 1; /* fragmented handshake: fail open */
    size_t p = 9, end = p + hello_length;
    if (p + 34 > end) return 1;
    p += 34;
    if (p >= end) return 1;
    size_t n = data[p++];
    if (p + n + 2 > end) return 1;
    p += n;
    size_t cipher_length = sni16(data + p); p += 2;
    if (p + cipher_length + 1 > end) return 1;
    p += cipher_length;
    n = data[p++];
    if (p + n + 2 > end) return 1;
    p += n;
    size_t extensions_length = sni16(data + p); p += 2;
    if (p + extensions_length > end) return 1;
    size_t extensions_end = p + extensions_length;
    while (p + 4 <= extensions_end) {
        unsigned int type = sni16(data + p);
        size_t size = sni16(data + p + 2); p += 4;
        if (p + size > extensions_end) return 1;
        if (type == 0) {
            if (size < 5) return 1;
            size_t list_end = p + 2 + sni16(data + p);
            size_t q = p + 2;
            if (list_end > p + size) return 1;
            while (q + 3 <= list_end) {
                unsigned int name_type = data[q];
                size_t name_length = sni16(data + q + 1); q += 3;
                if (q + name_length > list_end || name_length > NA_MAX_DOMAIN) return 1;
                if (name_type == 0) {
                    memcpy(domain, data + q, name_length);
                    domain[name_length] = 0;
                    *blocked_name = 1;
                    return 1;
                }
                q += name_length;
            }
            return 1;
        }
        p += size;
    }
    return 1;
}

int na_sni_parse_hello(const unsigned char *data, size_t length, char domain[NA_MAX_DOMAIN + 1], int *has_name) {
    return parse_client_hello(data, length, domain, has_name);
}

static void connection_close(Connection *c) {
    if (c->fd[0] >= 0) close(c->fd[0]);
    if (c->fd[1] >= 0) close(c->fd[1]);
    memset(c, 0, sizeof(*c));
    c->fd[0] = c->fd[1] = -1;
}

static int buffer_read(int fd, Buffer *buffer) {
    if (buffer->read == buffer->write) buffer->read = buffer->write = 0;
    if (buffer->read && buffer->write == SNI_BUFFER_SIZE) {
        memmove(buffer->data, buffer->data + buffer->read, buffer->write - buffer->read);
        buffer->write -= buffer->read; buffer->read = 0;
    }
    if (buffer->write == SNI_BUFFER_SIZE) return 0;
    ssize_t got = recv(fd, buffer->data + buffer->write, SNI_BUFFER_SIZE - buffer->write, 0);
    if (got > 0) { buffer->write += (size_t)got; return 1; }
    if (!got) return -2;
    if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
    return -1;
}

static int buffer_write(int fd, Buffer *buffer) {
    if (buffer->read == buffer->write) { buffer->read = buffer->write = 0; return 0; }
    ssize_t sent = send(fd, buffer->data + buffer->read, buffer->write - buffer->read, 0);
    if (sent > 0) {
        buffer->read += (size_t)sent;
        if (buffer->read == buffer->write) buffer->read = buffer->write = 0;
        return 1;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
    return -1;
}

static int original_destination(int fd, struct sockaddr_in *destination) {
    socklen_t length = sizeof(*destination);
    memset(destination, 0, sizeof(*destination));
    return getsockopt(fd, SOL_IP, SO_ORIGINAL_DST, destination, &length);
}

static int iface_is_up(const char *name) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return 0;
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", name);
    int up = 0;
    if (ioctl(fd, SIOCGIFFLAGS, &ifr) == 0)
        up = (ifr.ifr_flags & IFF_UP) && (ifr.ifr_flags & IFF_RUNNING);
    close(fd);
    return up;
}

/* Locally originated flows on this Padavan build can land on an unstable
   ECMP member (four PPP links).  Binding the upstream socket to one working
   WAN interface keeps every proxied TLS flow on the same path. */
static const char *choose_bind_iface(const Config *cfg) {
    static char chosen[32];
    if (cfg->sni_bind_iface[0] && strcmp(cfg->sni_bind_iface, "auto")) {
        snprintf(chosen, sizeof(chosen), "%s", cfg->sni_bind_iface);
        return chosen;
    }
    for (int i = 0; i < 4; i++) {
        snprintf(chosen, sizeof(chosen), "ppp%d", i);
        if (iface_is_up(chosen)) return chosen;
    }
    chosen[0] = 0;
    return NULL;
}

static void accept_connections(int listener, Connection *connections, int maximum,
                               SharedStats *stats, const char *bind_iface) {    for (;;) {
        int client = accept(listener, NULL, NULL);
        if (client < 0) return;
        na_set_cloexec(client);
        int slot = -1;
        for (int i = 0; i < maximum; i++) if (connections[i].fd[0] < 0) { slot = i; break; }
        struct sockaddr_in destination;
        if (slot < 0 || original_destination(client, &destination) < 0 || destination.sin_port == 0) {
            close(client);
            if (stats) stats->sni_errors++;
            continue;
        }
        int remote = socket(AF_INET, SOCK_STREAM, 0);
        if (remote < 0 || na_set_nonblock(client) < 0 || na_set_nonblock(remote) < 0) {
            if (remote >= 0) close(remote);
            close(client);
            continue;
        }
        if (bind_iface && bind_iface[0] &&
            setsockopt(remote, SOL_SOCKET, SO_BINDTODEVICE, bind_iface, strlen(bind_iface) + 1) < 0) {
#ifdef NA_DEBUG
            fprintf(stderr, "sni: cannot bind %s errno=%d\n", bind_iface, errno);
#endif
        }
        int one = 1;
        setsockopt(client, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        setsockopt(remote, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        Connection *c = &connections[slot];
        memset(c, 0, sizeof(*c));
        c->fd[0] = client; c->fd[1] = remote; c->connecting = 1; c->activity = na_now_ms();
        if (connect(remote, (struct sockaddr *)&destination, sizeof(destination)) < 0 && errno != EINPROGRESS) {
            connection_close(c);
            if (stats) stats->sni_errors++;
            continue;
        }
        if (stats) stats->sni_total++;
    }
}

int na_sni_main(const Config *cfg, int foreground) {
    const char *pidfile = "/tmp/nanoad/sni.pid";
    RuleSet rules;
    if (rules_load(&rules, cfg->rules_path) < 0) { fprintf(stderr, "nanoad: cannot load %s\n", cfg->rules_path); return 1; }
    int listener = na_tcp_listener(cfg->sni_listen, cfg->sni_port, 128);
    if (listener < 0) { fprintf(stderr, "nanoad: cannot bind SNI %s:%d\n", cfg->sni_listen, cfg->sni_port); return 1; }
    na_set_nonblock(listener);
    if (na_daemonize(pidfile, foreground) < 0) return 1;
    na_install_signals();
    SharedStats *stats = na_stats_open(cfg->stats_path);
    int maximum = cfg->sni_max_connections;
    Connection *connections = calloc((size_t)maximum, sizeof(*connections));
    struct pollfd *fds = calloc((size_t)maximum * 2 + 1, sizeof(*fds));
    int *indices = calloc((size_t)maximum * 2 + 1, sizeof(*indices));
    unsigned char *sides = calloc((size_t)maximum * 2 + 1, 1);
    if (!connections || !fds || !indices || !sides) return 1;
    for (int i = 0; i < maximum; i++) connections[i].fd[0] = connections[i].fd[1] = -1;
    const char *bind_iface = choose_bind_iface(cfg);
    na_log("SNI filter listening on %s:%d, max=%d, outbound=%s",
           cfg->sni_listen, cfg->sni_port, maximum,
           bind_iface && bind_iface[0] ? bind_iface : "kernel-route");
    while (!g_stop) {
        if (g_reload) { g_reload = 0; rules_reload(&rules); }
        uint64_t now = na_now_ms();
        for (int i = 0; i < maximum; i++) if (connections[i].fd[0] >= 0) {
            uint64_t timeout = connections[i].decided ? SNI_IDLE_TIMEOUT : SNI_HANDSHAKE_TIMEOUT;
            if (now - connections[i].activity > timeout) connection_close(&connections[i]);
        }
        int count = 0;
        fds[count].fd = listener; fds[count].events = POLLIN; indices[count] = -1; count++;
        for (int i = 0; i < maximum; i++) {
            Connection *c = &connections[i];
            if (c->fd[0] < 0) continue;
            for (int side = 0; side < 2; side++) {
                Buffer *incoming = side == 0 ? &c->c2s : &c->s2c;
                Buffer *outgoing = side == 0 ? &c->s2c : &c->c2s;
                short events = 0;
                if (!c->eof[side] && incoming->write < SNI_BUFFER_SIZE) events |= POLLIN;
                if ((side == 1 && c->connecting) || outgoing->read < outgoing->write) {
                    if (side == 0 || c->decided) events |= POLLOUT;
                }
                fds[count].fd = c->fd[side]; fds[count].events = events;
                indices[count] = i; sides[count] = (unsigned char)side; count++;
            }
        }
        int result = poll(fds, (nfds_t)count, 250);
        if (result < 0) { if (errno == EINTR) continue; break; }
        if (!result) continue;
        if (fds[0].revents & POLLIN) accept_connections(listener, connections, maximum, stats, bind_iface);
        for (int k = 1; k < count; k++) {
            int i = indices[k], side = sides[k];
            Connection *c = &connections[i];
            if (c->fd[side] < 0 || c->fd[side] != fds[k].fd || !fds[k].revents) continue;
            short events = fds[k].revents;
            if (events & (POLLERR | POLLNVAL)) { connection_close(c); if (stats) stats->sni_errors++; continue; }
            if (side == 1 && c->connecting && (events & POLLOUT)) {
                int error = 0; socklen_t size = sizeof(error);
                if (getsockopt(c->fd[1], SOL_SOCKET, SO_ERROR, &error, &size) < 0 || error) {
                    connection_close(c); if (stats) stats->sni_errors++; continue;
                }
                c->connecting = 0;
            }
            Buffer *incoming = side == 0 ? &c->c2s : &c->s2c;
            Buffer *outgoing = side == 0 ? &c->s2c : &c->c2s;
            if ((events & POLLOUT) && (side == 0 || c->decided) && outgoing->read < outgoing->write) {
                int status = buffer_write(c->fd[side], outgoing);
                if (status < 0) { connection_close(c); continue; }
                c->activity = now;
            }
            if ((events & POLLIN) && !c->eof[side]) {
                int status = buffer_read(c->fd[side], incoming);
                if (status == -1) { connection_close(c); continue; }
                if (status == -2) c->eof[side] = 1;
                if (status > 0) c->activity = now;
            }
            if (!c->decided && side == 0 && c->c2s.write > c->c2s.read) {
                char domain[NA_MAX_DOMAIN + 1]; int has_name;
                if (parse_client_hello(c->c2s.data + c->c2s.read, c->c2s.write - c->c2s.read, domain, &has_name)) {
                    c->decided = 1;
                    if (has_name && !na_domain_normalize(domain, domain) && rules_match(&rules, domain) > 0) {
                        if (stats) { stats->sni_blocked++; na_stats_domain(stats->sni_last, domain); }
                        connection_close(c);
                        continue;
                    }
                    if (stats) stats->sni_allowed++;
                } else if (c->c2s.write == SNI_BUFFER_SIZE) {
                    c->decided = 1;
                    if (stats) stats->sni_allowed++;
                }
            }
            for (int s = 0; s < 2; s++) {
                Buffer *sent = s == 0 ? &c->c2s : &c->s2c;
                if (c->eof[s] && sent->read == sent->write && !c->shutdown_sent[s]) {
                    shutdown(c->fd[1 - s], SHUT_WR); c->shutdown_sent[s] = 1;
                }
            }
            if (c->eof[0] && c->eof[1] && c->c2s.read == c->c2s.write && c->s2c.read == c->s2c.write)
                connection_close(c);
            else if ((events & POLLHUP) && incoming->read == incoming->write) {
                c->eof[side] = 1;
                if (c->eof[0] && c->eof[1]) connection_close(c);
            }
        }
    }
    for (int i = 0; i < maximum; i++) if (connections[i].fd[0] >= 0) connection_close(&connections[i]);
    close(listener);
    free(connections); free(fds); free(indices); free(sides);
    na_stats_close(stats); rules_unload(&rules); na_remove_pidfile(pidfile);
    return 0;
}
