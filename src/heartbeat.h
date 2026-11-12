#pragma once

#include "work_stealing.h"
#include <stddef.h>

// ============================= PROMOTABLE JOB LIST
typedef struct jobref jobref;
struct jobref {
    job *j;
    jobref *next, *prev;
};
// ============================= HEARTBEAT
bool beat();
// ============================= FORK
void hb_fork(jobref *j);

// ============================= JOIN
void hb_join(jobref *jr);

// ============================= INIT/SHUTDOWN
void hb_runtime_init(size_t nt, u64 interval);
void hb_runtime_shutdown();
