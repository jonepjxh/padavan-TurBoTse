#include "nanoad.h"
#include <ctype.h>
#include <libgen.h>
#include <sys/wait.h>

#define WEB_HEADER_MAX 16384
#define WEB_BODY_MAX (256 * 1024)
static char g_base[256] = "/etc/storage/nanoad";
static char g_csrf[33];

static const char *reason(int status) {
    switch (status) {
    case 200: return "OK"; case 204: return "No Content"; case 400: return "Bad Request";
    case 401: return "Unauthorized"; case 403: return "Forbidden"; case 404: return "Not Found";
    case 405: return "Method Not Allowed"; case 413: return "Payload Too Large";
    case 500: return "Internal Server Error"; default: return "Error";
    }
}

static void send_all(int fd, const void *data, size_t length) {
    const char *p = data;
    while (length) {
        ssize_t n = send(fd, p, length, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return;
        p += n; length -= (size_t)n;
    }
}

static void response(int fd, int status, const char *type, const char *body, size_t length, int authentication) {
    char header[1024];
    int n = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
        "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n"
        "X-Frame-Options: DENY\r\nContent-Security-Policy: default-src 'self'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; base-uri 'none'; frame-ancestors 'none'\r\n"
        "%sConnection: close\r\n\r\n", status, reason(status), type, length,
        authentication ? "WWW-Authenticate: Basic realm=\"NanoAd\"\r\n" : "");
    send_all(fd, header, (size_t)n);
    if (body && length) send_all(fd, body, length);
}

static void text_response(int fd, int status, const char *text) {
    response(fd, status, "text/plain; charset=utf-8", text, strlen(text), 0);
}

static void json_response(int fd, const char *json) {
    response(fd, 200, "application/json; charset=utf-8", json, strlen(json), 0);
}

static size_t json_escape(char *output, size_t size, const char *input) {
    size_t used = 0;
    if (!size) return 0;
    for (; *input && used + 7 < size; input++) {
        unsigned char c = (unsigned char)*input;
        if (c == '"' || c == '\\') { output[used++] = '\\'; output[used++] = (char)c; }
        else if (c == '\n') { output[used++] = '\\'; output[used++] = 'n'; }
        else if (c == '\r') { output[used++] = '\\'; output[used++] = 'r'; }
        else if (c == '\t') { output[used++] = '\\'; output[used++] = 't'; }
        else if (c < 32) { used += (size_t)snprintf(output + used, size - used, "\\u%04x", c); }
        else output[used++] = (char)c;
    }
    output[used] = 0;
    return used;
}

static int base64_encode(const unsigned char *input, size_t length, char *output, size_t size) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (size < ((length + 2) / 3) * 4 + 1) return -1;
    size_t i = 0, o = 0;
    while (i < length) {
        uint32_t a = i < length ? input[i++] : 0;
        uint32_t b = i < length ? input[i++] : 0;
        uint32_t c = i < length ? input[i++] : 0;
        uint32_t value = (a << 16) | (b << 8) | c;
        output[o++] = alphabet[(value >> 18) & 63]; output[o++] = alphabet[(value >> 12) & 63];
        output[o++] = alphabet[(value >> 6) & 63]; output[o++] = alphabet[value & 63];
    }
    size_t remainder = length % 3;
    if (remainder) { output[o - 1] = '='; if (remainder == 1) output[o - 2] = '='; }
    output[o] = 0;
    return 0;
}

static int constant_equal(const char *a, const char *b) {
    size_t la = strlen(a), lb = strlen(b), max = la > lb ? la : lb;
    unsigned char different = (unsigned char)(la ^ lb);
    for (size_t i = 0; i < max; i++) different |= (unsigned char)(a[i < la ? i : 0] ^ b[i < lb ? i : 0]);
    return different == 0;
}

