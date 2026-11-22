#ifndef NANOAD_H
#define NANOAD_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#define NA_VERSION "1.1.7"
#define NA_RULE_MAGIC "NADR0001"
#define NA_STATS_MAGIC 0x4e414453u
#define NA_STATS_VERSION 1u
#define NA_MAX_DOMAIN 253
#define NA_DEFAULT_CONFIG "/etc/storage/nanoad/nanoad.conf"
#define NA_DEFAULT_RULES "/tmp/nanoad/rules.bin"
#define NA_DEFAULT_STATS "/tmp/nanoad/stats.bin"

#define RULE_ALLOW 0x01u
#define RULE_EXACT 0x02u

typedef struct {
    char magic[8];
    uint32_t version;
    uint32_t count;
    uint32_t capacity;
    uint32_t table_offset;
    uint32_t strings_offset;
    uint32_t file_size;
    uint32_t reserved;
} RuleDiskHeader;

typedef struct {
    uint32_t hash;
    uint32_t offset;
    uint16_t length;
    uint8_t flags;
    uint8_t reserved;
} RuleDiskEntry;

typedef struct {
    void *mapping;
    size_t mapping_size;
    RuleDiskHeader *header;
    RuleDiskEntry *table;
    char *strings;
    char path[256];
} RuleSet;

typedef struct {
    uint32_t accepted;
    uint32_t skipped;
    uint32_t duplicates;
    uint32_t pruned;
    uint32_t final_count;
} CompileStats;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint64_t started;
    uint64_t dns_total;
    uint64_t dns_blocked;
    uint64_t dns_forwarded;
    uint64_t dns_errors;
    uint64_t sni_total;
    uint64_t sni_blocked;
    uint64_t sni_allowed;
    uint64_t sni_errors;
    uint64_t web_requests;
    uint64_t rules_count;
    uint64_t updated_at;
    char dns_last[256];
    char sni_last[256];
} SharedStats;

typedef struct {
    char dns_listen[64];
    int dns_port;
    char dns_upstreams[512];
    char dns_response[16];
    int filter_enabled;
    int dns_enabled;
    char sni_listen[64];
    int sni_port;
    int sni_enabled;
    int sni_max_connections;
    char sni_bind_iface[32];
    int sni_block_quic;
    char web_listen[64];
    int web_port;
    int web_enabled;
    char web_user[64];
    char web_token[128];
    char web_root[256];
    char config_path[256];
    char rules_path[256];
    char stats_path[256];
} Config;

extern volatile sig_atomic_t g_reload;
extern volatile sig_atomic_t g_stop;

void na_handle_signal(int sig);
void na_install_signals(void);
int na_daemonize(const char *pidfile, int foreground);
void na_remove_pidfile(const char *pidfile);
void na_log(const char *fmt, ...);
uint64_t na_now_ms(void);
int na_set_nonblock(int fd);
void na_set_cloexec(int fd);
int na_tcp_listener(const char *address, int port, int backlog);
int na_udp_listener(const char *address, int port);
int na_parse_address(const char *text, int default_port, struct sockaddr_storage *out,
                     socklen_t *out_len);
const char *na_config_get(const char *path, const char *key, char *value, size_t size);
void na_config_defaults(Config *cfg);
int na_config_load(Config *cfg, const char *path);
int na_config_set(const char *path, const char *key, const char *value);
SharedStats *na_stats_open(const char *path);
void na_stats_close(SharedStats *stats);
void na_stats_domain(char destination[256], const char *domain);

int rules_compile(char **inputs, int input_count, char **allow_inputs, int allow_count,
                  const char *output, CompileStats *stats);
int rules_load(RuleSet *rules, const char *path);
void rules_unload(RuleSet *rules);
int rules_reload(RuleSet *rules);
int rules_match(const RuleSet *rules, const char *domain);
uint32_t na_domain_hash(const char *domain, size_t length);
int na_domain_normalize(const char *input, char output[NA_MAX_DOMAIN + 1]);

int na_dns_main(const Config *cfg, int foreground);
int na_sni_main(const Config *cfg, int foreground);
int na_sni_parse_hello(const unsigned char *data, size_t length, char domain[NA_MAX_DOMAIN + 1], int *has_name);
int na_web_main(const Config *cfg, int foreground);

#endif
