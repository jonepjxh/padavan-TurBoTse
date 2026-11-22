#include "nanoad.h"

#define DNS_MAX_PENDING 128
#define DNS_MAX_TCP_CLIENTS 32
#define DNS_MAX_UPSTREAMS 4
#define DNS_PACKET_MAX 4096
#define DNS_TIMEOUT_MS 600

enum { FD_UDP, FD_TCP_LISTEN, FD_UPSTREAM, FD_PENDING_TCP, FD_CLIENT_TCP };

typedef struct { int kind, index; } FdRef;
typedef struct {
    struct sockaddr_storage address;
    socklen_t address_length;
    int fd;
} Upstream;

typedef struct {
    int used;
    uint16_t upstream_id, client_id;
    int client_tcp;
    struct sockaddr_storage client_address;
    socklen_t client_address_length;
    int upstream, retries;
    uint64_t deadline;
    uint16_t query_length;
    unsigned char query[DNS_PACKET_MAX];
    int tcp_fd, tcp_state;
    size_t tcp_in_length, tcp_in_expected, tcp_out_offset;
    unsigned char tcp_in[DNS_PACKET_MAX + 2];
} Pending;

typedef struct {
    int fd;
    size_t in_length, expected, out_length, out_offset;
    int pending;
    unsigned char input[DNS_PACKET_MAX + 2];
    unsigned char output[DNS_PACKET_MAX + 2];
} TcpClient;

static uint16_t get16(const unsigned char *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static void put16(unsigned char *p, uint16_t v) { p[0] = (unsigned char)(v >> 8); p[1] = (unsigned char)v; }
static void put32(unsigned char *p, uint32_t v) { p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16); p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v; }

static int question(const unsigned char *packet, size_t length, char domain[NA_MAX_DOMAIN + 1],
                    size_t *end, uint16_t *type, uint16_t *class) {
    if (length < 17 || get16(packet + 4) < 1) return -1;
    size_t offset = 12, used = 0;
    while (offset < length) {
        unsigned int label = packet[offset++];
        if (!label) break;
        if ((label & 0xc0) || label > 63 || offset + label > length || used + label + 1 > NA_MAX_DOMAIN) return -1;
        if (used) domain[used++] = '.';
        memcpy(domain + used, packet + offset, label);
        used += label;
        offset += label;
    }
    if (!used || offset + 4 > length) return -1;
    domain[used] = 0;
    *type = get16(packet + offset);
    *class = get16(packet + offset + 2);
    *end = offset + 4;
    return 0;
}

static size_t empty_response(const unsigned char *query, size_t question_end, int rcode,
                             unsigned char *output) {
    memcpy(output, query, question_end);
    output[2] = (unsigned char)((query[2] & 0x79) | 0x80);
    output[3] = (unsigned char)(0x80 | (rcode & 15));
    put16(output + 4, 1);
    put16(output + 6, 0); put16(output + 8, 0); put16(output + 10, 0);
    return question_end;
}

static size_t block_response(const Config *cfg, const unsigned char *query, size_t end,
                             uint16_t type, unsigned char *output) {
    size_t length = empty_response(query, end, !strcmp(cfg->dns_response, "nxdomain") ? 3 : 0, output);
    if (strcmp(cfg->dns_response, "zero") || (type != 1 && type != 28) || length + 16 + 16 > DNS_PACKET_MAX)
        return length;
    size_t rdlength = type == 1 ? 4 : 16;
    put16(output + 6, 1);
    output[length++] = 0xc0; output[length++] = 0x0c;
    output[length++] = query[end - 4]; output[length++] = query[end - 3];
    output[length++] = 0; output[length++] = 1;
    put32(output + length, 60); length += 4;
    put16(output + length, (uint16_t)rdlength); length += 2;
    memset(output + length, 0, rdlength); length += rdlength;
    return length;
}

static void pending_tcp_close(Pending *p) {
    if (p->tcp_fd >= 0) close(p->tcp_fd);
    p->tcp_fd = -1;
    p->tcp_state = 0;
}

static void pending_clear(Pending *p) {
    pending_tcp_close(p);
    memset(p, 0, sizeof(*p));
    p->client_tcp = -1;
    p->tcp_fd = -1;
}

static void client_close(TcpClient *client, Pending *pending) {
    if (client->fd >= 0) close(client->fd);
    if (client->pending >= 0 && pending[client->pending].used) {
        pending[client->pending].client_tcp = -2;
        pending[client->pending].client_address_length = 0;
    }
    memset(client, 0, sizeof(*client));
    client->fd = -1;
    client->pending = -1;
}

