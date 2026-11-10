#ifndef MATCHING_H
#define MATCHING_H
#include "common.h"
#include "records.h"
#include "graph.h"
typedef struct {
    int  numPatients;
    int  numDonors;
    int  matchCount;
    int *patientMatch;   /* length numPatients: matched donor COLUMN or -1 */
    int *donorMatch;     /* length numDonors:   matched patient ROW or -1 */
} MatchingResult;
void   matching_init(MatchingResult *m);
Status matching_compute(const BipartiteGraph *g, MatchingResult *m);   /* Kuhn; recursive DFS; handles empty graph */
void   matching_free(MatchingResult *m);
void   matching_write_json(StrBuf *sb, const MatchingResult *m, const BipartiteGraph *g, const RecordStore *store);
#endif

