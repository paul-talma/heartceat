#include "kernels.h"
#include "forkjoin.h"
#include "heartbeat.h"
#include "heartbeat_loops.h"
#include <string.h>

#define ARRAY_SIZE (1 << 20)
#define MAX_DEPTH 8

typedef struct {
    struct node *node;
    size_t depth;
    size_t max_depth;
    u64 result;
} sum_arg;

// =========================================== SERIAL

static u64 run_serial(struct node *root, const struct kconfig *cfg) {
    (void)cfg;
    if (!root)
        return 0;
    return run_serial(root->left, NULL) + run_serial(root->right, NULL) +
           root->val;
}

// =========================================== FORK-JOIN
static void fj_sum_job(void *p);
static u64 fj_sum(struct node *node, size_t depth, size_t max_depth);

// closure body
// unpacks args
static void fj_sum_job(void *p) {
    sum_arg *a = p;
    a->result = fj_sum(a->node, a->depth, a->max_depth);
}

static u64 fj_sum(struct node *node, size_t depth, size_t max_depth) {
    if (!node)
        return 0;
    if (max_depth <= depth) {
        return run_serial(node, NULL);
    }
    if (!node->right) { // don't fork on NULL branch
        return fj_sum(node->left, depth + 1, max_depth) + node->val;
    }
    // create job
    sum_arg a = {
        .node = node->right,
        .depth = depth + 1,
        .max_depth = max_depth,
        .result = 0,
    };
    job j = {
        .fn = &fj_sum_job,
        .arg = &a,
        .done = 0,
    };
    // fork, sum left, join
    fj_fork(&j);
    u64 left_sum = fj_sum(node->left, depth + 1, max_depth);
    fj_join(&j);
    return left_sum + a.result + node->val;
}

static u64 run_fork_join(struct node *root, const struct kconfig *cfg) {
    fj_init(cfg->nthreads);
    u64 sum = fj_sum(root, 0, MAX_DEPTH);
    fj_shutdown();
    return sum;
}

// =========================================== HEARTBEAT
static void hb_sum_job(void *p);
static u64 hb_sum(struct node *node);

static void hb_sum_job(void *p) {
    sum_arg *a = p;
    a->result = hb_sum(a->node);
}

static u64 hb_sum(struct node *node) {
    if (!node)
        return 0;
    if (!node->right) { // fork only if right child !NULL
        return node->val + hb_sum(node->left);
    }
    sum_arg a = {
        .node = node->right,
        .result = 0,
    };
    job j = {
        .fn = &hb_sum_job,
        .arg = &a,
        .done = 0,
    };
    // fork, sum left, join
    jobref jr = {
        .j = &j,
        .next = NULL,
        .prev = NULL,
    };
    hb_fork(&jr);
    u64 left_sum = hb_sum(node->left);
    hb_join(&jr);
    return left_sum + a.result + node->val;
}

static u64 run_heartbeat(struct node *root, const struct kconfig *cfg) {
    hb_runtime_init(cfg->nthreads, cfg->heartbeat);
    u64 sum = hb_sum(root);
    hb_runtime_shutdown();
    return sum;
}

// =========================================== LOOPS
// =========================================== SERIAL

static u64 run_serial_flat_loop(const struct array *a,
                                const struct kconfig *cfg) {
    u64 sum = 0;
    for (size_t i = 0; i < a->n; ++i) {
        sum += a->val[i];
    }
    return sum;
}

// =========================================== HB
static void body(void *arg_, size_t i) {
    body_arg *arg = arg_;
    u64 *a = arg->a;
    u64 *b = arg->b;
    u64 *c = arg->c;
    a[i] = vadd_op(b[i], c[i], arg->work);
}

static void run_serial_vadd(const struct vadd *v, const struct kconfig *cfg) {
    (void)cfg;
    for (u64 i = 0; i < v->n; ++i)
        v->out[i] = vadd_op(v->b[i], v->c[i], v->work);
}

static void run_heartbeat_vadd(const struct vadd *v,
                               const struct kconfig *cfg) {
    body_arg ctx = {
        .a = v->out,
        .b = v->b,
        .c = v->c,
        .work = v->work,
    };
    _Atomic size_t pending = 1;
    ld ld = {
        .cur = 0,
        .high = v->n,
        .body = body,
        .ctx = &ctx,
        .pending = &pending,
    };
    hb_runtime_init(cfg->nthreads, cfg->heartbeat);
    hb_loop(&ld);
    hb_runtime_shutdown();
}

// =========================================== KERNELS

const struct kernel kernels[] =
    {
        {.name = "serial",
         .desc = "computes the sum serially",
         .run_tree = run_serial,
         .max_depth = 100000},
        {.name = "fork-join",
         .desc = "computes the sum in parallel using fork join",
         .run_tree = run_fork_join,
         .max_depth = 100000},
        {.name = "heartbeat",
         .desc = "computes the sum in parallel using heartbeat scheduling",
         .run_tree = run_heartbeat,
         .max_depth = 100000},
        {.name = "serial_flat_loop",
         .desc = "sums an array serially",
         .run_flat = run_serial_flat_loop},
        {.name = "serial_vadd",
         .desc = "vector addition, serial",
         .run_vadd = run_serial_vadd},
        {.name = "heartbeat_vadd",
         .desc = "vector addition, heartbeat loop",
         .run_vadd = run_heartbeat_vadd},
}

;

const int nkernels = sizeof kernels / sizeof kernels[0];

const struct kernel *kernel_find(const char *name) {
    for (int i = 0; i < nkernels; i++)
        if (!strcmp(kernels[i].name, name))
            return &kernels[i];
    return NULL;
}