static void client_response(Pending *p, TcpClient *clients, int udp_fd,
                            unsigned char *response, size_t length) {
    if (length < 2) return;
    unsigned char saved[2] = {response[0], response[1]};
    put16(response, p->client_id);
    if (p->client_tcp >= 0) {
        TcpClient *client = &clients[p->client_tcp];
        if (client->fd >= 0 && length <= DNS_PACKET_MAX) {
            put16(client->output, (uint16_t)length);
            memcpy(client->output + 2, response, length);
            client->out_length = length + 2;
            client->out_offset = 0;
            client->in_length = 0;
            client->expected = 2;
            client->pending = -1;
        }
    } else if (p->client_tcp == -1) {
        sendto(udp_fd, response, length, 0, (struct sockaddr *)&p->client_address, p->client_address_length);
    }
    memcpy(response, saved, 2);
}

static int allocate_id(Pending *pending) {
    for (int n = 0; n < 512; n++) {
        uint16_t id = (uint16_t)rand();
        int collision = 0;
        for (int i = 0; i < DNS_MAX_PENDING; i++)
            if (pending[i].used && pending[i].upstream_id == id) { collision = 1; break; }
        if (!collision) return id;
    }
    return -1;
}

static int udp_send_one(Pending *p, Upstream *upstream, int index) {
    unsigned char saved[2] = {p->query[0], p->query[1]};
    put16(p->query, p->upstream_id);
    ssize_t sent = sendto(upstream->fd, p->query, p->query_length, 0,
                          (struct sockaddr *)&upstream->address, upstream->address_length);
    memcpy(p->query, saved, 2);
    if (sent != (ssize_t)p->query_length) return -1;
    p->upstream = index;
    p->deadline = na_now_ms() + DNS_TIMEOUT_MS;
    return 0;
}

static int udp_send_query(Pending *p, Upstream *upstreams, int count) {
    int ok = 0;
    for (int i = 0; i < count; i++) if (udp_send_one(p, &upstreams[i], i) == 0) ok++;
    return ok ? 0 : -1;
}

static int tcp_begin(Pending *p, Upstream *upstreams) {
    pending_tcp_close(p);
    Upstream *server = &upstreams[p->upstream];
    int fd = socket(server->address.ss_family, SOCK_STREAM, 0);
    if (fd < 0 || na_set_nonblock(fd) < 0) { if (fd >= 0) close(fd); return -1; }
    if (connect(fd, (struct sockaddr *)&server->address, server->address_length) < 0 && errno != EINPROGRESS) {
#ifdef NA_DEBUG
        fprintf(stderr, "tcp_begin connect errno=%d\n", errno);
#endif
        close(fd); return -1;
    }
    p->tcp_fd = fd;
    p->tcp_state = 1;
    p->tcp_in_length = 0;
    p->tcp_in_expected = 2;
    p->tcp_out_offset = 0;
    p->deadline = na_now_ms() + DNS_TIMEOUT_MS * 2;
    return 0;
}

static int retry(Pending *p, Upstream *upstreams, int count) {
    pending_tcp_close(p);
    /* Primary-first: normal answers cost one packet.  Only after the primary
       times out do we try the secondary, and only after both fail do we fan
       out again.  This halves upstream traffic and avoids rate limits. */
    if (p->retries == 0 && count > 1 && udp_send_one(p, &upstreams[1], 1) == 0) {
        p->retries = 1;
        return 0;
    }
    if (++p->retries >= 3) return -1;
    return udp_send_query(p, upstreams, count);
}

