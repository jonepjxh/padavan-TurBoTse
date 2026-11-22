#include "nanoad.h"

static void usage(FILE *out) {
    fprintf(out,
        "NanoAd " NA_VERSION " - lightweight router ad blocker\n"
        "Usage:\n"
        "  nanoad compile -o rules.bin [--allow allow.txt] list.txt [...]\n"
        "  nanoad match -r rules.bin domain\n"
        "  nanoad sni-parse clienthello.bin\n"
        "  nanoad bench -r rules.bin count\n"
        "  nanoad dns|sni|web [-c nanoad.conf] [-r rules.bin] [-f]\n"
        "  nanoad --version\n");
}

static int compile_command(int argc, char **argv) {
    const char *output = NULL;
    char **allow = calloc((size_t)argc, sizeof(char *));
    char **inputs = calloc((size_t)argc, sizeof(char *));
    int allow_count = 0, input_count = 0;
    if (!allow || !inputs) return 1;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) output = argv[++i];
        else if (!strcmp(argv[i], "--allow") && i + 1 < argc) allow[allow_count++] = argv[++i];
        else inputs[input_count++] = argv[i];
    }
    if (!output || !input_count) { usage(stderr); free(allow); free(inputs); return 2; }
    CompileStats stats;
    int result = rules_compile(inputs, input_count, allow, allow_count, output, &stats);
    if (!result) printf("rules=%u accepted=%u duplicates=%u pruned=%u skipped=%u\n",
                        stats.final_count, stats.accepted, stats.duplicates, stats.pruned, stats.skipped);
    else fprintf(stderr, "nanoad: rule compilation failed\n");
    free(allow); free(inputs);
    return result ? 1 : 0;
}

static void seed_random(void) {
    unsigned int seed = (unsigned int)time(NULL) ^ (unsigned int)getpid();
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        unsigned int extra = 0;
        if (read(fd, &extra, sizeof(extra)) == (ssize_t)sizeof(extra)) seed ^= extra;
        close(fd);
    }
    srand(seed);
}

static int service_command(int argc, char **argv) {
    const char *mode = argv[1], *config_path = NA_DEFAULT_CONFIG, *rules_path = NA_DEFAULT_RULES;
    int foreground = 0;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "-c") && i + 1 < argc) config_path = argv[++i];
        else if (!strcmp(argv[i], "-r") && i + 1 < argc) rules_path = argv[++i];
        else if (!strcmp(argv[i], "-f")) foreground = 1;
        else { usage(stderr); return 2; }
    }
    Config cfg;
    if (na_config_load(&cfg, config_path) < 0) {
        fprintf(stderr, "nanoad: cannot read config: %s\n", config_path);
        return 1;
    }
    if (strcmp(rules_path, NA_DEFAULT_RULES)) snprintf(cfg.rules_path, sizeof(cfg.rules_path), "%s", rules_path);
    seed_random();
    if (!strcmp(mode, "dns")) return na_dns_main(&cfg, foreground);
    if (!strcmp(mode, "sni")) return na_sni_main(&cfg, foreground);
    if (!strcmp(mode, "web")) return na_web_main(&cfg, foreground);
    usage(stderr);
    return 2;
}

int main(int argc, char **argv) {
    if (argc < 2) { usage(stderr); return 2; }
    if (!strcmp(argv[1], "--version") || !strcmp(argv[1], "-v")) { puts(NA_VERSION); return 0; }
    if (!strcmp(argv[1], "compile")) return compile_command(argc, argv);
    if (!strcmp(argv[1], "match")) {
        if (argc != 5 || strcmp(argv[2], "-r")) { usage(stderr); return 2; }
        RuleSet rules;
        if (rules_load(&rules, argv[3]) < 0) { fprintf(stderr, "cannot load rules\n"); return 1; }
        int result = rules_match(&rules, argv[4]);
        printf("%s: %s\n", argv[4], result > 0 ? "block" : result < 0 ? "allow" : "pass");
        rules_unload(&rules);
        return 0;
    }
    if (!strcmp(argv[1], "bench")) {
        if (argc != 5 || strcmp(argv[2], "-r")) { usage(stderr); return 2; }
        RuleSet rules;
        if (rules_load(&rules, argv[3]) < 0) { fprintf(stderr, "cannot load rules\n"); return 1; }
        unsigned long count = strtoul(argv[4], NULL, 10);
        if (!count || count > 50000000ul) count = 1000000ul;
        struct timespec start, finish;
        volatile int sink = 0;
        char domain[64];
        clock_gettime(CLOCK_MONOTONIC, &start);
        for (unsigned long i = 0; i < count; i++) {
            snprintf(domain, sizeof(domain), "x.d%lu.example", i % (rules.header->count ? rules.header->count : 1));
            sink += rules_match(&rules, domain);
        }
        clock_gettime(CLOCK_MONOTONIC, &finish);
        double seconds = (double)(finish.tv_sec - start.tv_sec) + (double)(finish.tv_nsec - start.tv_nsec) / 1e9;
        printf("queries=%lu seconds=%.3f qps=%.0f sink=%d\n", count, seconds, seconds > 0 ? count / seconds : 0, sink);
        rules_unload(&rules);
        return 0;
    }
    if (!strcmp(argv[1], "sni-parse")) {
        if (argc != 3) { usage(stderr); return 2; }
        int fd = open(argv[2], O_RDONLY);
        unsigned char data[8192];
        ssize_t length = fd >= 0 ? read(fd, data, sizeof(data)) : -1;
        if (fd >= 0) close(fd);
        if (length <= 0) { fprintf(stderr, "cannot read hello\n"); return 1; }
        char domain[NA_MAX_DOMAIN + 1]; int has_name;
        if (!na_sni_parse_hello(data, (size_t)length, domain, &has_name)) {
            puts("need-more-data"); return 0;
        }
        if (has_name) printf("sni=%s\n", domain);
        else puts("sni=none");
        return 0;
    }
    if (!strcmp(argv[1], "dns") || !strcmp(argv[1], "sni") || !strcmp(argv[1], "web"))
        return service_command(argc, argv);
    usage(stderr);
    return 2;
}
