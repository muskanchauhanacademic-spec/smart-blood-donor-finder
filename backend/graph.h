#ifndef GRAPH_H
#define GRAPH_H
#include "common.h"
#include "records.h"
typedef struct {
    int   numPatients;       /* rows */
    int   numDonors;         /* columns */
    int   edgeCount;
    int  *patientIds;        /* length numPatients; row i  <-> store->patients.items[i] at build time */
    int  *donorIds;          /* length numDonors;   col j  <-> store->donors.items[j]   at build time */
    int **adj;               /* numPatients x numDonors, calloc'd; adj[i][j] is 0 or 1 */
    int  *patientDegree;     /* row sums */
    int  *donorDegree;       /* column sums */
    char  cityFilter[CITY_LEN];   /* "" when no filter */
} BipartiteGraph;
void   graph_init(BipartiteGraph *g);       /* zero everything */
Status graph_build(BipartiteGraph *g, const RecordStore *store, const char *cityFilter);
        /* frees previous contents first; edge (i,j) iff compat_is_edge(...). If numPatients==0 or
           numDonors==0: adj/degree pointers stay NULL (still fill counts, ids). On failure g is empty. */
Status graph_compute_degrees(BipartiteGraph *g);   /* fills patientDegree/donorDegree from adj */
int    graph_has_edge(const BipartiteGraph *g, int patientRow, int donorCol);  /* bounds-safe, 0 if invalid */
void   graph_free(BipartiteGraph *g);
void   graph_write_json(StrBuf *sb, const BipartiteGraph *g, const RecordStore *store);          /* GET /api/graph */
void   graph_write_matrix_json(StrBuf *sb, const BipartiteGraph *g);                              /* GET /api/graph/matrix */
void   graph_write_degrees_json(StrBuf *sb, const BipartiteGraph *g, const RecordStore *store);  /* GET /api/graph/degrees */
#endif