/* Forward a query. Returns the pending slot or -1. The caller owns TCP filtering. */
static int forward(Pending *pending, TcpClient *clients, int tcp_index,
                   const struct sockaddr_storage *address, socklen_t address_length,
                   Upstream *upstreams, int upstream_count, const unsigned char *query, size_t length) {
    if (length < 2 || length > DNS_PACKET_MAX) return -1;
    int slot = -1;
    for (int i = 0; i < DNS_MAX_PENDING; i++) if (!pending[i].used) { slot = i; break; }
    if (slot < 0) {
#ifdef NA_DEBUG
        fprintf(stderr, "fwd: no slot len=%zu\n", length);
#endif
        return -1;
    }
    Pending *p = &pending[slot];
    pending_clear(p);
    p->used = 1;
    p->client_id = get16(query);
    p->client_tcp = tcp_index;
    if (address) {
        memcpy(&p->client_address, address, address_length);
        p->client_address_length = address_length;
    }
    memcpy(p->query, query, length);
    size_t send_length = length;
    /* Most stub clients (including dnsmasq here) send no OPT record.  A bare
       512-byte UDP answer truncates large RRsets and forces a slow TCP retry,
       so append a minimal EDNS0 OPT advertising 4096 bytes. */
    if (length >= 12 && get16(p->query + 10) == 0 && length + 11 <= DNS_PACKET_MAX) {
        unsigned char *opt = p->query + length;
        opt[0] = 0;
        put16(opt + 1, 41);
        put16(opt + 3, 4096);
        put32(opt + 5, 0);
        put16(opt + 9, 0);
        put16(p->query + 10, 1);
        send_length = length + 11;
    }
    p->query_length = (uint16_t)send_length;
#ifdef NA_DEBUG
    fprintf(stderr, "OUT len=%u:", p->query_length);
    for (unsigned int i = 0; i < p->query_length && i < 80; i++) fprintf(stderr, "%02x", p->query[i]);
    fprintf(stderr, "\n");
#endif
    p->upstream = 0;
    int id = allocate_id(pending);
    if (id < 0) {
#ifdef NA_DEBUG
        fprintf(stderr, "fwd: no id\n");
#endif
        pending_clear(p); return -1;
    }
    p->upstream_id = (uint16_t)id;
    int sent = udp_send_one(p, &upstreams[0], 0);
    if (sent < 0 && upstream_count > 1) sent = udp_send_one(p, &upstreams[1], 1);
    if (sent < 0) {
#ifdef NA_DEBUG
        fprintf(stderr, "fwd: send fail up=%d len=%u errno=%d\n", p->upstream, p->query_length, errno);
#endif
        pending_clear(p); return -1;
    }
    if (tcp_index >= 0) clients[tcp_index].pending = slot;
    return slot;
}

static void udp_receive(int udp_fd, Pending *pending, TcpClient *clients, Upstream *upstreams,
                        int upstream_count, const Config *cfg, const RuleSet *rules, SharedStats *stats) {
    for (int batch = 0; batch < 64; batch++) {
        unsigned char packet[DNS_PACKET_MAX], response[DNS_PACKET_MAX];
        struct sockaddr_storage address;
        socklen_t address_length = sizeof(address);
        ssize_t length = recvfrom(udp_fd, packet, sizeof(packet), 0, (struct sockaddr *)&address, &address_length);
        if (length < 0) return;
        if (length < 12) continue;
        char domain[NA_MAX_DOMAIN + 1];
        size_t end;
        uint16_t type, class;
        if (stats) stats->dns_total++;
        if (!question(packet, (size_t)length, domain, &end, &type, &class) && class == 1 && rules_match(rules, domain) > 0) {
            size_t n = block_response(cfg, packet, end, type, response);
            sendto(udp_fd, response, n, 0, (struct sockaddr *)&address, address_length);
            if (stats) { stats->dns_blocked++; na_stats_domain(stats->dns_last, domain); }
        } else {
            if (stats) stats->dns_forwarded++;
            if (forward(pending, clients, -1, &address, address_length, upstreams, upstream_count,
                        packet, (size_t)length) < 0 && stats) stats->dns_errors++;
        }
    }
}

static void upstream_receive(int index, Pending *pending, TcpClient *clients, Upstream *upstreams,
                             int upstream_count, int udp_fd, SharedStats *stats) {
    for (int batch = 0; batch < 64; batch++) {
        unsigned char response[DNS_PACKET_MAX];
        ssize_t length = recvfrom(upstreams[index].fd, response, sizeof(response), 0, NULL, NULL);
        if (length < 2) return;
        uint16_t id = get16(response);
        int slot = -1;
        for (int i = 0; i < DNS_MAX_PENDING; i++)
            if (pending[i].used && pending[i].tcp_fd < 0 && pending[i].upstream_id == id) { slot = i; break; }
        if (slot < 0) {
#ifdef NA_DEBUG
            fprintf(stderr, "up%d: no pending id=%u len=%zd\n", index, id, length);
#endif
            continue;
        }
        Pending *p = &pending[slot];
        p->upstream = index;
#ifdef NA_DEBUG
        fprintf(stderr, "resp up=%d id=%u len=%zd flags=0x%04x an=%u ar=%u\n", index, id, length,
                length >= 12 ? get16(response + 2) : 0, length >= 12 ? get16(response + 6) : 0,
                length >= 12 ? get16(response + 10) : 0);
#endif
        int rcode = length >= 12 ? (get16(response + 2) & 15) : 0;
        if (rcode == 2 || rcode == 4 || rcode == 5) {
            /* A broken/limited upstream may answer SERVFAIL instantly.  Do not
               hand that to the client while another upstream is still unused. */
            if (retry(p, upstreams, upstream_count) == 0) continue;
        }
        if (length >= 12 && (response[2] & 4)) {
            if (tcp_begin(p, upstreams) == 0) continue;
            if (retry(p, upstreams, upstream_count) == 0) continue;
            pending_clear(p);
            if (stats) stats->dns_errors++;
            continue;
        }
        client_response(p, clients, udp_fd, response, (size_t)length);
        pending_clear(p);
    }
}

