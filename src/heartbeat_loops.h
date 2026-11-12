#pragma once

#include "types.h"
#include "work_stealing.h"
#include <stddef.h>

typedef u64 loop_result;

typedef struct {
    u64 (*op)(u64 acc, u64 val);
    u64 id;
    u64 res;
} reduction;

/*
 * cur and high indicate the bounds of the loop
 * body is the loop body
 * data points to the array being looped over
 * result stores the result of applying body to each element of the array within
 * the bounds
 * tc is the number of live loops
 */
typedef struct {
    size_t cur, high;
    void (*body)(void *ctx, size_t i);
    void *ctx;
    _Atomic size_t *pending;
    job j;
} ld;

// TODO: need to find a way to make this generic/the responsibility of the
// application code
typedef struct {
    u64 *a;
    u64 *b;
    u64 *c;
    u64 work;
} body_arg;

typedef struct {
    void (*body)(void *arg);
    body_arg *arg;
    ld *ld;
} loop_arg;

typedef struct {
    u64 *arr;
    size_t id;
    u64 *res;
} sum_body_arg;

typedef struct {
    u64 *acc;
    u64 val;
} sum_reduce_arg;

void hb_loop(void *p);
