#include "bench_common.h"

#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>

#ifdef __linux__
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#endif

#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

/* ---------------- RNG / stats / parsing ---------------- */

void bench_shuffle_u32(uint32_t *a, size_t n, BenchRng *r)
{
    if (n < 2)
        return;
    for (size_t i = n - 1; i > 0; i--) {
        const size_t j = (size_t)bench_rng_below(r, (uint64_t)i + 1);
        const uint32_t t = a[i];
        a[i] = a[j];
        a[j] = t;
    }
}

static int cmp_double(const void *a, const void *b)
{
    const double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

double bench_median(double *v, size_t n)
{
    if (n == 0)
        return 0.0;
    qsort(v, n, sizeof(*v), cmp_double);
    return (n & 1) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

void bench_flush_caches(unsigned char *buf, size_t bytes)
{
    for (size_t i = 0; i < bytes; i += 64)
        buf[i] = (unsigned char)(buf[i] + 1);
}

int bench_parse_u64_list(const char *s, uint64_t *out, int max)
{
    int n = 0;
    const char *p = s;
    while (*p) {
        char *end;
        unsigned long long v = strtoull(p, &end, 10);
        if (end == p)
            return -1;
        if (*end == 'K' || *end == 'k') {
            v *= 1000ull;
            end++;
        } else if (*end == 'M' || *end == 'm') {
            v *= 1000000ull;
            end++;
        }
        if ((*end != ',' && *end != '\0') || n >= max)
            return -1;
        out[n++] = (uint64_t)v;
        p = (*end == ',') ? end + 1 : end;
    }
    return n;
}

int bench_parse_name_list(const char *s, const char *const *names, int nnames, int *out, int max)
{
    int n = 0;
    const char *p = s;
    while (*p) {
        const char *e = strchr(p, ',');
        const size_t len = e ? (size_t)(e - p) : strlen(p);
        int found = -1;
        for (int i = 0; i < nnames; i++)
            if (strlen(names[i]) == len && strncmp(names[i], p, len) == 0)
                found = i;
        if (found < 0 || n >= max)
            return -1;
        out[n++] = found;
        if (e == NULL)
            break;
        p = e + 1;
    }
    return n;
}

/* ---------------- file helpers ---------------- */

static long kv_file_value(const char *path, const char *field)
{
    FILE *f = fopen(path, "r");
    if (f == NULL)
        return -1;
    char line[256];
    const size_t fl = strlen(field);
    long v = -1;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, field, fl) == 0 && line[fl] == ':') {
            v = strtol(line + fl + 1, NULL, 10);
            break;
        }
    }
    fclose(f);
    return v;
}

long bench_meminfo_value(const char *field) { return kv_file_value("/proc/meminfo", field); }
long bench_proc_status_kib(const char *field) { return kv_file_value("/proc/self/status", field); }

#ifdef __linux__
static int read_text(const char *path, char *buf, size_t len)
{
    FILE *f = fopen(path, "r");
    if (f == NULL)
        return -1;
    size_t n = fread(buf, 1, len - 1, f);
    fclose(f);
    buf[n] = '\0';
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == ' '))
        buf[--n] = '\0';
    return (int)n;
}

static long read_long(const char *path)
{
    char b[64];
    if (read_text(path, b, sizeof(b)) <= 0)
        return -1;
    return strtol(b, NULL, 10);
}
#endif

/* ---------------- cache / CPU information ---------------- */

int bench_cache_levels(BenchCacheLevel *out, int max)
{
    int n = 0;
#ifdef __linux__
    for (int i = 0; i < 8 && n < max; i++) {
        char path[128], buf[64];
        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu0/cache/index%d/level", i);
        const long level = read_long(path);
        if (level < 0)
            break;
        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu0/cache/index%d/type", i);
        if (read_text(path, buf, sizeof(buf)) <= 0 || strcmp(buf, "Instruction") == 0)
            continue;

        BenchCacheLevel *c = &out[n];
        memset(c, 0, sizeof(*c));
        c->level = (int)level;
        snprintf(c->type, sizeof(c->type), "%.15s", buf);

        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu0/cache/index%d/size", i);
        if (read_text(path, buf, sizeof(buf)) > 0) {
            char *end;
            long v = strtol(buf, &end, 10);
            if (*end == 'M')
                v *= 1024;
            c->size_kib = v;
        }
        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu0/cache/index%d/ways_of_associativity", i);
        c->ways = (int)read_long(path);
        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu0/cache/index%d/number_of_sets", i);
        c->sets = (int)read_long(path);
        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu0/cache/index%d/coherency_line_size", i);
        c->line = (int)read_long(path);
        n++;
    }
#else
    (void)out;
    (void)max;
#endif
    return n;
}