static int pending_tcp_event(Pending *p, TcpClient *clients, int udp_fd, short events) {
    if (events & (POLLERR | POLLHUP | POLLNVAL)) return -1;
    if (p->tcp_state == 1) {
        int error = 0; socklen_t size = sizeof(error);
        if (getsockopt(p->tcp_fd, SOL_SOCKET, SO_ERROR, &error, &size) < 0 || error) return -1;
        p->tcp_state = 2;
    }
    if ((events & POLLOUT) && p->tcp_state == 2) {
        if (p->tcp_out_offset < 2) {
            unsigned char header[2]; put16(header, p->query_length);
            ssize_t sent = send(p->tcp_fd, header + p->tcp_out_offset, 2 - p->tcp_out_offset, 0);
            if (sent > 0) p->tcp_out_offset += (size_t)sent;
            else if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) return -1;
        } else {
            size_t offset = p->tcp_out_offset - 2;
            unsigned char saved[2] = {p->query[0], p->query[1]};
            put16(p->query, p->upstream_id);
            ssize_t sent = send(p->tcp_fd, p->query + offset, p->query_length - offset, 0);
            memcpy(p->query, saved, 2);
            if (sent > 0) p->tcp_out_offset += (size_t)sent;
            else if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) return -1;
        }
        if (p->tcp_out_offset == (size_t)p->query_length + 2) p->tcp_state = 3;
    }
    if ((events & POLLIN) && p->tcp_state == 3) {
        ssize_t got = recv(p->tcp_fd, p->tcp_in + p->tcp_in_length, p->tcp_in_expected - p->tcp_in_length, 0);
        if (!got) return -1;
        if (got < 0) return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
        p->tcp_in_length += (size_t)got;
        if (p->tcp_in_length == 2 && p->tcp_in_expected == 2) {
            size_t length = get16(p->tcp_in);
            if (!length || length > DNS_PACKET_MAX) return -1;
            p->tcp_in_expected = length + 2;
        } else if (p->tcp_in_length == p->tcp_in_expected) {
            if (get16(p->tcp_in + 2) == p->upstream_id)
                client_response(p, clients, udp_fd, p->tcp_in + 2, p->tcp_in_expected - 2);
            return 1;
        }
    }
    return 0;
}

static void accept_clients(int listener, TcpClient *clients) {
    for (;;) {
        int fd = accept(listener, NULL, NULL);
        if (fd < 0) return;
        na_set_cloexec(fd);
        na_set_nonblock(fd);
        int slot = -1;
        for (int i = 0; i < DNS_MAX_TCP_CLIENTS; i++) if (clients[i].fd < 0) { slot = i; break; }
        if (slot < 0) close(fd);
        else {
            memset(&clients[slot], 0, sizeof(clients[slot]));
            clients[slot].fd = fd; clients[slot].expected = 2; clients[slot].pending = -1;
        }
    }
}