static char *header_value(char *headers, const char *name) {
    static char value[2048];
    size_t length = strlen(name);
    char *line = headers;
    while (line && *line) {
        char *next = strstr(line, "\r\n");
        size_t line_length = next ? (size_t)(next - line) : strlen(line);
        if (line_length > length + 1 && !strncasecmp(line, name, length) && line[length] == ':') {
            char *start = line + length + 1;
            while ((size_t)(start - line) < line_length && (*start == ' ' || *start == '\t')) start++;
            size_t size = line_length - (size_t)(start - line);
            if (size >= sizeof(value)) size = sizeof(value) - 1;
            memcpy(value, start, size); value[size] = 0;
            return value;
        }
        line = next ? next + 2 : NULL;
    }
    return NULL;
}

static int authenticated(char *headers, const Config *cfg) {
    char *authorization = header_value(headers, "Authorization");
    if (!authorization || strncasecmp(authorization, "Basic ", 6)) return 0;
    char plain[256], encoded[512];
    int n = snprintf(plain, sizeof(plain), "%s:%s", cfg->web_user, cfg->web_token);
    if (n <= 0 || (size_t)n >= sizeof(plain) || base64_encode((unsigned char *)plain, (size_t)n, encoded, sizeof(encoded)) < 0) return 0;
    return constant_equal(authorization + 6, encoded);
}

