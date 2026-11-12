#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "harness.h"
#include "util.h"

static void usage(FILE *f, const char *prog) {
    fprintf(f,
            "usage: %s [options]\n"
            "  -f FILE     test cases file (default tests/cases.txt)\n"
            "  -t A,B,...  only run these cases\n"
            "  -g SPEC     run an ad-hoc tree instead of the cases file (repeatable),\n"
            "              e.g. -g 'perfect 20'\n"
            "  -k A,B,...  kernels to run (default all)\n"
            "  -b NAME     baseline kernel for speedups, always run first (default: first kernel)\n"
            "  -r N        timed repetitions per kernel (default 5)\n"
            "  -p N        threads (default: online cores)\n"
            "  -H N        heartbeat parameter passed to kernels (default 1000)\n"
            "  -l          list kernels and cases, then exit\n"
            "\n"
            "cases file: one case per line, '#' starts a comment\n"
            "  NAME KIND ARGS... [expect=SUM]\n"
            "tree kinds (run by kernels with .run_tree):\n"
            "  file PATH                          preorder tokens: integer value, '.' empty\n"
            "                                     (PATH is relative to the cases file)\n"
            "  perfect HEIGHT [SEED]\n"
            "  random NODES [SEED]                each node added at the end of a random walk\n"
            "  chain NODES [left|right] [SEED]\n"
            "  chains HEIGHT PATHS LENGTH [SEED]  perfect tree plus random-direction paths\n"
            "  spine LENGTH HEIGHT [SEED]         left chain ending in a perfect tree\n"
            "flat loop kinds (run by kernels with .run_flat; sum of an array):\n"
            "  array N [SEED]\n"
            "vector add kinds (run by kernels with .run_vadd; out[i] = b[i] + c[i], checked per element):\n"
            "  vadd N [SEED] [WORK]               WORK extra multiply-adds per element (default 0)\n"
            "nested loop kinds (run by kernels with .run_nested; sum of a jagged 2D array):\n"
            "  rows ROWS LEN [SEED]               every row has LEN items\n"
            "  jagged ROWS MAXLEN [SEED]          row lengths uniform in [0, MAXLEN]\n"
            "  tri ROWS [SEED]                    row i has i items\n"
            "  spike ROWS LEN [SEED]              all rows empty except the middle one\n",
            prog);
}

static int parse_int(const char *s, long min, long *out) {
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno || end == s || *end || v < min)
        return -1;
    *out = v;
    return 0;
}

static int in_list(const char *list, const char *name) {
    size_t n = strlen(name);
    for (const char *p = list; *p;) {
        const char *comma = strchr(p, ',');
        size_t len = comma ? (size_t)(comma - p) : strlen(p);
        if (len == n && !strncmp(p, name, n))
            return 1;
        if (!comma)
            break;
        p = comma + 1;
    }
    return 0;
}

static int select_kernels(const char *list, const char *baseline, const struct kernel ***out) {
    const struct kernel **ks = xmalloc((size_t)(nkernels + 1) * sizeof *ks);
    int n = 0;
    const struct kernel *base = kernel_find(baseline);
    if (!base) {
        fprintf(stderr, "unknown kernel '%s'\n", baseline);
        free(ks);
        return -1;
    }
    ks[n++] = base;
    if (list) {
        char *copy = strcpy(xmalloc(strlen(list) + 1), list);
        for (char *tok = strtok(copy, ","); tok; tok = strtok(NULL, ",")) {
            const struct kernel *k = kernel_find(tok);
            if (!k) {
                fprintf(stderr, "unknown kernel '%s'\n", tok);
                free(copy);
                free(ks);
                return -1;
            }
            int dup = 0;
            for (int i = 0; i < n; i++)
                dup |= ks[i] == k;
            if (!dup)
                ks[n++] = k;
        }
        free(copy);
    } else {
        for (int i = 0; i < nkernels; i++)
            if (&kernels[i] != base)
                ks[n++] = &kernels[i];
    }
    *out = ks;
    return n;
}

