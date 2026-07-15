#ifndef _BS_GML_GC_H_
#define _BS_GML_GC_H_

#include <stddef.h>

struct Runner;

typedef struct {
    size_t arraysBefore;
    size_t structsBefore;
    size_t arraysCollected;
    size_t structsCollected;
} GMLGCStats;

// Collects unreachable GML arrays and structs, including reference cycles.
// Call only at a VM safe point (between bytecode/event executions).
GMLGCStats GMLGC_collect(struct Runner* runner);

#endif /* _BS_GML_GC_H_ */