static int csrf_valid(char *headers) {
    char *value = header_value(headers, "X-NanoAd-Token");
    return value && constant_equal(value, g_csrf);
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void url_decode(char *text) {
    char *out = text;
    while (*text) {
        if (*text == '%' && hex_value(text[1]) >= 0 && hex_value(text[2]) >= 0) {
            *out++ = (char)((hex_value(text[1]) << 4) | hex_value(text[2])); text += 3;
        } else if (*text == '+') { *out++ = ' '; text++; }
        else *out++ = *text++;
    }
    *out = 0;
}

static char *form_value(char *body, const char *key, char *value, size_t size) {
    char *save = NULL;
    value[0] = 0;
    for (char *item = strtok_r(body, "&", &save); item; item = strtok_r(NULL, "&", &save)) {
        char *equal = strchr(item, '=');
        if (!equal) continue;
        *equal++ = 0;
        url_decode(item); url_decode(equal);
        if (!strcmp(item, key)) { snprintf(value, size, "%s", equal); return value; }
    }
    return NULL;
}

static int atomic_write(const char *path, const char *data, size_t length) {
    char temporary[512];
    snprintf(temporary, sizeof(temporary), "%s.tmp.%ld", path, (long)getpid());
    int fd = open(temporary, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return -1;
    size_t offset = 0;
    while (offset < length) {
        ssize_t n = write(fd, data + offset, length - offset);
        if (n <= 0) { close(fd); unlink(temporary); return -1; }
        offset += (size_t)n;
    }
    if (fsync(fd) || close(fd)) { unlink(temporary); return -1; }
    return rename(temporary, path);
}

static int valid_subscriptions(const char *body) {
    char *copy = strdup(body);
    if (!copy) return 0;
    int valid = 1;
    char *save = NULL;
    for (char *line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        if (*line && line[strlen(line) - 1] == '\r') line[strlen(line) - 1] = 0;
        while (*line == ' ' || *line == '\t') line++;
        if (!*line || *line == '#') continue;
        if ((strncmp(line, "http://", 7) && strncmp(line, "https://", 8)) || strlen(line) > 2048) { valid = 0; break; }
        for (char *p = line; *p; p++) if ((unsigned char)*p < 32 || *p == 127) { valid = 0; break; }
        if (!valid) break;
    }
    free(copy);
    return valid;
}

static int append_file(const char *path, const char *body, size_t length) {
    char *old = NULL;
    size_t old_length = 0;
    FILE *file = fopen(path, "r");
    if (file) {
        fseek(file, 0, SEEK_END); long size = ftell(file); fseek(file, 0, SEEK_SET);
        if (size < 0 || size + (long)length > WEB_BODY_MAX) { fclose(file); return -1; }
        old = malloc((size_t)size + 2); old_length = (size_t)size;
        if (!old || fread(old, 1, old_length, file) != old_length) { fclose(file); free(old); return -1; }
        fclose(file);
        if (old_length && old[old_length - 1] != '\n') old[old_length++] = '\n';
    }
    char *combined = malloc(old_length + length + 1);
    if (!combined) { free(old); return -1; }
    if (old_length) memcpy(combined, old, old_length);
    memcpy(combined + old_length, body, length); combined[old_length + length] = 0;
    int result = atomic_write(path, combined, old_length + length);
    free(old); free(combined);
    return result;
}

static void background_script(const char *name, const char *argument, int delayed) {
    char command[1024];
    if (argument)
        snprintf(command, sizeof(command),
                 "(sleep %d; exec %s/%s %s) >>/tmp/nanoad/web-action.log 2>&1 &",
                 delayed ? 1 : 0, g_base, name, argument);
    else
        snprintf(command, sizeof(command),
                 "(sleep %d; exec %s/%s) >>/tmp/nanoad/web-action.log 2>&1 &",
                 delayed ? 1 : 0, g_base, name);
    if (system(command) < 0)
        fprintf(stderr, "cannot launch %s: %s\n", name, strerror(errno));
}

static uint64_t process_rss(const char *pidfile, int *running) {
    FILE *file = fopen(pidfile, "r");
    if (!file) return 0;
    long pid = 0;
    if (fscanf(file, "%ld", &pid) != 1) pid = 0;
    fclose(file);
    if (pid <= 0 || kill((pid_t)pid, 0) < 0) return 0;
    if (running) *running = 1;
    char path[64], line[256]; snprintf(path, sizeof(path), "/proc/%ld/status", pid);
    file = fopen(path, "r");
    if (!file) return 0;
    uint64_t kb = 0;
    while (fgets(line, sizeof(line), file)) if (!strncmp(line, "VmRSS:", 6)) { sscanf(line + 6, "%" SCNu64, &kb); break; }
    fclose(file); return kb;
}

static void status_api(int fd, const Config *cfg, const RuleSet *rules, SharedStats *stats) {
    char *json = malloc(16384);
    if (!json) { text_response(fd, 500, "out of memory"); return; }
    struct stat st;
    uint64_t rule_bytes = stat(cfg->rules_path, &st) ? 0 : (uint64_t)st.st_size;
    uint64_t updated_at = stat(cfg->rules_path, &st) ? 0 : (uint64_t)st.st_mtime;
    int dns_running = 0, sni_running = 0;
    uint64_t rss = process_rss("/tmp/nanoad/dns.pid", &dns_running) +
                   process_rss("/tmp/nanoad/sni.pid", &sni_running) +
                   process_rss("/tmp/nanoad/web.pid", NULL);
    SharedStats empty; memset(&empty, 0, sizeof(empty));
    if (!stats) stats = &empty;
    char dns_last[1024], sni_last[1024];
    json_escape(dns_last, sizeof(dns_last), stats->dns_last);
    json_escape(sni_last, sizeof(sni_last), stats->sni_last);
    snprintf(json, 16384,
        "{\"version\":\"%s\",\"filter_enabled\":%d,\"dns_enabled\":%d,\"sni_enabled\":%d,\"web_enabled\":1,"
        "\"dns_running\":%d,\"sni_running\":%d,"
        "\"dns_port\":%d,\"sni_port\":%d,\"web_port\":%d,\"sni_block_quic\":%d,"
        "\"dns_response\":\"%s\",\"dns_upstreams\":\"%s\",\"rules\":%u,\"rule_bytes\":%" PRIu64 ","
        "\"rss_kb\":%" PRIu64 ",\"dns_total\":%" PRIu64 ",\"dns_blocked\":%" PRIu64 ","
        "\"dns_forwarded\":%" PRIu64 ",\"dns_errors\":%" PRIu64 ",\"sni_total\":%" PRIu64 ","
        "\"sni_blocked\":%" PRIu64 ",\"sni_allowed\":%" PRIu64 ",\"sni_errors\":%" PRIu64 ","
        "\"started\":%" PRIu64 ",\"updated_at\":%" PRIu64 ",\"dns_last\":\"%s\",\"sni_last\":\"%s\"}",
        NA_VERSION, cfg->filter_enabled, cfg->dns_enabled, cfg->sni_enabled, dns_running, sni_running, cfg->dns_port, cfg->sni_port, cfg->web_port,
        cfg->sni_block_quic, cfg->dns_response, cfg->dns_upstreams, rules->header ? rules->header->count : 0,
        rule_bytes, rss, stats->dns_total, stats->dns_blocked, stats->dns_forwarded, stats->dns_errors,
        stats->sni_total, stats->sni_blocked, stats->sni_allowed, stats->sni_errors,
        stats->started, updated_at, dns_last, sni_last);
    json_response(fd, json); free(json);
}

static void file_api(int fd, const Config *cfg, const char *query) {
    char type[32] = "";
    if (query) {
        char copy[256]; snprintf(copy, sizeof(copy), "%s", query);
        form_value(copy, "type", type, sizeof(type));
    }
    const char *name = NULL;
    if (!strcmp(type, "subscriptions")) name = "subscriptions.txt";
    else if (!strcmp(type, "block")) name = "custom-block.txt";
    else if (!strcmp(type, "allow")) name = "custom-allow.txt";
    else if (!strcmp(type, "update-log")) { response(fd, 200, "text/plain; charset=utf-8", "", 0, 0); return; }
    if (!name) { text_response(fd, 404, "unknown file"); return; }
    char path[512]; snprintf(path, sizeof(path), "%s/%s", g_base, name);
    int f = open(path, O_RDONLY);
    if (f < 0) { response(fd, 200, "text/plain; charset=utf-8", "", 0, 0); return; }
    char *data = malloc(WEB_BODY_MAX + 1);
    if (!data) { close(f); text_response(fd, 500, "out of memory"); return; }
    ssize_t length = read(f, data, WEB_BODY_MAX); close(f);
    if (length < 0) length = 0;
    data[length] = 0;
    response(fd, 200, "text/plain; charset=utf-8", data, (size_t)length, 0); free(data);
    (void)cfg;
}

static void log_api(int fd) {
    int f = open("/tmp/nanoad/update.log", O_RDONLY);
    if (f < 0) { response(fd, 200, "text/plain; charset=utf-8", "", 0, 0); return; }
    off_t size = lseek(f, 0, SEEK_END);
    if (size > 32768) lseek(f, size - 32768, SEEK_SET); else lseek(f, 0, SEEK_SET);
    char *data = malloc(32769); ssize_t n = data ? read(f, data, 32768) : -1; close(f);
    if (!data || n < 0) { free(data); text_response(fd, 500, "read error"); return; }
    response(fd, 200, "text/plain; charset=utf-8", data, (size_t)n, 0); free(data);
}

static int account_name_valid(const char *name) {
    size_t length = strlen(name);
    if (length < 1 || length > 32) return 0;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++)
        if (!(isalnum(*p) || *p == '_' || *p == '-' || *p == '.')) return 0;
    return 1;
}

static int account_password_valid(const char *password) {
    size_t length = strlen(password);
    if (length < 4 || length > 64) return 0;
    for (const unsigned char *p = (const unsigned char *)password; *p; p++)
        if (*p < 33 || *p > 126 || *p == ':') return 0;
    return 1;
}

static void account_get(int fd, const Config *cfg) {
    char user[256];
    json_escape(user, sizeof(user), cfg->web_user);
    char json[512];
    snprintf(json, sizeof(json), "{\"user\":\"%s\"}", user);
    json_response(fd, json);
}

static void account_api(int fd, Config *cfg, char *body) {
    char user[64] = "", password[128] = "", confirm[128] = "";
    char copy[1024];
    snprintf(copy, sizeof(copy), "%s", body);
    form_value(copy, "username", user, sizeof(user));
    snprintf(copy, sizeof(copy), "%s", body);
    form_value(copy, "password", password, sizeof(password));
    snprintf(copy, sizeof(copy), "%s", body);
    form_value(copy, "confirm", confirm, sizeof(confirm));
    if (!account_name_valid(user)) { text_response(fd, 400, "username must be 1-32 chars: letters, digits, dot, dash, underscore"); return; }
    if (!account_password_valid(password)) { text_response(fd, 400, "password must be 4-64 printable chars, no colon"); return; }
    if (strcmp(password, confirm)) { text_response(fd, 400, "password confirmation does not match"); return; }
    char old_user[64], old_token[128];
    snprintf(old_user, sizeof(old_user), "%s", cfg->web_user);
    snprintf(old_token, sizeof(old_token), "%s", cfg->web_token);
    if (na_config_set(cfg->config_path, "web_user", user) < 0 ||
        na_config_set(cfg->config_path, "web_token", password) < 0) {
        na_config_set(cfg->config_path, "web_user", old_user);
        na_config_set(cfg->config_path, "web_token", old_token);
        text_response(fd, 500, "cannot write account settings");
        return;
    }
    snprintf(cfg->web_user, sizeof(cfg->web_user), "%s", user);
    snprintf(cfg->web_token, sizeof(cfg->web_token), "%s", password);
    text_response(fd, 200, "account updated; re-login required");
}

static void settings_api(int fd, const Config *cfg, char *body) {
    char value[1024];
    const char *booleans[] = {"filter_enabled", "dns_enabled", "sni_enabled", "sni_block_quic"};
    for (size_t i = 0; i < 4; i++) {
        char copy[1024]; snprintf(copy, sizeof(copy), "%s", body);
        if (form_value(copy, booleans[i], value, sizeof(value)) && (!strcmp(value, "0") || !strcmp(value, "1")))
            na_config_set(cfg->config_path, booleans[i], value);
    }
    char copy[1024]; snprintf(copy, sizeof(copy), "%s", body);
    if (form_value(copy, "dns_response", value, sizeof(value)) && (!strcmp(value, "zero") || !strcmp(value, "nxdomain")))
        na_config_set(cfg->config_path, "dns_response", value);
    snprintf(copy, sizeof(copy), "%s", body);
    if (form_value(copy, "dns_upstreams", value, sizeof(value)) && *value) {
        char test[1024]; strcpy(test, value); int valid = 1, count = 0; char *save = NULL;
        for (char *p = strtok_r(test, ",", &save); p; p = strtok_r(NULL, ",", &save)) {
            struct sockaddr_storage ss; socklen_t length;
            while (*p == ' ') p++;
            if (na_parse_address(p, 53, &ss, &length) < 0 || ++count > 4) { valid = 0; break; }
        }
        if (valid && count) na_config_set(cfg->config_path, "dns_upstreams", value);
    }
    background_script("nanoad-ctl", "restart", 1);
    text_response(fd, 200, "saved; services restart in 1 second");
}

static void handle_post(int fd, Config *cfg, const char *path, const char *query, char *headers,
                        char *body, size_t body_length) {
    if (!csrf_valid(headers)) { text_response(fd, 403, "invalid CSRF token"); return; }
    if (!strcmp(path, "/api/settings")) { settings_api(fd, cfg, body); return; }
    if (!strcmp(path, "/api/account")) { account_api(fd, cfg, body); return; }
    if (!strcmp(path, "/api/update")) { background_script("nanoad-update", NULL, 0); text_response(fd, 200, "update started"); return; }
    if (!strcmp(path, "/api/restart")) { background_script("nanoad-ctl", "restart", 1); text_response(fd, 200, "restart scheduled"); return; }
    if (!strcmp(path, "/api/subscriptions")) {
        if (!valid_subscriptions(body)) { text_response(fd, 400, "only http/https URLs, one per line"); return; }
        char file[512]; snprintf(file, sizeof(file), "%s/subscriptions.txt", g_base);
        int result = atomic_write(file, body, body_length);
        text_response(fd, result ? 500 : 200, result ? "write failed" : "saved");
        return;
    }
    if (!strcmp(path, "/api/custom")) {
        char copy[256], copy_append[256]; snprintf(copy, sizeof(copy), "%s", query ? query : "");
        snprintf(copy_append, sizeof(copy_append), "%s", query ? query : "");
        char type[16], append[16]; form_value(copy, "type", type, sizeof(type)); form_value(copy_append, "append", append, sizeof(append));
        if (strcmp(type, "block") && strcmp(type, "allow")) { text_response(fd, 400, "type must be block or allow"); return; }
        char file[512]; snprintf(file, sizeof(file), "%s/custom-%s.txt", g_base, type);
        int result = !strcmp(append, "1") ? append_file(file, body, body_length) : atomic_write(file, body, body_length);
        text_response(fd, result ? 500 : 200, result ? "write failed" : "saved");
        return;
    }
    text_response(fd, 404, "unknown API");
}

static void index_page(int fd, const Config *cfg) {
    char path[512]; snprintf(path, sizeof(path), "%s/index.html", cfg->web_root);
    FILE *file = fopen(path, "r");
    if (!file) { text_response(fd, 404, "WebUI not found"); return; }
    fseek(file, 0, SEEK_END); long size = ftell(file); fseek(file, 0, SEEK_SET);
    if (size < 0 || size > 262144) { fclose(file); text_response(fd, 500, "invalid WebUI"); return; }
    char *data = malloc((size_t)size + 1);
    if (!data || fread(data, 1, (size_t)size, file) != (size_t)size) { fclose(file); free(data); text_response(fd, 500, "read error"); return; }
    fclose(file); data[size] = 0;
    char *marker = strstr(data, "__CSRF__");
    char header[512];
    int n = snprintf(header, sizeof(header), "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %zu\r\nCache-Control: no-store\r\nX-Frame-Options: DENY\r\nConnection: close\r\n\r\n",
                     (size_t)size - (marker ? 8 : 0) + (marker ? strlen(g_csrf) : 0));
    send_all(fd, header, (size_t)n);
    if (marker) { send_all(fd, data, (size_t)(marker - data)); send_all(fd, g_csrf, strlen(g_csrf)); send_all(fd, marker + 8, (size_t)size - (size_t)(marker - data) - 8); }
    else send_all(fd, data, (size_t)size);
    free(data);
}

static void request(int fd, Config *cfg, RuleSet *rules, SharedStats *stats) {
    struct timeval timeout = {4, 0}; setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    size_t capacity = WEB_HEADER_MAX + WEB_BODY_MAX + 1, used = 0, header_length = 0, content_length = 0;
    char *buffer = malloc(capacity);
    if (!buffer) return;
    while (used + 1 < capacity) {
        ssize_t n = recv(fd, buffer + used, capacity - used - 1, 0);
        if (n <= 0) break;
        used += (size_t)n; buffer[used] = 0;
        if (!header_length) {
            char *end = strstr(buffer, "\r\n\r\n");
            if (end) {
                header_length = (size_t)(end - buffer) + 4;
                if (header_length > WEB_HEADER_MAX) { response(fd, 400, "text/plain", "headers too large", 16, 0); free(buffer); return; }
                char saved = buffer[header_length]; buffer[header_length] = 0;
                char *length_header = header_value(buffer, "Content-Length");
                if (length_header) content_length = (size_t)strtoul(length_header, NULL, 10);
                buffer[header_length] = saved;
                if (content_length > WEB_BODY_MAX) { response(fd, 413, "text/plain", "body too large", 13, 0); free(buffer); return; }
            }
        }
        if (header_length && used >= header_length + content_length) break;
    }
    if (!header_length || used < header_length + content_length) { free(buffer); return; }
    buffer[header_length + content_length] = 0;
    char saved = buffer[header_length]; buffer[header_length] = 0;
    char method[8], target[2048];
    if (sscanf(buffer, "%7s %2047s", method, target) != 2) { free(buffer); return; }
    int auth = authenticated(buffer, cfg), csrf = csrf_valid(buffer);
    (void)csrf;
    buffer[header_length] = saved;
    if (stats) stats->web_requests++;
    if (!auth) { response(fd, 401, "text/plain; charset=utf-8", "authentication required", 23, 1); free(buffer); return; }
    char *query = strchr(target, '?'); if (query) *query++ = 0;
    char *body = buffer + header_length;
    if (!strcmp(method, "POST")) handle_post(fd, cfg, target, query, buffer, body, content_length);
    else if (!strcmp(method, "GET")) {
        if (!strcmp(target, "/") || !strcmp(target, "/index.html")) index_page(fd, cfg);
        else if (!strcmp(target, "/api/status")) status_api(fd, cfg, rules, stats);
        else if (!strcmp(target, "/api/account")) account_get(fd, cfg);
        else if (!strcmp(target, "/api/file")) file_api(fd, cfg, query);
        else if (!strcmp(target, "/api/log")) log_api(fd);
        else if (!strcmp(target, "/api/check")) {
            char copy[512], domain[256], normalized[256]; snprintf(copy, sizeof(copy), "%s", query ? query : "");
            form_value(copy, "domain", domain, sizeof(domain));
            int valid = na_domain_normalize(domain, normalized) == 0;
            int match = valid ? rules_match(rules, normalized) : 0;
            char json[512]; snprintf(json, sizeof(json), "{\"domain\":\"%s\",\"result\":%d}", valid ? normalized : "", match);
            json_response(fd, json);
        } else text_response(fd, 404, "not found");
    } else text_response(fd, 405, "method not allowed");
    free(buffer);
}

static void make_csrf(void) {
    unsigned char bytes[16]; int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0 && read(fd, bytes, sizeof(bytes)) == (ssize_t)sizeof(bytes)) close(fd);
    else { if (fd >= 0) close(fd); for (size_t i = 0; i < sizeof(bytes); i++) bytes[i] = (unsigned char)rand(); }
    for (size_t i = 0; i < sizeof(bytes); i++) sprintf(g_csrf + i * 2, "%02x", bytes[i]);
}

