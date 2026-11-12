#include "loops.h"

#include <string.h>

#include "util.h"

static void fill(u32 *val, u64 n, u64 seed) {
    for (u64 i = 0; i < n; i++)
        val[i] = (u32)(next_rand(&seed) % 100);
}

int array_is_kind(const char *kind) { return !strcmp(kind, "array"); }

int array_build(struct array *a, int argc, char **argv, char *err, size_t errlen) {
    memset(a, 0, sizeof *a);
    u64 seed;
    if (argc < 2 || argc > 3 || strcmp(argv[0], "array") || arg_u64(argc, argv, 1, 0, &a->n) ||
        arg_u64(argc, argv, 2, 1, &seed))
        return fail(err, errlen, "usage: array N [SEED]");
    a->val = xmalloc(a->n * sizeof *a->val);
    fill(a->val, a->n, seed);
    return 0;
}

void array_stats(struct array *a) {
    a->sum = 0;
    for (u64 i = 0; i < a->n; i++)
        a->sum += a->val[i];
}

void array_free(struct array *a) {
    free(a->val);
    memset(a, 0, sizeof *a);
}

static const char *kinds[] = {"rows", "jagged", "tri", "spike"};

int jagged_is_kind(const char *kind) {
    for (size_t i = 0; i < sizeof kinds / sizeof kinds[0]; i++)
        if (!strcmp(kind, kinds[i]))
            return 1;
    return 0;
}

int jagged_build(struct jagged *a, int argc, char **argv, char *err, size_t errlen) {
    memset(a, 0, sizeof *a);
    if (argc < 1)
        return fail(err, errlen, "empty loop spec");
    const char *kind = argv[0];
    u64 rows, len = 0, seed;

    if (!strcmp(kind, "rows")) {
        if (argc < 3 || argc > 4 || arg_u64(argc, argv, 1, 0, &rows) ||
            arg_u64(argc, argv, 2, 0, &len) || arg_u64(argc, argv, 3, 1, &seed))
            return fail(err, errlen, "usage: rows ROWS LEN [SEED]");
    } else if (!strcmp(kind, "jagged")) {
        if (argc < 3 || argc > 4 || arg_u64(argc, argv, 1, 0, &rows) ||
            arg_u64(argc, argv, 2, 0, &len) || arg_u64(argc, argv, 3, 1, &seed))
            return fail(err, errlen, "usage: jagged ROWS MAXLEN [SEED]");
    } else if (!strcmp(kind, "tri")) {
        if (argc < 2 || argc > 3 || arg_u64(argc, argv, 1, 0, &rows) ||
            arg_u64(argc, argv, 2, 1, &seed))
            return fail(err, errlen, "usage: tri ROWS [SEED]");
    } else if (!strcmp(kind, "spike")) {
        if (argc < 3 || argc > 4 || arg_u64(argc, argv, 1, 0, &rows) || rows == 0 ||
            arg_u64(argc, argv, 2, 0, &len) || arg_u64(argc, argv, 3, 1, &seed))
            return fail(err, errlen, "usage: spike ROWS(>0) LEN [SEED]");
    } else {
        return fail(err, errlen, "unknown loop kind '%s'", kind);
    }

    a->rows = rows;
    a->off = xmalloc((rows + 1) * sizeof *a->off);
    a->off[0] = 0;
    u64 rng = seed ^ 0x5bd1e995ULL;
    for (u64 i = 0; i < rows; i++) {
        u64 n;
        switch (kind[0]) {
        case 'r':
            n = len;
            break;
        case 'j':
            n = next_rand(&rng) % (len + 1);
            break;
        case 't':
            n = i;
            break;
        default:
            n = i == rows / 2 ? len : 0;
            break;
        }
        a->off[i + 1] = a->off[i] + n;
    }
    a->val = xmalloc(a->off[rows] * sizeof *a->val);
    fill(a->val, a->off[rows], seed);
    return 0;
}

void jagged_stats(struct jagged *a) {
    a->items = a->off ? a->off[a->rows] : 0;
    a->longest = 0;
    a->sum = 0;
    for (u64 i = 0; i < a->rows; i++) {
        u64 n = a->off[i + 1] - a->off[i];
        if (n > a->longest)
            a->longest = n;
    }
    for (u64 i = 0; i < a->items; i++)
        a->sum += a->val[i];
}

void jagged_free(struct jagged *a) {
    free(a->off);
    free(a->val);
    memset(a, 0, sizeof *a);
}

int vadd_is_kind(const char *kind) { return !strcmp(kind, "vadd"); }

int vadd_build(struct vadd *v, int argc, char **argv, char *err, size_t errlen) {
    memset(v, 0, sizeof *v);
    u64 seed;
    if (argc < 2 || argc > 4 || strcmp(argv[0], "vadd") || arg_u64(argc, argv, 1, 0, &v->n) ||
        arg_u64(argc, argv, 2, 1, &seed) || arg_u64(argc, argv, 3, 0, &v->work))
        return fail(err, errlen, "usage: vadd N [SEED] [WORK]");
    v->b = xmalloc(v->n * sizeof *v->b);
    v->c = xmalloc(v->n * sizeof *v->c);
    v->out = xmalloc(v->n * sizeof *v->out);
    for (u64 i = 0; i < v->n; i++) {
        v->b[i] = next_rand(&seed) % 100;
        v->c[i] = next_rand(&seed) % 100;
        v->sum += vadd_op(v->b[i], v->c[i], v->work);
    }
    vadd_poison(v);
    return 0;
}

void vadd_poison(struct vadd *v) { memset(v->out, 0xff, v->n * sizeof *v->out); }

u64 vadd_check(const struct vadd *v, u64 *sum) {
    u64 bad = 0, s = 0;
    for (u64 i = 0; i < v->n; i++) {
        s += v->out[i];
        bad += v->out[i] != vadd_op(v->b[i], v->c[i], v->work);
    }
    *sum = s;
    return bad;
}

void vadd_free(struct vadd *v) {
    free(v->b);
    free(v->c);
    free(v->out);
    memset(v, 0, sizeof *v);
}
