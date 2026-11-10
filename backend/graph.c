/*
 * graph.c - The bipartite graph of "Smart Blood Donor Finder".
 *
 * MATHEMATICAL MODEL
 * ------------------
 * We build a bipartite graph G = (P U D, E):
 *     P = the set of patients   (one ROW    of the adjacency matrix per patient)
 *     D = the set of donors     (one COLUMN of the adjacency matrix per donor)
 *     E = the set of edges      (patient i, donor j)
 * "Bipartite" means every edge joins a patient to a donor; there are never
 * edges between two patients or between two donors.
 *
 * The adjacency matrix is a table of 0/1 numbers:
 *     adj[i][j] = 1   if donor j can give blood to patient i (an edge),
 *     adj[i][j] = 0   otherwise.
 * Whether an edge exists is decided ONLY by compat_is_edge() (compatibility.c).
 * This file never re-implements that rule.
 *
 * DEGREES
 * -------
 * The degree of a vertex is the number of edges touching it:
 *     degree(patient i) = sum over j of adj[i][j]     (sum of ROW i)
 *     degree(donor   j) = sum over i of adj[i][j]     (sum of COLUMN j)
 * Handshake lemma for bipartite graphs: every edge has exactly one patient end
 * and one donor end, so
 *     (sum of all patient degrees) = (sum of all donor degrees) = |E|.
 *
 * MEMORY
 * ------
 * All arrays are allocated with calloc (so they start as zeros) and are owned
 * by the BipartiteGraph. graph_free() releases everything. A graph is only
 * valid while the RecordStore it was built from is NOT modified.
 */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "graph.h"
#include "compatibility.h"

/* ------------------------------------------------------------------------ */
/* Small private helpers                                                    */
/* ------------------------------------------------------------------------ */

/*
 * Read array[index], but treat a NULL array as "all zeros".
 * Why: for a graph with 0 patients or 0 donors the degree arrays stay NULL
 * (we never malloc(0)), yet the JSON writers must still print degree 0.
 */
static int int_at(const int *array, int index)
{
    if (array == NULL) {
        return 0;
    }
    return array[index];
}

/* Add the sum of array[0..count-1] (NULL array counts as zeros). */
static int sum_of(const int *array, int count)
{
    int i;
    int total = 0;
    for (i = 0; i < count; i++) {
        total += int_at(array, i);
    }
    return total;
}

/* Write a JSON list of numbers, for example [2,1,2]. */
static void write_int_list(StrBuf *sb, const int *array, int count)
{
    int i;
    sb_append_char(sb, '[');
    for (i = 0; i < count; i++) {
        if (i > 0) {
            sb_append_char(sb, ',');
        }
        sb_appendf(sb, "%d", int_at(array, i));
    }
    sb_append_char(sb, ']');
}

/* Write  "cityFilter":"Pune"  (no leading or trailing comma). */
static void write_city_filter(StrBuf *sb, const BipartiteGraph *g)
{
    sb_append(sb, "\"cityFilter\":");
    sb_append_json_string(sb, g->cityFilter);
}

/* One patient vertex for /api/graph: patient fields plus "degree". */
static void write_patient_vertex(StrBuf *sb, const Patient *p, int degree)
{
    sb_appendf(sb, "{\"id\":%d,\"name\":", p->id);
    sb_append_json_string(sb, p->name);
    sb_append(sb, ",\"bloodGroup\":");
    sb_append_json_string(sb, p->bloodGroup);
    sb_append(sb, ",\"city\":");
    sb_append_json_string(sb, p->city);
    sb_appendf(sb, ",\"degree\":%d}", degree);
}

/* One donor vertex for /api/graph: donor fields plus "available" and "degree". */
static void write_donor_vertex(StrBuf *sb, const Donor *d, int degree)
{
    sb_appendf(sb, "{\"id\":%d,\"name\":", d->id);
    sb_append_json_string(sb, d->name);
    sb_append(sb, ",\"bloodGroup\":");
    sb_append_json_string(sb, d->bloodGroup);
    sb_append(sb, ",\"city\":");
    sb_append_json_string(sb, d->city);
    sb_appendf(sb, ",\"available\":%s,\"degree\":%d}",
               d->available ? "true" : "false", degree);
}

/* ------------------------------------------------------------------------ */
/* Life cycle: init / free                                                  */
/* ------------------------------------------------------------------------ */

