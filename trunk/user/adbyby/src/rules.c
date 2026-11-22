#include "nanoad.h"
#include <ctype.h>

typedef struct {
    char *domain;
    uint8_t flags;
} BuildEntry;

typedef struct {
    BuildEntry *items;
    size_t count;
    size_t capacity;
    CompileStats stats;
} Builder;

static char *trim(char *text) {
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') text++;
    char *end = text + strlen(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) --end;
    *end = 0;
    return text;
}

int na_domain_normalize(const char *input, char output[NA_MAX_DOMAIN + 1]) {
    if (!input) return -1;
    while (*input == '.') input++;
    size_t length = strlen(input);
    while (length && input[length - 1] == '.') length--;
    if (!length || length > NA_MAX_DOMAIN) return -1;
    size_t label = 0;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)input[i];
        if (c >= 'A' && c <= 'Z') c = (unsigned char)(c + ('a' - 'A'));
        if (!(isalnum(c) || c == '-' || c == '_' || c == '.')) return -1;
        if (c == '.') {
            if (!label) return -1;
            label = 0;
        } else if (++label > 63) return -1;
        output[i] = (char)c;
    }
    if (!label) return -1;
    output[length] = 0;
    return 0;
}

uint32_t na_domain_hash(const char *domain, size_t length) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < length; i++) {
        hash ^= (unsigned char)domain[i];
        hash *= 16777619u;
    }
    return hash;
}

static int builder_add(Builder *builder, const char *domain, uint8_t flags) {
    if (builder->count == builder->capacity) {
        size_t capacity = builder->capacity ? builder->capacity * 2 : 4096;
        BuildEntry *items = realloc(builder->items, capacity * sizeof(*items));
        if (!items) return -1;
        builder->items = items;
        builder->capacity = capacity;
    }
    builder->items[builder->count].domain = strdup(domain);
    if (!builder->items[builder->count].domain) return -1;
    builder->items[builder->count].flags = flags;
    builder->count++;
    builder->stats.accepted++;
    return 0;
}

static int supported_modifiers(const char *modifiers) {
    if (!modifiers || !*modifiers) return 1;
    char copy[256];
    if (strlen(modifiers) >= sizeof(copy)) return 0;
    strcpy(copy, modifiers);
    char *save = NULL;
    int valid = 1;
    for (char *token = strtok_r(copy, ",", &save); token; token = strtok_r(NULL, ",", &save)) {
        token = trim(token);
        if (strcmp(token, "important")) valid = 0;
    }
    return valid;
}

