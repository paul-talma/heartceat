#include "harness.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "util.h"

#define ULL(x) ((unsigned long long)(x))

enum status { RUN_OK, RUN_WRONG, RUN_SKIP, RUN_FAIL, RUN_NA };

enum wkind { W_TREE, W_FLAT, W_NESTED, W_VADD };

static const char *wnames[] = {"tree", "flat loop", "nested loop", "vector add"};

static const char *labels[] = {"", "WRONG", "n/a", "FAIL", "-"};

struct workload {
    enum wkind kind;
    union {
        struct tree t;
        struct array f;
        struct jagged a;
        struct vadd v;
    };
    u64 sum;
    u64 items;
};

struct result {
    enum status status;
    double median;
    double min;
    u64 got;
    u64 bad;
};

static char *xstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    return memcpy(xmalloc(n), s, n);
}

int case_init(struct tcase *c, const char *name, int argc, char **argv, const char *basedir) {
    memset(c, 0, sizeof *c);
    c->argv = xmalloc((size_t)(argc + 1) * sizeof *c->argv);
    size_t speclen = 1;
    for (int i = 0; i < argc; i++) {
        if (!strncmp(argv[i], "expect=", 7)) {
            if (parse_u64(argv[i] + 7, &c->expect)) {
                free(c->argv);
                return -1;
            }
            c->has_expect = 1;
            continue;
        }
        c->argv[c->argc++] = xstrdup(argv[i]);
        speclen += strlen(argv[i]) + 1;
    }
    c->argv[c->argc] = NULL;

    c->spec = xmalloc(speclen);
    c->spec[0] = 0;
    for (int i = 0; i < c->argc; i++) {
        if (i)
            strcat(c->spec, " ");
        strcat(c->spec, c->argv[i]);
    }
    c->name = xstrdup(name);

    if (basedir && c->argc == 2 && !strcmp(c->argv[0], "file") && c->argv[1][0] != '/') {
        size_t n = strlen(basedir) + strlen(c->argv[1]) + 2;
        char *full = xmalloc(n);
        snprintf(full, n, "%s/%s", basedir, c->argv[1]);
        free(c->argv[1]);
        c->argv[1] = full;
    }
    return 0;
}

void case_free(struct tcase *c) {
    for (int i = 0; i < c->argc; i++)
        free(c->argv[i]);
    free(c->argv);
    free(c->spec);
    free(c->name);
}

void cases_free(struct tcase *cs, int n) {
    for (int i = 0; i < n; i++)
        case_free(&cs[i]);
    free(cs);
}

int cases_load(const char *path, struct tcase **out, int *n) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "%s: %s\n", path, strerror(errno));
        return -1;
    }
    const char *slash = strrchr(path, '/');
    char *basedir = slash ? strndup(path, (size_t)(slash - path)) : xstrdup(".");
    int cap = 16, count = 0, lineno = 0, rc = 0;
    struct tcase *cs = xmalloc((size_t)cap * sizeof *cs);
    char line[4096];
    while (fgets(line, sizeof line, f)) {
        lineno++;
        line[strcspn(line, "#")] = 0;
        char *toks[64];
        int nt = 0;
        for (char *t = strtok(line, " \t\r\n"); t && nt < 64; t = strtok(NULL, " \t\r\n"))
            toks[nt++] = t;
        if (nt == 0)
            continue;
        if (nt < 2) {
            fprintf(stderr, "%s:%d: expected NAME KIND [ARGS...] [expect=SUM]\n", path, lineno);
            rc = -1;
            break;
        }
        if (count == cap) {
            cap *= 2;
            cs = xrealloc(cs, (size_t)cap * sizeof *cs);
        }
        if (case_init(&cs[count], toks[0], nt - 1, toks + 1, basedir)) {
            fprintf(stderr, "%s:%d: bad expect= value\n", path, lineno);
            rc = -1;
            break;
        }
        count++;
    }
    fclose(f);
    free(basedir);
    if (rc) {
        cases_free(cs, count);
        return -1;
    }
    *out = cs;
    *n = count;
    return 0;
}

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static int workload_build(struct workload *w, const struct tcase *c, char *err, size_t errlen) {
    memset(w, 0, sizeof *w);
    const char *kind = c->argc ? c->argv[0] : "";
    if (array_is_kind(kind)) {
        w->kind = W_FLAT;
        if (array_build(&w->f, c->argc, c->argv, err, errlen))
            return -1;
        array_stats(&w->f);
        w->sum = w->f.sum;
        w->items = w->f.n;
    } else if (jagged_is_kind(kind)) {
        w->kind = W_NESTED;
        if (jagged_build(&w->a, c->argc, c->argv, err, errlen))
            return -1;
        jagged_stats(&w->a);
        w->sum = w->a.sum;
        w->items = w->a.items;
    } else if (vadd_is_kind(kind)) {
        w->kind = W_VADD;
        if (vadd_build(&w->v, c->argc, c->argv, err, errlen))
            return -1;
        w->sum = w->v.sum;
        w->items = w->v.n;
    } else {
        w->kind = W_TREE;
        if (tree_build(&w->t, c->argc, c->argv, err, errlen))
            return -1;
        tree_stats(&w->t);
        w->sum = w->t.sum;
        w->items = w->t.nodes;
    }
    return 0;
}

