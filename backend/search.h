#ifndef SEARCH_H
#define SEARCH_H
#include "common.h"
#include "records.h"
typedef struct { Donor *donors; int count; } DonorSearchResult;   /* donors: malloc'd COPIES; NULL when count==0 */
Status search_compatible_donors(const RecordStore *store, int patientId, const char *cityFilter,
                                int availableOnly, DonorSearchResult *out);
        /* NOT_FOUND if patient absent. Match = compat_can_donate && compat_city_matches
           && (availableOnly ? available : 1). Two passes: count, then malloc exact size. */
void    search_result_free(DonorSearchResult *r);
#endif