static int add_domain_rule(Builder *builder, char *line, uint8_t forced_flags) {
    char *p = trim(line);
    if (!*p || *p == '!' || *p == '#' || *p == '[' || *p == ';') return 0;
    uint8_t flags = forced_flags;
    if (p[0] == '@' && p[1] == '@') {
        flags |= RULE_ALLOW;
        p = trim(p + 2);
    }
    if (!strncmp(p, "address=/", 9)) {
        p += 9;
        char *last = strrchr(p, '/');
        if (!last) return 0;
        *last = 0;
        char *save = NULL;
        for (char *domain = strtok_r(p, "/", &save); domain; domain = strtok_r(NULL, "/", &save)) {
            char normalized[NA_MAX_DOMAIN + 1];
            if (!na_domain_normalize(domain, normalized)) {
                if (builder_add(builder, normalized, flags & ~RULE_EXACT) < 0) return -1;
            } else builder->stats.skipped++;
        }
        return 0;
    }
    if (strchr(p, ' ') || strchr(p, '\t')) {
        char *save = NULL;
        char *first = strtok_r(p, " \t", &save);
        char *second = strtok_r(NULL, " \t", &save);
        if (!first || !second) return 0;
        if (strcmp(first, "0.0.0.0") && strcmp(first, "127.0.0.1") && strcmp(first, "::") &&
            strcmp(first, "::1") && strcmp(first, "0")) return 0;
        char *comment = strchr(second, '#');
        if (comment) *comment = 0;
        char normalized[NA_MAX_DOMAIN + 1];
        if (!na_domain_normalize(second, normalized)) {
            if (builder_add(builder, normalized, flags | RULE_EXACT) < 0) return -1;
        } else builder->stats.skipped++;
        return 0;
    }
    if (strstr(p, "##") || strstr(p, "#@#") || strstr(p, "#?#") || strstr(p, "://") ||
        p[0] == '/' || strchr(p, '?') || strchr(p, '=')) return 0;
    char *dollar = strchr(p, '$');
    if (dollar) {
        *dollar = 0;
        if (!supported_modifiers(dollar + 1)) return 0;
    }
    int suffix_pattern = 0;
    if (p[0] == '|' && p[1] == '|') {
        p += 2;
        suffix_pattern = 1;
    } else if (p[0] == '|') return 0;
    char *caret = strchr(p, '^');
    if (caret) {
        if (caret[1]) return 0;
        *caret = 0;
    }
    if (p[0] == '*' && p[1] == '.') p += 2;
    char normalized[NA_MAX_DOMAIN + 1];
    if (na_domain_normalize(p, normalized)) {
        if (*trim(p)) builder->stats.skipped++;
        return 0;
    }
    if (!suffix_pattern && !(forced_flags & RULE_EXACT)) flags &= ~RULE_EXACT;
    if (builder_add(builder, normalized, flags) < 0) return -1;
    return 0;
}

static int parse_file(Builder *builder, const char *path, uint8_t forced_flags) {
    FILE *file = fopen(path, "r");
    if (!file) return -1;
    char *line = NULL;
    size_t capacity = 0;
    ssize_t length;
    unsigned long line_number = 0;
    while ((length = getline(&line, &capacity, file)) >= 0) {
        line_number++;
        if (length > 16384) {
            builder->stats.skipped++;
            continue;
        }
        if (add_domain_rule(builder, line, forced_flags) < 0) {
            na_log("rule parse allocation failed: %s:%lu", path, line_number);
            free(line);
            fclose(file);
            return -1;
        }
    }
    free(line);
    fclose(file);
    return 0;
}

static int entry_compare(const void *left, const void *right) {
    const BuildEntry *a = left, *b = right;
    int result = strcmp(a->domain, b->domain);
    if (result) return result;
    return (int)b->flags - (int)a->flags;
}

static size_t deduplicate(Builder *builder) {
    if (!builder->count) return 0;
    qsort(builder->items, builder->count, sizeof(*builder->items), entry_compare);
    size_t output = 0;
    for (size_t i = 0; i < builder->count;) {
        size_t end = i + 1;
        uint8_t flags = builder->items[i].flags;
        while (end < builder->count && !strcmp(builder->items[i].domain, builder->items[end].domain)) {
            if (builder->items[end].flags & RULE_ALLOW) flags = RULE_ALLOW;
            else if (!(flags & RULE_ALLOW) && !(builder->items[end].flags & RULE_EXACT)) flags &= ~RULE_EXACT;
            free(builder->items[end].domain);
            end++;
        }
        builder->items[output].domain = builder->items[i].domain;
        builder->items[output].flags = flags;
        output++;
        builder->stats.duplicates += (uint32_t)(end - i - 1);
        i = end;
    }
    builder->count = output;
    return output;
}

static int domain_compare(const void *left, const void *right) {
    const BuildEntry *a = left, *b = right;
    return strcmp(a->domain, b->domain);
}

static int parent_is_blocked(BuildEntry *items, size_t count, const char *domain) {
    const char *dot = strchr(domain, '.');
    while (dot) {
        const char *parent = dot + 1;
        BuildEntry key = {(char *)parent, 0};
        BuildEntry *found = bsearch(&key, items, count, sizeof(*items), domain_compare);
        if (found && !(found->flags & (RULE_ALLOW | RULE_EXACT))) return 1;
        dot = strchr(parent, '.');
    }
    return 0;
}

