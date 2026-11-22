#include "nanoad.h"
#include <ctype.h>
#include <fcntl.h>
#include <libgen.h>
#include <net/if.h>
#include <sys/ioctl.h>

volatile sig_atomic_t g_reload = 0;
volatile sig_atomic_t g_stop = 0;

void na_handle_signal(int sig) {
    if (sig == SIGHUP) g_reload = 1;
    else g_stop = 1;
}

void na_install_signals(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = na_handle_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);
}

void na_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsyslog(LOG_INFO, fmt, ap);
    va_end(ap);
}

uint64_t na_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

int na_set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return flags < 0 ? -1 : fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int na_daemonize(const char *pidfile, int foreground) {
    if (!foreground) {
        pid_t pid = fork();
        if (pid < 0) return -1;
        if (pid > 0) _exit(0);
        if (setsid() < 0) return -1;
        pid = fork();
        if (pid < 0) return -1;
        if (pid > 0) _exit(0);
        int fd = open("/dev/null", O_RDWR);
        if (fd >= 0) {
            dup2(fd, STDIN_FILENO);
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            if (fd > 2) close(fd);
        }
    }
    openlog("nanoad", LOG_PID, LOG_DAEMON);
    mkdir("/tmp/nanoad", 0755);
    if (pidfile && *pidfile) {
        int fd = open(pidfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) return -1;
        char text[32];
        int length = snprintf(text, sizeof(text), "%ld\n", (long)getpid());
        if (write(fd, text, (size_t)length) != length) {
            close(fd);
            return -1;
        }
        close(fd);
    }
    return 0;
}

void na_remove_pidfile(const char *pidfile) {
    if (pidfile && *pidfile) unlink(pidfile);
}

static int parse_ip_port(const char *host, const char *port, int default_port,
                         struct sockaddr_storage *out, socklen_t *out_len) {
    char *end = NULL;
    long number = default_port;
    if (port && *port) {
        errno = 0;
        number = strtol(port, &end, 10);
        if (errno || !end || *end || number < 1 || number > 65535) return -1;
    }
    memset(out, 0, sizeof(*out));
    struct sockaddr_in *v4 = (struct sockaddr_in *)out;
    if (inet_pton(AF_INET, host, &v4->sin_addr) == 1) {
        v4->sin_family = AF_INET;
        v4->sin_port = htons((uint16_t)number);
        *out_len = sizeof(*v4);
        return 0;
    }
    struct sockaddr_in6 *v6 = (struct sockaddr_in6 *)out;
    if (inet_pton(AF_INET6, host, &v6->sin6_addr) == 1) {
        v6->sin6_family = AF_INET6;
        v6->sin6_port = htons((uint16_t)number);
        *out_len = sizeof(*v6);
        return 0;
    }
    return -1;
}

int na_parse_address(const char *text, int default_port, struct sockaddr_storage *out,
                     socklen_t *out_len) {
    char buffer[256];
    if (!text || strlen(text) >= sizeof(buffer)) return -1;
    strcpy(buffer, text);
    char *host = buffer, *port = NULL;
    if (buffer[0] == '[') {
        char *close = strchr(buffer, ']');
        if (!close) return -1;
        *close = 0;
        host = buffer + 1;
        if (close[1] == ':') port = close + 2;
        else if (close[1]) return -1;
    } else {
        char *colon = strrchr(buffer, ':');
        if (colon && strchr(buffer, ':') == colon) {
            *colon = 0;
            port = colon + 1;
        }
    }
    return parse_ip_port(host, port, default_port, out, out_len);
}

void na_set_cloexec(int fd) {
    int flags = fcntl(fd, F_GETFD, 0);
    if (flags >= 0) fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

static int make_listener(const char *address, int port, int type, int backlog) {
    struct sockaddr_storage ss;
    socklen_t length;
    if (na_parse_address(address, port, &ss, &length) < 0) return -1;
    int fd = socket(ss.ss_family, type, 0);
    if (fd < 0) return -1;
    na_set_cloexec(fd);
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    if (ss.ss_family == AF_INET6) setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &one, sizeof(one));
    if (bind(fd, (struct sockaddr *)&ss, length) < 0 || (type == SOCK_STREAM && listen(fd, backlog) < 0)) {
        close(fd);
        return -1;
    }
    return fd;
}

int na_tcp_listener(const char *address, int port, int backlog) {
    return make_listener(address, port, SOCK_STREAM, backlog);
}

int na_udp_listener(const char *address, int port) {
    return make_listener(address, port, SOCK_DGRAM, 0);
}

