#include "tree.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

static struct node *new_node(struct node **next, u64 *rng) {
    struct node *n = (*next)++;
    n->val = (u32)(next_rand(rng) % 100);
    n->left = NULL;
    n->right = NULL;
    return n;
}

static struct node **child(struct node *n, u64 *rng) {
    return next_rand(rng) & 1 ? &n->right : &n->left;
}

static struct node **random_empty_slot(struct node **slot, u64 *rng) {
    while (*slot)
        slot = child(*slot, rng);
    return slot;
}

static struct node *build_perfect(struct node **next, u64 height, u64 *rng) {
    if (height == 0)
        return NULL;
    struct node *n = new_node(next, rng);
    n->left = build_perfect(next, height - 1, rng);
    n->right = build_perfect(next, height - 1, rng);
    return n;
}

static const char *scan(const char **p, size_t *len) {
    const char *s = *p;
    while (*s && isspace((unsigned char)*s))
        s++;
    if (!*s)
        return NULL;
    const char *e = s;
    while (*e && !isspace((unsigned char)*e))
        e++;
    *p = e;
    *len = (size_t)(e - s);
    return s;
}

static int is_null_token(const char *s, size_t len) {
    return len == 1 && *s == '.';
}

static char *read_file(const char *path, char *err, size_t errlen) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fail(err, errlen, "%s: %s", path, strerror(errno));
        return NULL;
    }
    size_t cap = 4096, len = 0;
    char *buf = xmalloc(cap);
    size_t got;
    while ((got = fread(buf + len, 1, cap - len - 1, f)) > 0) {
        len += got;
        if (cap - len - 1 == 0) {
            cap *= 2;
            buf = xrealloc(buf, cap);
        }
    }
    int bad = ferror(f);
    fclose(f);
    if (bad) {
        free(buf);
        fail(err, errlen, "%s: read error", path);
        return NULL;
    }
    buf[len] = 0;
    for (char *c = buf; *c; c++) {
        if (*c == '#')
            while (*c && *c != '\n')
                *c++ = ' ';
        if (!*c)
            break;
    }
    return buf;
}

static int tree_read(struct tree *t, const char *path, char *err, size_t errlen) {
    char *buf = read_file(path, err, errlen);
    if (!buf)
        return -1;

    u64 count = 0, tokens = 0;
    const char *p = buf, *s;
    size_t len;
    while ((s = scan(&p, &len))) {
        tokens++;
        if (!is_null_token(s, len))
            count++;
    }
    if (tokens == 0) {
        free(buf);
        return fail(err, errlen, "%s: empty file (use '.' for an empty tree)", path);
    }

    t->pool = xmalloc(count * sizeof *t->pool);
    struct node *next = t->pool;
    size_t cap = 64, top = 0;
    struct node ***slots = xmalloc(cap * sizeof *slots);
    slots[top++] = &t->root;
    int rc = 0;

    p = buf;
    u64 index = 0;
    while ((s = scan(&p, &len))) {
        index++;
        if (top == 0) {
            rc = fail(err, errlen, "%s: trailing tokens starting at token %llu", path,
                      (unsigned long long)index);
            break;
        }
        struct node **slot = slots[--top];
        if (is_null_token(s, len)) {
            *slot = NULL;
            continue;
        }
        char *end;
        errno = 0;
        unsigned long v = strtoul(s, &end, 10);
        if (errno || end != s + len || *s == '-' || v > UINT32_MAX) {
            rc = fail(err, errlen, "%s: bad token '%.*s' (token %llu)", path, (int)len, s,
                      (unsigned long long)index);
            break;
        }
        struct node *n = next++;
        n->val = (u32)v;
        n->left = NULL;
        n->right = NULL;
        *slot = n;
        if (top + 2 > cap) {
            cap *= 2;
            slots = xrealloc(slots, cap * sizeof *slots);
        }
        slots[top++] = &n->right;
        slots[top++] = &n->left;
    }
    if (rc == 0 && top != 0)
        rc = fail(err, errlen, "%s: truncated tree (%zu subtrees missing)", path, top);

    free(slots);
    free(buf);
    if (rc)
        tree_free(t);
    return rc;
}