static void prune_parents(Builder *builder) {
    size_t output = 0;
    for (size_t i = 0; i < builder->count; i++) {
        BuildEntry *entry = &builder->items[i];
        if (!(entry->flags & (RULE_ALLOW | RULE_EXACT)) &&
            parent_is_blocked(builder->items, builder->count, entry->domain)) {
            free(entry->domain);
            builder->stats.pruned++;
            continue;
        }
        if (output != i) builder->items[output] = builder->items[i];
        output++;
    }
    builder->count = output;
}

static uint32_t next_capacity(size_t count) {
    if (!count) return 1;
    size_t target = count * 2;
    uint32_t capacity = 1;
    while (capacity < target && capacity < (1u << 30)) capacity <<= 1;
    return capacity;
}

static int write_all(int fd, const void *buffer, size_t length) {
    const char *p = buffer;
    while (length) {
        ssize_t written = write(fd, p, length);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return -1;
        p += written;
        length -= (size_t)written;
    }
    return 0;
}

int rules_compile(char **inputs, int input_count, char **allow_inputs, int allow_count,
                  const char *output, CompileStats *stats) {
    Builder builder;
    memset(&builder, 0, sizeof(builder));
    for (int i = 0; i < input_count; i++)
        if (parse_file(&builder, inputs[i], 0) < 0) goto fail;
    for (int i = 0; i < allow_count; i++)
        if (parse_file(&builder, allow_inputs[i], RULE_ALLOW) < 0) goto fail;
    deduplicate(&builder);
    prune_parents(&builder);
    uint32_t capacity = next_capacity(builder.count);
    uint32_t table_size = capacity * (uint32_t)sizeof(RuleDiskEntry);
    uint32_t strings_offset = (uint32_t)sizeof(RuleDiskHeader) + table_size;
    uint32_t strings_size = 0;
    for (size_t i = 0; i < builder.count; i++) strings_size += (uint32_t)strlen(builder.items[i].domain) + 1;
    RuleDiskHeader header;
    memset(&header, 0, sizeof(header));
    memcpy(header.magic, NA_RULE_MAGIC, 8);
    header.version = 1;
    header.count = (uint32_t)builder.count;
    header.capacity = capacity;
    header.table_offset = sizeof(header);
    header.strings_offset = strings_offset;
    header.file_size = strings_offset + strings_size;
    RuleDiskEntry *table = malloc(table_size);
    char *strings = malloc(strings_size ? strings_size : 1);
    if (!table || !strings) {
        free(table);
        free(strings);
        goto fail;
    }
    for (uint32_t i = 0; i < capacity; i++) {
        table[i].hash = 0;
        table[i].offset = UINT32_MAX;
        table[i].length = 0;
        table[i].flags = 0;
        table[i].reserved = 0;
    }
    uint32_t offset = 0;
    for (size_t i = 0; i < builder.count; i++) {
        const char *domain = builder.items[i].domain;
        size_t length = strlen(domain);
        memcpy(strings + offset, domain, length + 1);
        uint32_t hash = na_domain_hash(domain, length);
        uint32_t slot = hash & (capacity - 1);
        while (table[slot].offset != UINT32_MAX) slot = (slot + 1) & (capacity - 1);
        table[slot].hash = hash;
        table[slot].offset = offset;
        table[slot].length = (uint16_t)length;
        table[slot].flags = builder.items[i].flags;
        offset += (uint32_t)length + 1;
    }
    int fd = open(output, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0 || write_all(fd, &header, sizeof(header)) || write_all(fd, table, table_size) ||
        (strings_size && write_all(fd, strings, strings_size)) || fsync(fd) || close(fd)) {
        if (fd >= 0) close(fd);
        free(table);
        free(strings);
        goto fail;
    }
    free(table);
    free(strings);
    builder.stats.final_count = (uint32_t)builder.count;
    if (stats) *stats = builder.stats;
    for (size_t i = 0; i < builder.count; i++) free(builder.items[i].domain);
    free(builder.items);
    return 0;
fail:
    for (size_t i = 0; i < builder.count; i++) free(builder.items[i].domain);
    free(builder.items);
    return -1;
}