static void client_event(int index, TcpClient *clients, Pending *pending, Upstream *upstreams,
                         int upstream_count, const Config *cfg, const RuleSet *rules,
                         SharedStats *stats, short events) {
    TcpClient *client = &clients[index];
    if (events & (POLLERR | POLLHUP | POLLNVAL)) { client_close(client, pending); return; }
    if ((events & POLLOUT) && client->out_offset < client->out_length) {
        ssize_t sent = send(client->fd, client->output + client->out_offset, client->out_length - client->out_offset, 0);
        if (sent > 0) client->out_offset += (size_t)sent;
        else if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) { client_close(client, pending); return; }
    }
    if (client->out_length && client->out_offset == client->out_length) client->out_length = client->out_offset = 0;
    if (!(events & POLLIN) || client->pending >= 0 || client->out_length) return;
    ssize_t got = recv(client->fd, client->input + client->in_length, client->expected - client->in_length, 0);
    if (!got) { client_close(client, pending); return; }
    if (got < 0) { if (errno != EAGAIN && errno != EWOULDBLOCK) client_close(client, pending); return; }
    client->in_length += (size_t)got;
    if (client->in_length == 2 && client->expected == 2) {
        size_t length = get16(client->input);
        if (!length || length > DNS_PACKET_MAX) { client_close(client, pending); return; }
        client->expected = length + 2;
    }
    if (client->in_length != client->expected || client->expected <= 2) return;
    if (stats) stats->dns_total++;
    char domain[NA_MAX_DOMAIN + 1]; size_t end; uint16_t type, class;
    if (!question(client->input + 2, client->expected - 2, domain, &end, &type, &class) && class == 1 && rules_match(rules, domain) > 0) {
        size_t n = block_response(cfg, client->input + 2, end, type, client->output + 2);
        put16(client->output, (uint16_t)n);
        client->out_length = n + 2; client->out_offset = 0;
        client->in_length = 0; client->expected = 2;
        if (stats) { stats->dns_blocked++; na_stats_domain(stats->dns_last, domain); }
        return;
    }
    if (stats) stats->dns_forwarded++;
    if (forward(pending, clients, index, NULL, 0, upstreams, upstream_count,
                client->input + 2, client->expected - 2) < 0) {
        if (stats) stats->dns_errors++;
        client_close(client, pending);
    }
}