void graph_init(BipartiteGraph *g)
{
    if (g == NULL) {
        return;
    }
    /* Set every field by hand so that every pointer is a real NULL. */
    g->numPatients   = 0;
    g->numDonors     = 0;
    g->edgeCount     = 0;
    g->patientIds    = NULL;
    g->donorIds      = NULL;
    g->adj           = NULL;
    g->patientDegree = NULL;
    g->donorDegree   = NULL;
    g->cityFilter[0] = '\0';
}

void graph_free(BipartiteGraph *g)
{
    int i;

    if (g == NULL) {
        return;
    }

    /* adj is an array of row pointers. Free every row first, then the array
       of pointers itself. free(NULL) is harmless, so a half-built graph
       (some rows still NULL after a failed calloc) is also handled. */
    if (g->adj != NULL) {
        for (i = 0; i < g->numPatients; i++) {
            free(g->adj[i]);
        }
        free(g->adj);
    }
    free(g->patientIds);
    free(g->donorIds);
    free(g->patientDegree);
    free(g->donorDegree);

    /* Reset to the empty state: pointers become NULL, so a second call to
       graph_free() does nothing (idempotent) and nothing dangles. */
    graph_init(g);
}

/* ------------------------------------------------------------------------ */
/* Degrees                                                                  */
/* ------------------------------------------------------------------------ */

Status graph_compute_degrees(BipartiteGraph *g)
{
    int i;
    int j;

    if (g == NULL) {
        return STATUS_ERR_INTERNAL;
    }

    /* Allow calling this twice: throw away old results first. */
    free(g->patientDegree);
    g->patientDegree = NULL;
    free(g->donorDegree);
    g->donorDegree = NULL;

    /* Empty graph (or one side empty): no matrix, so no degree arrays.
       The JSON writers read a NULL degree array as zeros. */
    if (g->adj == NULL || g->numPatients <= 0 || g->numDonors <= 0) {
        return STATUS_OK;
    }

    /* calloc(count, size) checks count*size for overflow by itself and gives
       zero-filled memory, which is what we need to start counting. */
    g->patientDegree = calloc((size_t)g->numPatients, sizeof(int));
    g->donorDegree   = calloc((size_t)g->numDonors, sizeof(int));
    if (g->patientDegree == NULL || g->donorDegree == NULL) {
        free(g->patientDegree);
        g->patientDegree = NULL;
        free(g->donorDegree);
        g->donorDegree = NULL;
        return STATUS_ERR_NOMEM;
    }

    /* Walk over the whole matrix once. Because adj[i][j] is 0 or 1, adding it
       is the same as "add 1 if there is an edge":
           patientDegree[i] = adj[i][0] + adj[i][1] + ... + adj[i][numDonors-1]
                              (the sum of ROW i)
           donorDegree[j]   = adj[0][j] + adj[1][j] + ... + adj[numPatients-1][j]
                              (the sum of COLUMN j)  */
    for (i = 0; i < g->numPatients; i++) {
        for (j = 0; j < g->numDonors; j++) {
            g->patientDegree[i] += g->adj[i][j];
            g->donorDegree[j]   += g->adj[i][j];
        }
    }
    return STATUS_OK;
}

/* ------------------------------------------------------------------------ */
/* Building the graph                                                       */
/* ------------------------------------------------------------------------ */