static void workload_print(const struct workload *w, double built) {
    printf("  ");
    if (w->kind == W_TREE)
        printf("nodes %llu, depth %llu, ", ULL(w->t.nodes), ULL(w->t.depth));
    else if (w->kind == W_NESTED)
        printf("rows %llu, items %llu, longest row %llu, ", ULL(w->a.rows), ULL(w->a.items),
               ULL(w->a.longest));
    else
        printf("items %llu, ", ULL(w->items));
    printf("%s %llu, built in %.0f ms\n", w->kind == W_VADD ? "checksum" : "sum", ULL(w->sum),
           built * 1e3);
}

static void workload_free(struct workload *w) {
    switch (w->kind) {
    case W_TREE:
        tree_free(&w->t);
        break;
    case W_FLAT:
        array_free(&w->f);
        break;
    case W_NESTED:
        jagged_free(&w->a);
        break;
    case W_VADD:
        vadd_free(&w->v);
        break;
    }
}

static int supports(const struct kernel *k, enum wkind kind) {
    switch (kind) {
    case W_TREE:
        return k->run_tree != NULL;
    case W_FLAT:
        return k->run_flat != NULL;
    case W_NESTED:
        return k->run_nested != NULL;
    case W_VADD:
        return k->run_vadd != NULL;
    }
    return 0;
}

static u64 run_once(const struct kernel *k, const struct workload *w, const struct kconfig *cfg) {
    switch (w->kind) {
    case W_TREE:
        return k->run_tree(w->t.root, cfg);
    case W_FLAT:
        return k->run_flat(&w->f, cfg);
    case W_NESTED:
        return k->run_nested(&w->a, cfg);
    case W_VADD:
        k->run_vadd(&w->v, cfg);
        return 0;
    }
    return 0;
}

static int verify(struct workload *w, u64 ret, struct result *r) {
    if (w->kind != W_VADD) {
        r->got = ret;
        return ret == w->sum;
    }
    r->bad = vadd_check(&w->v, &r->got);
    vadd_poison(&w->v);
    return r->bad == 0;
}

static void run_kernel(const struct kernel *k, struct workload *w, const struct hconfig *cfg,
                       double *times, struct result *r) {
    memset(r, 0, sizeof *r);
    if (!supports(k, w->kind)) {
        r->status = RUN_NA;
        return;
    }
    if (w->kind == W_TREE && k->max_depth && w->t.depth > k->max_depth) {
        r->status = RUN_SKIP;
        return;
    }
    if (k->init && k->init(&cfg->k)) {
        r->status = RUN_FAIL;
        return;
    }
    int ok = verify(w, run_once(k, w, &cfg->k), r);
    for (int i = 0; ok && i < cfg->reps; i++) {
        double t0 = now();
        u64 ret = run_once(k, w, &cfg->k);
        times[i] = now() - t0;
        ok = verify(w, ret, r);
    }
    if (k->fini)
        k->fini();
    if (!ok) {
        r->status = RUN_WRONG;
        return;
    }
    qsort(times, (size_t)cfg->reps, sizeof *times, cmp_double);
    int m = cfg->reps / 2;
    r->median = cfg->reps % 2 ? times[m] : (times[m - 1] + times[m]) / 2;
    r->min = times[0];
}

static void print_row(const struct kernel *k, const struct workload *w, const struct result *r,
                      const struct result *base) {
    printf("  %-12s ", k->name);
    if (r->status != RUN_OK) {
        printf("%10s %10s %8s %8s  ", "-", "-", "-", "-");
        if (r->status == RUN_SKIP)
            printf("skipped: depth %llu > %llu\n", ULL(w->t.depth), ULL(k->max_depth));
        else if (r->status == RUN_FAIL)
            printf("init failed\n");
        else if (r->status == RUN_NA)
            printf("no %s implementation\n", wnames[w->kind]);
        else if (w->kind == W_VADD)
            printf("WRONG: %llu of %llu elements\n", ULL(r->bad), ULL(w->v.n));
        else
            printf("WRONG: got %llu\n", ULL(r->got));
        return;
    }
    printf("%10.3f %10.3f ", r->median * 1e3, r->min * 1e3);
    if (w->items)
        printf("%8.2f ", r->median * 1e9 / (double)w->items);
    else
        printf("%8s ", "-");
    if (base->status == RUN_OK && r->median > 0)
        printf("%7.2fx", base->median / r->median);
    else
        printf("%8s", "-");
    printf("  ok\n");
}