int na_web_main(const Config *original, int foreground) {
    Config cfg = *original;
    if (!cfg.web_token[0] || !strcmp(cfg.web_token, "__NANOAD_TOKEN__")) {
        fprintf(stderr, "nanoad: web_token is not configured; run install.sh\n");
        return 1;
    }
    const char *pidfile = "/tmp/nanoad/web.pid";
    // Control scripts and writable config files live beside nanoad.conf, not
    // beside the running binary (nanoad-ctl executes a copy from /tmp/nanoad/bin).
    if (cfg.config_path[0]) {
        char config_copy[256];
        snprintf(config_copy, sizeof(config_copy), "%s", cfg.config_path);
        char *dir = dirname(config_copy);
        if (dir && *dir) snprintf(g_base, sizeof(g_base), "%s", dir);
    }
    RuleSet rules;
    if (rules_load(&rules, cfg.rules_path) < 0) { fprintf(stderr, "nanoad: cannot load %s\n", cfg.rules_path); return 1; }
    int listener = na_tcp_listener(cfg.web_listen, cfg.web_port, 16);
    if (listener < 0) { fprintf(stderr, "nanoad: cannot bind WebUI %s:%d\n", cfg.web_listen, cfg.web_port); return 1; }
    na_set_nonblock(listener);
    if (na_daemonize(pidfile, foreground) < 0) return 1;
    na_install_signals(); make_csrf();
    SharedStats *stats = na_stats_open(cfg.stats_path);
    na_log("WebUI listening on %s:%d", cfg.web_listen, cfg.web_port);
    while (!g_stop) {
        if (g_reload) {
            g_reload = 0; rules_reload(&rules);
            Config updated; if (!na_config_load(&updated, cfg.config_path) &&
                updated.web_token[0] && strcmp(updated.web_token, "__NANOAD_TOKEN__")) cfg = updated;
        }
        struct pollfd pfd = {listener, POLLIN, 0};
        int result = poll(&pfd, 1, 250);
        if (result < 0) { if (errno == EINTR) continue; break; }
        if (result > 0 && (pfd.revents & POLLIN)) {
            int fd = accept(listener, NULL, NULL);
            if (fd >= 0) { na_set_cloexec(fd); request(fd, &cfg, &rules, stats); close(fd); }
        }
    }
    close(listener); na_stats_close(stats); rules_unload(&rules); na_remove_pidfile(pidfile);
    return 0;
}