Status graph_build(BipartiteGraph *g, const RecordStore *store, const char *cityFilter)
{
    int    i;
    int    j;
    int    n;                       /* number of patients (rows)  */
    int    m;                       /* number of donors (columns) */
    Status st = STATUS_ERR_NOMEM;   /* error code used if we jump to 'fail' */

    if (g == NULL) {
        return STATUS_ERR_INTERNAL;
    }

    /* Always start from an empty graph, so rebuilding never leaks memory. */
    graph_free(g);

    if (store == NULL) {
        return STATUS_ERR_INTERNAL;
    }

    /* Read the sizes. A NULL items pointer means "no records" even if a count
       looks positive, so we never dereference a NULL array. */
    n = (store->patients.items != NULL && store->patients.count > 0)
            ? store->patients.count : 0;
    m = (store->donors.items != NULL && store->donors.count > 0)
            ? store->donors.count : 0;
    g->numPatients = n;
    g->numDonors   = m;

    /* Copy the city filter (NULL means "no filter" = empty text).
       snprintf never writes more than sizeof(g->cityFilter) bytes and always
       adds the final '\0', so an over-long filter is cut short safely.
       The API layer rejects over-long filters before they reach this point.
       Edges below are computed with THIS stored copy, so the filter that is
       reported in the JSON is exactly the filter that was applied. */
    snprintf(g->cityFilter, sizeof g->cityFilter, "%s",
             cityFilter != NULL ? cityFilter : "");

    /* Copy the ids so the graph still knows which row/column is which
       patient/donor. Only allocate when there is something to store
       (never allocate zero bytes). */
    if (n > 0) {
        g->patientIds = calloc((size_t)n, sizeof(int));
        if (g->patientIds == NULL) {
            goto fail;
        }
        for (i = 0; i < n; i++) {
            g->patientIds[i] = store->patients.items[i].id;
        }
    }
    if (m > 0) {
        g->donorIds = calloc((size_t)m, sizeof(int));
        if (g->donorIds == NULL) {
            goto fail;
        }
        for (j = 0; j < m; j++) {
            g->donorIds[j] = store->donors.items[j].id;
        }
    }

    /* The matrix exists only if BOTH sides are non-empty.
       Step 1: one array of n row pointers (all start as NULL).
       Step 2: one zero-filled row of m ints for every patient.
       If any step fails, 'goto fail' calls graph_free(), which frees the rows
       that were already allocated and then the array of row pointers. */
    if (n > 0 && m > 0) {
        g->adj = calloc((size_t)n, sizeof(int *));
        if (g->adj == NULL) {
            goto fail;
        }
        for (i = 0; i < n; i++) {
            g->adj[i] = calloc((size_t)m, sizeof(int));
            if (g->adj[i] == NULL) {
                goto fail;
            }
        }

        /* Fill the matrix: ask the single edge rule about every pair
           (patient i, donor j) and count the edges as we go. */
        for (i = 0; i < n; i++) {
            for (j = 0; j < m; j++) {
                if (compat_is_edge(&store->donors.items[j],
                                   &store->patients.items[i],
                                   g->cityFilter)) {
                    if (g->edgeCount == INT_MAX) {   /* cannot count higher */
                        st = STATUS_ERR_INTERNAL;
                        goto fail;
                    }
                    g->adj[i][j] = 1;
                    g->edgeCount++;
                }
            }
        }
    }

    /* Fill the degree arrays so every caller (and every JSON writer) can rely
       on them right after a successful build. */
    st = graph_compute_degrees(g);
    if (st != STATUS_OK) {
        goto fail;
    }
    return STATUS_OK;

fail:
    /* On any failure the graph is left completely empty. */
    graph_free(g);
    return st;
}

int graph_has_edge(const BipartiteGraph *g, int patientRow, int donorCol)
{
    if (g == NULL || g->adj == NULL) {
        return 0;
    }
    if (patientRow < 0 || patientRow >= g->numPatients) {
        return 0;
    }
    if (donorCol < 0 || donorCol >= g->numDonors) {
        return 0;
    }
    return g->adj[patientRow][donorCol] != 0 ? 1 : 0;
}

/* ------------------------------------------------------------------------ */
/* JSON writers (formats are defined in section H of the specification)     */
/*                                                                          */
/* Row i of the matrix is store->patients.items[i] and column j is          */
/* store->donors.items[j]. This is valid because the store does not change  */
/* between graph_build() and the writing inside one request.                */
/* ------------------------------------------------------------------------ */

/* GET /api/graph : vertices with degrees, and the edge list. */
void graph_write_json(StrBuf *sb, const BipartiteGraph *g, const RecordStore *store)
{
    BipartiteGraph emptyGraph;
    int i;
    int j;
    int firstEdge = 1;

    if (sb == NULL) {
        return;
    }

    /* A NULL graph or store is written as an empty graph (still valid JSON). */
    graph_init(&emptyGraph);
    if (g == NULL || store == NULL) {
        g = &emptyGraph;
    }

    sb_append_char(sb, '{');
    write_city_filter(sb, g);
    sb_appendf(sb, ",\"numPatients\":%d,\"numDonors\":%d,\"edgeCount\":%d",
               g->numPatients, g->numDonors, g->edgeCount);

    /* Patient vertices (row order). */
    sb_append(sb, ",\"patients\":[");
    for (i = 0; i < g->numPatients; i++) {
        if (i > 0) {
            sb_append_char(sb, ',');
        }
        write_patient_vertex(sb, &store->patients.items[i],
                             int_at(g->patientDegree, i));
    }

    /* Donor vertices (column order). */
    sb_append(sb, "],\"donors\":[");
    for (j = 0; j < g->numDonors; j++) {
        if (j > 0) {
            sb_append_char(sb, ',');
        }
        write_donor_vertex(sb, &store->donors.items[j],
                           int_at(g->donorDegree, j));
    }

    /* Edges in row-major order: row 0 left to right, then row 1, ... */
    sb_append(sb, "],\"edges\":[");
    for (i = 0; i < g->numPatients; i++) {
        for (j = 0; j < g->numDonors; j++) {
            if (graph_has_edge(g, i, j)) {
                if (!firstEdge) {
                    sb_append_char(sb, ',');
                }
                firstEdge = 0;
                sb_appendf(sb, "{\"patientId\":%d,\"donorId\":%d}",
                           int_at(g->patientIds, i), int_at(g->donorIds, j));
            }
        }
    }
    sb_append(sb, "]}");
}