static int width(const char *s, int min) {
    int l = (int)strlen(s);
    return l > min ? l : min;
}

static void print_summary(const struct tcase *cs, int ncases, const struct kernel **ks, int nks,
                          const struct result *res) {
    int namew = 7;
    for (int i = 0; i < ncases; i++)
        namew = width(cs[i].name, namew);
    char basehdr[64];
    snprintf(basehdr, sizeof basehdr, "%s ms", ks[0]->name);
    int basew = width(basehdr, 10);

    printf("\n== summary: %s in ms, others as speedup over %s\n", ks[0]->name, ks[0]->name);
    printf("%-*s  %*s", namew, "case", basew, basehdr);
    for (int j = 1; j < nks; j++)
        printf("  %*s", width(ks[j]->name, 8), ks[j]->name);
    printf("\n");

    for (int i = 0; i < ncases; i++) {
        const struct result *row = &res[i * nks];
        printf("%-*s  ", namew, cs[i].name);
        if (row[0].status == RUN_OK)
            printf("%*.3f", basew, row[0].median * 1e3);
        else
            printf("%*s", basew, labels[row[0].status]);
        for (int j = 1; j < nks; j++) {
            char cell[32] = "ok";
            if (row[j].status != RUN_OK)
                snprintf(cell, sizeof cell, "%s", labels[row[j].status]);
            else if (row[0].status == RUN_OK && row[j].median > 0)
                snprintf(cell, sizeof cell, "%.2fx", row[0].median / row[j].median);
            printf("  %*s", width(ks[j]->name, 8), cell);
        }
        printf("\n");
    }

    printf("%-*s  %*s", namew, "geomean", basew, "");
    for (int j = 1; j < nks; j++) {
        double logsum = 0;
        int n = 0;
        for (int i = 0; i < ncases; i++) {
            const struct result *base = &res[i * nks], *r = &res[i * nks + j];
            if (base->status == RUN_OK && r->status == RUN_OK && base->median > 0 &&
                r->median > 0) {
                logsum += log(base->median / r->median);
                n++;
            }
        }
        char cell[32] = "-";
        if (n)
            snprintf(cell, sizeof cell, "%.2fx", exp(logsum / n));
        printf("  %*s", width(ks[j]->name, 8), cell);
    }
    printf("\n");
}

int harness_run(const struct tcase *cs, int ncases, const struct kernel **ks, int nks,
                const struct hconfig *cfg) {
    struct result *res = xmalloc((size_t)(ncases * nks) * sizeof *res);
    double *times = xmalloc((size_t)cfg->reps * sizeof *times);
    int bad = 0;

    printf("threads %zu, heartbeat %llu, reps %d (+1 warmup)\n", cfg->k.nthreads,
           ULL(cfg->k.heartbeat), cfg->reps);

    for (int i = 0; i < ncases; i++) {
        const struct tcase *c = &cs[i];
        struct result *row = &res[i * nks];
        struct workload w;
        char err[512];

        if (strcmp(c->name, c->spec))
            printf("\n== %s: %s\n", c->name, c->spec);
        else
            printf("\n== %s\n", c->spec);
        fflush(stdout);
        double t0 = now();
        if (workload_build(&w, c, err, sizeof err)) {
            printf("  build failed: %s\n", err);
            for (int j = 0; j < nks; j++)
                row[j] = (struct result){.status = RUN_FAIL};
            bad = 1;
            continue;
        }
        workload_print(&w, now() - t0);
        if (c->has_expect && c->expect != w.sum) {
            printf("  EXPECTED sum %llu\n", ULL(c->expect));
            bad = 1;
        }
        printf("  %-12s %10s %10s %8s %8s  %s\n", "kernel", "median ms", "min ms", "ns/item",
               "speedup", "check");
        for (int j = 0; j < nks; j++) {
            run_kernel(ks[j], &w, cfg, times, &row[j]);
            if (row[j].status == RUN_WRONG || row[j].status == RUN_FAIL)
                bad = 1;
            print_row(ks[j], &w, &row[j], &row[0]);
            fflush(stdout);
        }
        workload_free(&w);
    }

    print_summary(cs, ncases, ks, nks, res);
    free(times);
    free(res);
    return bad ? -1 : 0;
}