int bench_cache_line_size(void)
{
#if defined(__linux__)
    long v = read_long("/sys/devices/system/cpu/cpu0/cache/index0/coherency_line_size");
    if (v > 0)
        return (int)v;
#ifdef _SC_LEVEL1_DCACHE_LINESIZE
    v = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
    if (v > 0)
        return (int)v;
#endif
#elif defined(__APPLE__)
    int64_t v = 0;
    size_t sz = sizeof(v);
    if (sysctlbyname("hw.cachelinesize", &v, &sz, NULL, 0) == 0 && v > 0)
        return (int)v;
#endif
    return 64;
}

static void cpu_model(char *buf, size_t len)
{
    snprintf(buf, len, "unknown");
#if defined(__linux__)
    FILE *f = fopen("/proc/cpuinfo", "r");
    if (f == NULL)
        return;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "model name", 10) == 0) {
            char *p = strchr(line, ':');
            if (p != NULL) {
                p++;
                while (*p == ' ' || *p == '\t')
                    p++;
                size_t l = strlen(p);
                while (l > 0 && p[l - 1] == '\n')
                    p[--l] = '\0';
                snprintf(buf, len, "%s", p);
            }
            break;
        }
    }
    fclose(f);
#elif defined(__APPLE__)
    size_t sz = len;
    if (sysctlbyname("machdep.cpu.brand_string", buf, &sz, NULL, 0) != 0)
        snprintf(buf, len, "unknown");
#endif
}

void bench_print_system_info(FILE *f)
{
    struct utsname u;
    char cpu[256], date[64];
    const time_t now = time(NULL);
    strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", localtime(&now));
    cpu_model(cpu, sizeof(cpu));
    if (uname(&u) != 0)
        memset(&u, 0, sizeof(u));

    fprintf(f, "| Item | Value |\n|---|---|\n");
    fprintf(f, "| Date | %s |\n", date);
    fprintf(f, "| Kernel | %s %s |\n", u.sysname, u.release);
    fprintf(f, "| Machine | %s |\n", u.machine);
    fprintf(f, "| CPU | %s |\n", cpu);
    fprintf(f, "| Online CPUs | %ld |\n", sysconf(_SC_NPROCESSORS_ONLN));
    fprintf(f, "| Base page size | %ld B |\n", sysconf(_SC_PAGESIZE));
    fprintf(f, "| Cache line size | %d B |\n", bench_cache_line_size());

    BenchCacheLevel lv[8];
    const int nl = bench_cache_levels(lv, 8);
    for (int i = 0; i < nl; i++)
        fprintf(f, "| L%d %s cache | %ld KiB, %d-way, %d sets, %d B lines |\n", lv[i].level, lv[i].type,
                lv[i].size_kib, lv[i].ways, lv[i].sets, lv[i].line);

#ifdef __linux__
    char buf[128];
    if (read_text("/sys/kernel/mm/transparent_hugepage/enabled", buf, sizeof(buf)) > 0)
        fprintf(f, "| Transparent huge pages | %s (base mode opts out per mapping) |\n", buf);
    fprintf(f, "| HugePages total / free | %ld / %ld (size %ld kB) |\n", bench_meminfo_value("HugePages_Total"),
            bench_meminfo_value("HugePages_Free"), bench_meminfo_value("Hugepagesize"));
    fprintf(f, "| perf_event_paranoid | %ld |\n", read_long("/proc/sys/kernel/perf_event_paranoid"));
#endif
#ifdef __VERSION__
    fprintf(f, "| Compiler | %s |\n", __VERSION__);
#endif
}

/* ---------------- hardware counters ---------------- */

static const char *const perf_names[PC_COUNT] = {
    "cycles", "instructions", "L1D load misses", "LLC misses",
    "dTLB load misses", "dTLB store misses", "page faults",
};

const char *perf_event_name(int ev)
{
    return (ev >= 0 && ev < PC_COUNT) ? perf_names[ev] : "?";
}

/* Events measured together. Each set fits in the PMU at once (<= 3 events). */
static const int perf_set_events[PERF_MAX_SETS][3] = {
    {PC_CYCLES, PC_INSTRUCTIONS, PC_PAGE_FAULTS},
    {PC_L1D_LOAD_MISS, PC_LLC_MISS, -1},
    {PC_DTLB_LOAD_MISS, PC_DTLB_STORE_MISS, -1},
};

#ifdef __linux__
#define HWC(cache, op, res) \
    ((uint64_t)(cache) | ((uint64_t)(op) << 8) | ((uint64_t)(res) << 16))