const char *na_config_get(const char *path, const char *key, char *value, size_t size) {
    FILE *file = fopen(path, "r");
    if (!file) return NULL;
    char line[1024];
    size_t key_len = strlen(key);
    value[0] = 0;
    while (fgets(line, sizeof(line), file)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (strncmp(p, key, key_len) || p[key_len] != '=') continue;
        p += key_len + 1;
        char *end = p + strlen(p);
        while (end > p && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ' || end[-1] == '\t')) --end;
        *end = 0;
        if ((size_t)(end - p) >= size) {
            fclose(file);
            return NULL;
        }
        memcpy(value, p, (size_t)(end - p) + 1);
        fclose(file);
        return value;
    }
    fclose(file);
    return NULL;
}

void na_config_defaults(Config *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    strcpy(cfg->dns_listen, "127.0.0.1");
    cfg->dns_port = 5353;
    strcpy(cfg->dns_upstreams, "223.5.5.5:53,119.29.29.29:53");
    strcpy(cfg->dns_response, "nxdomain");
    cfg->filter_enabled = 1;
    cfg->dns_enabled = 1;
    strcpy(cfg->sni_listen, "0.0.0.0");
    cfg->sni_port = 18443;
    cfg->sni_enabled = 0;
    cfg->sni_max_connections = 256;
    strcpy(cfg->sni_bind_iface, "auto");
    cfg->sni_block_quic = 0;
    strcpy(cfg->web_listen, "0.0.0.0");
    cfg->web_port = 18080;
    cfg->web_enabled = 1;
    strcpy(cfg->web_user, "admin");
    strcpy(cfg->web_root, "/etc/storage/nanoad/web");
    strcpy(cfg->rules_path, NA_DEFAULT_RULES);
    strcpy(cfg->stats_path, NA_DEFAULT_STATS);
}

static void copy_value(char *destination, size_t size, const char *value) {
    if (value && *value) snprintf(destination, size, "%s", value);
}

static int int_value(const char *value, int fallback, int minimum, int maximum) {
    if (!value || !*value) return fallback;
    char *end;
    long v = strtol(value, &end, 10);
    if (*end || v < minimum || v > maximum) return fallback;
    return (int)v;
}

int na_config_load(Config *cfg, const char *path) {
    na_config_defaults(cfg);
    snprintf(cfg->config_path, sizeof(cfg->config_path), "%s", path);
    char value[1024];
#define LOAD_STR(key, field) do { if (na_config_get(path, key, value, sizeof(value))) copy_value(cfg->field, sizeof(cfg->field), value); } while (0)
#define LOAD_INT(key, field, min, max) do { if (na_config_get(path, key, value, sizeof(value))) cfg->field = int_value(value, cfg->field, min, max); } while (0)
    LOAD_STR("dns_listen", dns_listen);
    LOAD_INT("dns_port", dns_port, 1, 65535);
    LOAD_STR("dns_upstreams", dns_upstreams);
    LOAD_STR("dns_response", dns_response);
    LOAD_INT("filter_enabled", filter_enabled, 0, 1);
    LOAD_INT("dns_enabled", dns_enabled, 0, 1);
    LOAD_STR("sni_listen", sni_listen);
    LOAD_INT("sni_port", sni_port, 1, 65535);
    LOAD_INT("sni_enabled", sni_enabled, 0, 1);
    LOAD_INT("sni_max_connections", sni_max_connections, 8, 512);
    LOAD_STR("sni_bind_iface", sni_bind_iface);
    LOAD_INT("sni_block_quic", sni_block_quic, 0, 1);
    LOAD_STR("web_listen", web_listen);
    LOAD_INT("web_port", web_port, 1, 65535);
    LOAD_INT("web_enabled", web_enabled, 0, 1);
    LOAD_STR("web_user", web_user);
    LOAD_STR("web_token", web_token);
    LOAD_STR("web_root", web_root);
#undef LOAD_STR
#undef LOAD_INT
    if (strcmp(cfg->dns_response, "zero") && strcmp(cfg->dns_response, "nxdomain"))
        strcpy(cfg->dns_response, "nxdomain");
    if (!*cfg->web_user) strcpy(cfg->web_user, "admin");
    if (!*cfg->web_token) return -1;
    return 0;
}

int na_config_set(const char *path, const char *key, const char *value) {
    if (!key || !*key || strlen(key) > 64 || !value || strlen(value) > 4000) return -1;
    for (const char *p = key; *p; p++)
        if (!(*p == '_' || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9'))) return -1;
    if (strchr(value, '\n') || strchr(value, '\r')) return -1;
    char temporary[512];
    snprintf(temporary, sizeof(temporary), "%s.tmp.%ld", path, (long)getpid());
    FILE *input = fopen(path, "r");
    FILE *output = fopen(temporary, "w");
    if (!output) {
        if (input) fclose(input);
        return -1;
    }
    int found = 0;
    char line[4096];
    size_t key_len = strlen(key);
    if (input) {
        while (fgets(line, sizeof(line), input)) {
            if (!strncmp(line, key, key_len) && line[key_len] == '=') {
                if (!found) fprintf(output, "%s=%s\n", key, value);
                found = 1;
            } else fputs(line, output);
        }
        fclose(input);
    }
    if (!found) fprintf(output, "%s=%s\n", key, value);
    if (fflush(output) || fsync(fileno(output)) || fclose(output)) {
        unlink(temporary);
        return -1;
    }
    chmod(temporary, 0600);
    return rename(temporary, path);
}

SharedStats *na_stats_open(const char *path) {
    int fd = open(path, O_RDWR | O_CREAT, 0600);
    if (fd < 0) return NULL;
    if (ftruncate(fd, sizeof(SharedStats)) < 0) {
        close(fd);
        return NULL;
    }
    void *memory = mmap(NULL, sizeof(SharedStats), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (memory == MAP_FAILED) return NULL;
    SharedStats *stats = memory;
    if (stats->magic != NA_STATS_MAGIC || stats->version != NA_STATS_VERSION) {
        memset(stats, 0, sizeof(*stats));
        stats->version = NA_STATS_VERSION;
        stats->started = (uint64_t)time(NULL);
        __sync_synchronize();
        stats->magic = NA_STATS_MAGIC;
    }
    return stats;
}

void na_stats_close(SharedStats *stats) {
    if (stats) munmap(stats, sizeof(*stats));
}

void na_stats_domain(char destination[256], const char *domain) {
    snprintf(destination, 256, "%s", domain ? domain : "");
}
