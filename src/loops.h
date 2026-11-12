#pragma once

#include <stddef.h>

#include "types.h"

struct array {
    u64 n;
    u32 *val;
    u64 sum;
};

struct vadd {
    u64 n;
    u64 *b;
    u64 *c;
    u64 *out;
    u64 work;
    u64 sum;
};

static inline u64 vadd_op(u64 b, u64 c, u64 work) {
    u64 x = b + c;
    for (u64 k = 0; k < work; k++)
        x = x * 6364136223846793005ULL + 1442695040888963407ULL;
    return x;
}

struct jagged {
    u64 rows;
    u64 *off;
    u32 *val;
    u64 items;
    u64 longest;
    u64 sum;
};

int array_is_kind(const char *kind);
int array_build(
    struct array *a, int argc, char **argv, char *err, size_t errlen);
void array_stats(struct array *a);
void array_free(struct array *a);

int jagged_is_kind(const char *kind);
int jagged_build(
    struct jagged *a, int argc, char **argv, char *err, size_t errlen);
void jagged_stats(struct jagged *a);
void jagged_free(struct jagged *a);

int vadd_is_kind(const char *kind);
int vadd_build(struct vadd *v, int argc, char **argv, char *err, size_t errlen);
void vadd_poison(struct vadd *v);
u64 vadd_check(const struct vadd *v, u64 *sum);
void vadd_free(struct vadd *v);