int rules_load(RuleSet *rules, const char *path) {
    memset(rules, 0, sizeof(*rules));
    snprintf(rules->path, sizeof(rules->path), "%s", path);
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    struct stat st;
    if (fstat(fd, &st) || st.st_size < (off_t)sizeof(RuleDiskHeader)) {
        close(fd);
        return -1;
    }
    void *mapping = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (mapping == MAP_FAILED) return -1;
    RuleDiskHeader *header = mapping;
    uint64_t table_end = (uint64_t)header->table_offset +
                         (uint64_t)header->capacity * sizeof(RuleDiskEntry);
    int valid = !memcmp(header->magic, NA_RULE_MAGIC, 8) && header->version == 1 &&
                header->file_size == (uint32_t)st.st_size && header->table_offset == sizeof(*header) &&
                header->capacity && header->capacity <= (1u << 26) &&
                !(header->capacity & (header->capacity - 1)) &&
                header->count <= header->capacity && header->strings_offset <= header->file_size &&
                table_end <= header->strings_offset;
    if (!valid) {
        munmap(mapping, (size_t)st.st_size);
        return -1;
    }
    rules->mapping = mapping;
    rules->mapping_size = (size_t)st.st_size;
    rules->header = header;
    rules->table = (RuleDiskEntry *)((char *)mapping + header->table_offset);
    rules->strings = (char *)mapping + header->strings_offset;
    return 0;
}

void rules_unload(RuleSet *rules) {
    if (rules->mapping) munmap(rules->mapping, rules->mapping_size);
    memset(rules, 0, sizeof(*rules));
}

int rules_reload(RuleSet *rules) {
    char path[sizeof(rules->path)];
    strcpy(path, rules->path);
    RuleSet replacement;
    if (rules_load(&replacement, path) < 0) return -1;
    rules_unload(rules);
    *rules = replacement;
    return 0;
}

static RuleDiskEntry *lookup(const RuleSet *rules, const char *domain, size_t length) {
    if (!rules->mapping || !rules->header->capacity) return NULL;
    uint32_t hash = na_domain_hash(domain, length);
    uint32_t slot = hash & (rules->header->capacity - 1);
    size_t strings_size = rules->mapping_size - rules->header->strings_offset;
    for (uint32_t probes = 0; probes < rules->header->capacity; probes++) {
        RuleDiskEntry *entry = &rules->table[slot];
        if (entry->offset == UINT32_MAX) return NULL;
        if (entry->hash == hash && entry->length == length &&
            (uint64_t)entry->offset + entry->length <= strings_size &&
            !memcmp(rules->strings + entry->offset, domain, length)) return entry;
        slot = (slot + 1) & (rules->header->capacity - 1);
    }
    return NULL;
}

int rules_match(const RuleSet *rules, const char *domain) {
    char normalized[NA_MAX_DOMAIN + 1];
    if (na_domain_normalize(domain, normalized)) return 0;
    int blocked = 0;
    size_t full_length = strlen(normalized);
    for (char *p = normalized; p && *p;) {
        size_t length = strlen(p);
        RuleDiskEntry *entry = lookup(rules, p, length);
        if (entry && (!(entry->flags & RULE_EXACT) || p == normalized)) {
            if (entry->flags & RULE_ALLOW) return -1;
            blocked = 1;
        }
        (void)full_length;
        p = strchr(p, '.');
        if (p) p++;
    }
    return blocked;
}