int na_dns_main(const Config *cfg, int foreground) {
    const char *pidfile = "/tmp/nanoad/dns.pid";
    RuleSet rules;
    if (rules_load(&rules, cfg->rules_path) < 0) { fprintf(stderr, "nanoad: cannot load %s\n", cfg->rules_path); return 1; }
    Upstream upstreams[DNS_MAX_UPSTREAMS]; memset(upstreams, 0, sizeof(upstreams));
    int upstream_count = 0;
    char servers[sizeof(cfg->dns_upstreams)]; strcpy(servers, cfg->dns_upstreams);
    char *save = NULL;
    for (char *token = strtok_r(servers, ",", &save); token && upstream_count < DNS_MAX_UPSTREAMS; token = strtok_r(NULL, ",", &save)) {
        while (*token == ' ') token++;
        Upstream *server = &upstreams[upstream_count];
        if (na_parse_address(token, 53, &server->address, &server->address_length) < 0) continue;
        server->fd = socket(server->address.ss_family, SOCK_DGRAM, 0);
        if (server->fd < 0 || na_set_nonblock(server->fd) < 0) {
            if (server->fd >= 0) close(server->fd);
            continue;
        }
        upstream_count++;
    }
    if (!upstream_count) { fprintf(stderr, "nanoad: no valid upstream IP:port\n"); return 1; }
    int udp_fd = na_udp_listener(cfg->dns_listen, cfg->dns_port);
    int tcp_listen = na_tcp_listener(cfg->dns_listen, cfg->dns_port, 64);
    if (udp_fd < 0 || tcp_listen < 0) { fprintf(stderr, "nanoad: cannot bind %s:%d\n", cfg->dns_listen, cfg->dns_port); return 1; }
    na_set_nonblock(udp_fd); na_set_nonblock(tcp_listen);
    if (na_daemonize(pidfile, foreground) < 0) return 1;
    na_install_signals();
    SharedStats *stats = na_stats_open(cfg->stats_path);
    if (stats) stats->rules_count = rules.header->count;
    Pending *pending = calloc(DNS_MAX_PENDING, sizeof(*pending));
    TcpClient *clients = calloc(DNS_MAX_TCP_CLIENTS, sizeof(*clients));
    size_t max_fds = DNS_MAX_PENDING + DNS_MAX_TCP_CLIENTS + DNS_MAX_UPSTREAMS + 2;
    struct pollfd *fds = calloc(max_fds, sizeof(*fds));
    FdRef *refs = calloc(max_fds, sizeof(*refs));
    if (!pending || !clients || !fds || !refs) return 1;
    for (int i = 0; i < DNS_MAX_PENDING; i++) { pending[i].client_tcp = -1; pending[i].tcp_fd = -1; }
    for (int i = 0; i < DNS_MAX_TCP_CLIENTS; i++) { clients[i].fd = -1; clients[i].pending = -1; }
    na_log("DNS listening on %s:%d with %u rules", cfg->dns_listen, cfg->dns_port, rules.header->count);
    while (!g_stop) {
        if (g_reload) { g_reload = 0; if (!rules_reload(&rules) && stats) stats->rules_count = rules.header->count; }
        uint64_t now = na_now_ms();
        for (int i = 0; i < DNS_MAX_PENDING; i++) if (pending[i].used && pending[i].deadline <= now) {
            if (retry(&pending[i], upstreams, upstream_count) < 0) {
#ifdef NA_DEBUG
                fprintf(stderr, "timeout up=%d id=%u retries=%d len=%u\n", pending[i].upstream,
                        pending[i].upstream_id, pending[i].retries, pending[i].query_length);
#endif
                unsigned char response[DNS_PACKET_MAX]; char domain[NA_MAX_DOMAIN + 1]; size_t end; uint16_t type, class;
                if (!question(pending[i].query, pending[i].query_length, domain, &end, &type, &class)) {
                    size_t n = empty_response(pending[i].query, end, 2, response);
                    client_response(&pending[i], clients, udp_fd, response, n);
                }
                if (pending[i].client_tcp >= 0) client_close(&clients[pending[i].client_tcp], pending);
                pending_clear(&pending[i]);
                if (stats) stats->dns_errors++;
            }
        }
        int count = 0;
#define ADD_FD(KIND, INDEX, FD, EVENTS) do { refs[count].kind = KIND; refs[count].index = INDEX; fds[count].fd = FD; fds[count].events = EVENTS; count++; } while (0)
        ADD_FD(FD_UDP, 0, udp_fd, POLLIN);
        ADD_FD(FD_TCP_LISTEN, 0, tcp_listen, POLLIN);
        for (int i = 0; i < upstream_count; i++) ADD_FD(FD_UPSTREAM, i, upstreams[i].fd, POLLIN);
        for (int i = 0; i < DNS_MAX_PENDING; i++) if (pending[i].used && pending[i].tcp_fd >= 0) {
            short events = POLLIN;
            if (pending[i].tcp_state <= 2) events |= POLLOUT;
            ADD_FD(FD_PENDING_TCP, i, pending[i].tcp_fd, events);
        }
        for (int i = 0; i < DNS_MAX_TCP_CLIENTS; i++) if (clients[i].fd >= 0) {
            short events = POLLIN;
            if (clients[i].out_offset < clients[i].out_length) events |= POLLOUT;
            ADD_FD(FD_CLIENT_TCP, i, clients[i].fd, events);
        }
#undef ADD_FD
        int result = poll(fds, (nfds_t)count, 250);
        if (result < 0) { if (errno == EINTR) continue; break; }
        if (!result) continue;
        for (int i = 0; i < count; i++) {
            if (!fds[i].revents) continue;
            switch (refs[i].kind) {
            case FD_UDP: udp_receive(udp_fd, pending, clients, upstreams, upstream_count, cfg, &rules, stats); break;
            case FD_TCP_LISTEN: accept_clients(tcp_listen, clients); break;
            case FD_UPSTREAM: upstream_receive(refs[i].index, pending, clients, upstreams, upstream_count, udp_fd, stats); break;
            case FD_PENDING_TCP: {
                Pending *p = &pending[refs[i].index];
                if (!p->used) break;
                int status = pending_tcp_event(p, clients, udp_fd, fds[i].revents);
                if (status > 0) pending_clear(p);
                else if (status < 0 && retry(p, upstreams, upstream_count) < 0) {
                    if (p->client_tcp >= 0) client_close(&clients[p->client_tcp], pending);
                    pending_clear(p); if (stats) stats->dns_errors++;
                }
                break;
            }
            case FD_CLIENT_TCP:
                if (clients[refs[i].index].fd >= 0)
                    client_event(refs[i].index, clients, pending, upstreams, upstream_count, cfg, &rules, stats, fds[i].revents);
                break;
            }
        }
    }
    na_log("DNS stopped");
    for (int i = 0; i < DNS_MAX_PENDING; i++) if (pending[i].used) pending_clear(&pending[i]);
    for (int i = 0; i < DNS_MAX_TCP_CLIENTS; i++) if (clients[i].fd >= 0) close(clients[i].fd);
    close(udp_fd); close(tcp_listen);
    for (int i = 0; i < upstream_count; i++) close(upstreams[i].fd);
    free(pending); free(clients); free(fds); free(refs);
    na_stats_close(stats); rules_unload(&rules); na_remove_pidfile(pidfile);
    return 0;
}
