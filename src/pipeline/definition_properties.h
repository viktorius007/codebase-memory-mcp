#ifndef CBM_PIPELINE_DEFINITION_PROPERTIES_H
#define CBM_PIPELINE_DEFINITION_PROPERTIES_H

#include "cbm.h"

#include <stddef.h>

/* Definition properties are persisted on every source node and consumed by
 * semantic indexing. Keep the envelope bounded, but never make its contents
 * depend on which optional field happened to fit first. */
enum { CBM_DEF_PROPERTIES_MAX_BYTES = 32768 };

typedef enum {
    CBM_DEF_PROPERTIES_OK = 0,
    CBM_DEF_PROPERTIES_OVERSIZE,
    CBM_DEF_PROPERTIES_ALLOCATION_UNAVAILABLE,
    CBM_DEF_PROPERTIES_INVALID,
} cbm_def_properties_status_t;

typedef struct {
    char *json;
    size_t length;
} cbm_def_properties_t;

cbm_def_properties_status_t cbm_def_properties_build(const CBMDefinition *def,
                                                     cbm_def_properties_t *out);
void cbm_def_properties_destroy(cbm_def_properties_t *properties);
const char *cbm_def_properties_status_name(cbm_def_properties_status_t status);

#ifdef CBM_ENABLE_TEST_SEAMS
void cbm_def_properties_test_fail_allocation_once(void);
#endif

#endif