int tree_build(struct tree *t, int argc, char **argv, char *err, size_t errlen) {
    memset(t, 0, sizeof *t);
    if (argc < 1)
        return fail(err, errlen, "empty tree spec");
    const char *kind = argv[0];

    if (!strcmp(kind, "file")) {
        if (argc != 2)
            return fail(err, errlen, "usage: file PATH");
        return tree_read(t, argv[1], err, errlen);
    }

    if (!strcmp(kind, "perfect")) {
        u64 height, seed;
        if (argc < 2 || argc > 3 || arg_u64(argc, argv, 1, 0, &height) ||
            arg_u64(argc, argv, 2, 1, &seed) || height > 32)
            return fail(err, errlen, "usage: perfect HEIGHT(<=32) [SEED]");
        t->pool = xmalloc((((u64)1 << height) - 1) * sizeof *t->pool);
        struct node *next = t->pool;
        t->root = build_perfect(&next, height, &seed);
        return 0;
    }

    if (!strcmp(kind, "random")) {
        u64 n, seed;
        if (argc < 2 || argc > 3 || arg_u64(argc, argv, 1, 0, &n) ||
            arg_u64(argc, argv, 2, 1, &seed))
            return fail(err, errlen, "usage: random NODES [SEED]");
        t->pool = xmalloc(n * sizeof *t->pool);
        struct node *next = t->pool;
        for (u64 i = 0; i < n; i++)
            *random_empty_slot(&t->root, &seed) = new_node(&next, &seed);
        return 0;
    }

    if (!strcmp(kind, "chain")) {
        u64 n, seed;
        int right = 0;
        if (argc >= 3) {
            if (!strcmp(argv[2], "right"))
                right = 1;
            else if (strcmp(argv[2], "left"))
                argc = 0;
        }
        if (argc < 2 || argc > 4 || arg_u64(argc, argv, 1, 0, &n) ||
            arg_u64(argc, argv, 3, 1, &seed))
            return fail(err, errlen, "usage: chain NODES [left|right] [SEED]");
        t->pool = xmalloc(n * sizeof *t->pool);
        struct node *next = t->pool;
        struct node **slot = &t->root;
        for (u64 i = 0; i < n; i++) {
            struct node *nd = new_node(&next, &seed);
            *slot = nd;
            slot = right ? &nd->right : &nd->left;
        }
        return 0;
    }

    if (!strcmp(kind, "chains")) {
        u64 height, paths, length, seed;
        if (argc < 4 || argc > 5 || arg_u64(argc, argv, 1, 0, &height) || height > 32 ||
            arg_u64(argc, argv, 2, 0, &paths) || arg_u64(argc, argv, 3, 0, &length) ||
            arg_u64(argc, argv, 4, 1, &seed))
            return fail(err, errlen, "usage: chains HEIGHT(<=32) PATHS LENGTH [SEED]");
        t->pool = xmalloc(((((u64)1 << height) - 1) + paths * length) * sizeof *t->pool);
        struct node *next = t->pool;
        t->root = build_perfect(&next, height, &seed);
        for (u64 i = 0; i < paths; i++) {
            struct node **slot = random_empty_slot(&t->root, &seed);
            for (u64 j = 0; j < length; j++) {
                struct node *nd = new_node(&next, &seed);
                *slot = nd;
                slot = child(nd, &seed);
            }
        }
        return 0;
    }

    if (!strcmp(kind, "spine")) {
        u64 length, height, seed;
        if (argc < 3 || argc > 4 || arg_u64(argc, argv, 1, 0, &length) ||
            arg_u64(argc, argv, 2, 0, &height) || height > 32 || arg_u64(argc, argv, 3, 1, &seed))
            return fail(err, errlen, "usage: spine LENGTH HEIGHT(<=32) [SEED]");
        t->pool = xmalloc((length + ((u64)1 << height) - 1) * sizeof *t->pool);
        struct node *next = t->pool;
        struct node **slot = &t->root;
        for (u64 i = 0; i < length; i++) {
            struct node *nd = new_node(&next, &seed);
            *slot = nd;
            slot = &nd->left;
        }
        *slot = build_perfect(&next, height, &seed);
        return 0;
    }

    return fail(err, errlen, "unknown tree kind '%s'", kind);
}

void tree_stats(struct tree *t) {
    t->nodes = 0;
    t->depth = 0;
    t->sum = 0;
    if (!t->root)
        return;
    struct frame {
        struct node *n;
        u64 depth;
    };
    size_t cap = 64, top = 0;
    struct frame *stack = xmalloc(cap * sizeof *stack);
    stack[top++] = (struct frame){t->root, 1};
    while (top) {
        struct frame f = stack[--top];
        t->nodes++;
        t->sum += f.n->val;
        if (f.depth > t->depth)
            t->depth = f.depth;
        if (top + 2 > cap) {
            cap *= 2;
            stack = xrealloc(stack, cap * sizeof *stack);
        }
        if (f.n->right)
            stack[top++] = (struct frame){f.n->right, f.depth + 1};
        if (f.n->left)
            stack[top++] = (struct frame){f.n->left, f.depth + 1};
    }
    free(stack);
}

void tree_free(struct tree *t) {
    free(t->pool);
    memset(t, 0, sizeof *t);
}