/* GET /api/graph/matrix : the 0/1 table plus row sums and column sums. */
void graph_write_matrix_json(StrBuf *sb, const BipartiteGraph *g)
{
    BipartiteGraph emptyGraph;
    int i;
    int j;

    if (sb == NULL) {
        return;
    }

    graph_init(&emptyGraph);
    if (g == NULL) {
        g = &emptyGraph;
    }

    sb_append_char(sb, '{');
    write_city_filter(sb, g);
    sb_appendf(sb, ",\"rows\":%d,\"cols\":%d,\"patientIds\":",
               g->numPatients, g->numDonors);
    write_int_list(sb, g->patientIds, g->numPatients);
    sb_append(sb, ",\"donorIds\":");
    write_int_list(sb, g->donorIds, g->numDonors);

    /* The matrix always has exactly "rows" lists of "cols" numbers, so the
       shape agrees with rows/cols. For 0 patients this is []; for patients
       but no donors every row is [] (no matrix memory exists in that case,
       and graph_has_edge() then answers 0). */
    sb_append(sb, ",\"matrix\":[");
    for (i = 0; i < g->numPatients; i++) {
        if (i > 0) {
            sb_append_char(sb, ',');
        }
        sb_append_char(sb, '[');
        for (j = 0; j < g->numDonors; j++) {
            if (j > 0) {
                sb_append_char(sb, ',');
            }
            sb_appendf(sb, "%d", graph_has_edge(g, i, j));
        }
        sb_append_char(sb, ']');
    }
    sb_append(sb, "]");

    /* rowSums = patient degrees, columnSums = donor degrees. */
    sb_append(sb, ",\"rowSums\":");
    write_int_list(sb, g->patientDegree, g->numPatients);
    sb_append(sb, ",\"columnSums\":");
    write_int_list(sb, g->donorDegree, g->numDonors);
    sb_append_char(sb, '}');
}

/* GET /api/graph/degrees : degree of every vertex and the handshake sums. */
void graph_write_degrees_json(StrBuf *sb, const BipartiteGraph *g, const RecordStore *store)
{
    BipartiteGraph emptyGraph;
    int i;
    int j;

    if (sb == NULL) {
        return;
    }

    graph_init(&emptyGraph);
    if (g == NULL || store == NULL) {
        g = &emptyGraph;
    }

    sb_append_char(sb, '{');
    write_city_filter(sb, g);

    sb_append(sb, ",\"patients\":[");
    for (i = 0; i < g->numPatients; i++) {
        if (i > 0) {
            sb_append_char(sb, ',');
        }
        sb_appendf(sb, "{\"id\":%d,\"name\":", store->patients.items[i].id);
        sb_append_json_string(sb, store->patients.items[i].name);
        sb_appendf(sb, ",\"degree\":%d}", int_at(g->patientDegree, i));
    }

    sb_append(sb, "],\"donors\":[");
    for (j = 0; j < g->numDonors; j++) {
        if (j > 0) {
            sb_append_char(sb, ',');
        }
        sb_appendf(sb, "{\"id\":%d,\"name\":", store->donors.items[j].id);
        sb_append_json_string(sb, store->donors.items[j].name);
        sb_appendf(sb, ",\"degree\":%d}", int_at(g->donorDegree, j));
    }

    /* The two sums are always equal to edgeCount (handshake lemma); printing
       all three lets the frontend (and a reviewer) check it at a glance. */
    sb_appendf(sb, "],\"edgeCount\":%d,\"patientDegreeSum\":%d,\"donorDegreeSum\":%d}",
               g->edgeCount,
               sum_of(g->patientDegree, g->numPatients),
               sum_of(g->donorDegree, g->numDonors));
}