static const struct {
    uint32_t type;
    uint64_t config;
} perf_defs[PC_COUNT] = {
    [PC_CYCLES] = {PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES},
    [PC_INSTRUCTIONS] = {PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS},
    [PC_L1D_LOAD_MISS] = {PERF_TYPE_HW_CACHE, HWC(PERF_COUNT_HW_CACHE_L1D, PERF_COUNT_HW_CACHE_OP_READ,
                                                  PERF_COUNT_HW_CACHE_RESULT_MISS)},
    [PC_LLC_MISS] = {PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_MISSES},
    [PC_DTLB_LOAD_MISS] = {PERF_TYPE_HW_CACHE, HWC(PERF_COUNT_HW_CACHE_DTLB, PERF_COUNT_HW_CACHE_OP_READ,
                                                   PERF_COUNT_HW_CACHE_RESULT_MISS)},
    [PC_DTLB_STORE_MISS] = {PERF_TYPE_HW_CACHE, HWC(PERF_COUNT_HW_CACHE_DTLB, PERF_COUNT_HW_CACHE_OP_WRITE,
                                                    PERF_COUNT_HW_CACHE_RESULT_MISS)},
    [PC_PAGE_FAULTS] = {PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS},
};

static int perf_open_event(int ev, int group_fd)
{
    struct perf_event_attr a;
    memset(&a, 0, sizeof(a));
    a.size = sizeof(a);
    a.type = perf_defs[ev].type;
    a.config = perf_defs[ev].config;
    a.disabled = (group_fd < 0); /* leader starts disabled; members follow it */
    a.exclude_kernel = 1;        /* user space only: works with perf_event_paranoid <= 2 */
    a.exclude_hv = 1;
    a.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
    return (int)syscall(SYS_perf_event_open, &a, 0 /* this process */, -1 /* any CPU */, group_fd, 0);
}
#endif

int perf_init(Perf *p, int enable)
{
    memset(p, 0, sizeof(*p));
    for (int s = 0; s < PERF_MAX_SETS; s++) {
        p->sets[s].leader = -1;
        for (int i = 0; i < PC_COUNT; i++)
            p->sets[s].fds[i] = -1;
    }
#ifdef __linux__
    if (!enable)
        return 0;
    for (int s = 0; s < PERF_MAX_SETS; s++) {
        PerfSet *set = &p->sets[p->nsets];
        for (int k = 0; k < 3; k++) {
            const int ev = perf_set_events[s][k];
            if (ev < 0)
                continue;
            const int fd = perf_open_event(ev, set->leader);
            if (fd < 0)
                continue; /* not supported on this CPU / not permitted */
            if (set->leader < 0)
                set->leader = fd;
            set->fds[set->n] = fd;
            set->events[set->n] = ev;
            set->n++;
            p->available[ev] = 1;
        }
        if (set->n > 0)
            p->nsets++;
    }
    return p->nsets;
#else
    (void)enable;
    (void)perf_set_events;
    return 0;
#endif
}

void perf_close(Perf *p)
{
#ifdef __linux__
    for (int s = 0; s < p->nsets; s++)
        for (int i = 0; i < p->sets[s].n; i++)
            close(p->sets[s].fds[i]);
#endif
    memset(p, 0, sizeof(*p));
}

void perf_start(Perf *p, int set)
{
#ifdef __linux__
    if (set < 0 || set >= p->nsets)
        return;
    const int fd = p->sets[set].leader;
    ioctl(fd, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP);
    ioctl(fd, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
#else
    (void)p;
    (void)set;
#endif
}

void perf_stop(Perf *p, int set, PerfReading *out)
{
    memset(out, 0, sizeof(*out));
#ifdef __linux__
    if (set < 0 || set >= p->nsets)
        return;
    const PerfSet *ps = &p->sets[set];
    ioctl(ps->leader, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP);

    uint64_t buf[3 + PC_COUNT];
    const ssize_t r = read(ps->leader, buf, sizeof(buf));
    if (r < (ssize_t)(3 * sizeof(uint64_t)))
        return;
    const uint64_t nr = buf[0], enabled = buf[1], running = buf[2];
    if (running == 0 || nr > (uint64_t)ps->n)
        return; /* the group never got the PMU */
    const double scale = (double)enabled / (double)running;
    for (uint64_t i = 0; i < nr; i++) {
        const int ev = ps->events[i];
        out->value[ev] = (double)buf[3 + i] * scale;
        out->valid[ev] = 1;
    }
#else
    (void)p;
    (void)set;
#endif
}
