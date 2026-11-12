#include "kernels.h"
#include "forkjoin.h"
#include "heartbeat.h"
#include <string.h>

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

// =========================================== KERNELS

const struct kernel kernels[] =
    {{.name = "serial",
      .desc = "computes the sum serially",
      .run = run_serial,
      .max_depth = 100000},
     {.name = "fork-join",
      .desc = "computes the sum in parallel using fork join",
      .run = run_fork_join,
      .max_depth = 100000},
     {.name = "heartbeat",
      .desc = "computes the sum in parallel using heartbeat scheduling",
      .run = run_heartbeat,
      .max_depth = 100000}}

;

const int nkernels = sizeof kernels / sizeof kernels[0];

const struct kernel *kernel_find(const char *name) {
    for (int i = 0; i < nkernels; i++)
        if (!strcmp(kernels[i].name, name))
            return &kernels[i];
    return NULL;
}
