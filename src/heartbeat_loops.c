#include "heartbeat_loops.h"
#include "heartbeat.h"
#include "work_stealing.h"
#include <stdlib.h>

#define RESULTS_ARRAY_SIZE 64

// ============================= LOOP DESCRIPTORS
typedef struct {
    ld *ld, *next, *prev;
} ld_node;

typedef struct {
    ld_node *head, *tail;
} descriptor_deque;

/*
 * Executes the loop described by loop descriptor p.
 * On a heartbeat, splits the current loop descriptor, registers a pending loop,
 * and continues running the lower half.
 */
static void run(void *p) {
    ld *d = p;
    for (size_t i = d->cur; i < d->high; ++i) {
        d->body(d->ctx, i);
        if (beat() && d->high - i > 1) {
            // TODO: need to free these
            ld *u = malloc(sizeof(*u)); // heap allocated lds
            *u = *d;
            u->cur = (i + 1 + d->high) / 2;
            d->high = u->cur;
            u->j = (job){
                .fn = run,
                .arg = u,
            };
            atomic_fetch_add(d->pending, 1);
            push_ws(&u->j);
        }
    }
    atomic_fetch_sub(d->pending, 1);
}

/*
 * Loop runner. Runs the current loop descriptor and picks up jobs when done.
 */
void hb_loop(void *p) {
    ld *d = p;
    run(d);
    while (0 < *d->pending) {
        ws_do_work();
    }
}