int main(int argc, char **argv) {
    const char *cases_path = "tests/cases.txt";
    const char *case_filter = NULL, *kernel_list = NULL, *baseline = kernels[0].name;
    const char *gens[64];
    int ngens = 0, list = 0;
    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    struct hconfig cfg = {
        .k = {.nthreads = ncpu > 0 ? (int)ncpu : 1, .heartbeat = 1000},
        .reps = 5,
    };

    int opt;
    long v;
    while ((opt = getopt(argc, argv, "f:t:g:k:b:r:p:H:lh")) != -1) {
        switch (opt) {
        case 'f':
            cases_path = optarg;
            break;
        case 't':
            case_filter = optarg;
            break;
        case 'g':
            if (ngens == 64) {
                fprintf(stderr, "too many -g\n");
                return 2;
            }
            gens[ngens++] = optarg;
            break;
        case 'k':
            kernel_list = optarg;
            break;
        case 'b':
            baseline = optarg;
            break;
        case 'r':
            if (parse_int(optarg, 1, &v))
                goto bad_arg;
            cfg.reps = (int)v;
            break;
        case 'p':
            if (parse_int(optarg, 1, &v))
                goto bad_arg;
            cfg.k.nthreads = (int)v;
            break;
        case 'H':
            if (parse_int(optarg, 1, &v))
                goto bad_arg;
            cfg.k.heartbeat = (u64)v;
            break;
        case 'l':
            list = 1;
            break;
        case 'h':
            usage(stdout, argv[0]);
            return 0;
        default:
            usage(stderr, argv[0]);
            return 2;
        }
    }
    if (optind != argc) {
        usage(stderr, argv[0]);
        return 2;
    }

    const struct kernel **ks;
    int nks = select_kernels(kernel_list, baseline, &ks);
    if (nks < 0)
        return 2;

    struct tcase *cs;
    int ncases;
    if (ngens) {
        cs = xmalloc((size_t)ngens * sizeof *cs);
        for (ncases = 0; ncases < ngens; ncases++) {
            char *copy = strcpy(xmalloc(strlen(gens[ncases]) + 1), gens[ncases]);
            char *toks[64];
            int nt = 0;
            for (char *tok = strtok(copy, " \t"); tok && nt < 64; tok = strtok(NULL, " \t"))
                toks[nt++] = tok;
            int rc = case_init(&cs[ncases], gens[ncases], nt, toks, NULL);
            free(copy);
            if (rc) {
                fprintf(stderr, "bad spec '%s'\n", gens[ncases]);
                return 2;
            }
        }
    } else if (cases_load(cases_path, &cs, &ncases)) {
        return 2;
    }

    if (case_filter) {
        int kept = 0;
        for (int i = 0; i < ncases; i++) {
            if (in_list(case_filter, cs[i].name))
                cs[kept++] = cs[i];
            else
                case_free(&cs[i]);
        }
        ncases = kept;
    }

    if (list) {
        printf("kernels:\n");
        for (int i = 0; i < nkernels; i++) {
            const struct kernel *k = &kernels[i];
            char kinds[32] = "";
            if (k->run_tree)
                strcat(kinds, ",tree");
            if (k->run_flat)
                strcat(kinds, ",flat");
            if (k->run_nested)
                strcat(kinds, ",nested");
            if (k->run_vadd)
                strcat(kinds, ",vadd");
            printf("  %-12s %-18s %s\n", k->name, kinds[0] ? kinds + 1 : "-", k->desc);
        }
        printf("cases:\n");
        for (int i = 0; i < ncases; i++)
            printf("  %-12s %s%s\n", cs[i].name, cs[i].spec, cs[i].has_expect ? " (expect)" : "");
        return 0;
    }

    if (ncases == 0) {
        fprintf(stderr, "no cases selected\n");
        return 2;
    }

    int rc = harness_run(cs, ncases, ks, nks, &cfg);
    cases_free(cs, ncases);
    free(ks);
    return rc ? 1 : 0;

bad_arg:
    fprintf(stderr, "bad value for -%c: '%s'\n", opt, optarg);
    return 2;
}
