#pragma once

#include "loops.h"
#include "tree.h"

struct kconfig {
    size_t nthreads;
    u64 heartbeat;
};

typedef u64 tree_kernel(struct node *root, const struct kconfig *cfg);
typedef u64 flat_kernel(const struct array *a, const struct kconfig *cfg);
typedef u64 nested_kernel(const struct jagged *a, const struct kconfig *cfg);
typedef void vadd_kernel(const struct vadd *v, const struct kconfig *cfg);

struct kernel {
    const char *name;
    const char *desc;
    tree_kernel *run_tree;
    flat_kernel *run_flat;
    nested_kernel *run_nested;
    vadd_kernel *run_vadd;
    int (*init)(const struct kconfig *cfg);
    void (*fini)(void);
    u64 max_depth;
};

extern const struct kernel kernels[];
extern const int nkernels;

const struct kernel *kernel_find(const char *name);
