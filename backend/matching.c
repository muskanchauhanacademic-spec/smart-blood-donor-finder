/*
 * matching.c - Maximum bipartite matching with Kuhn's algorithm.
 * Owner: Developer 4.
 *
 * WHAT THIS FILE DOES
 *   The BipartiteGraph (built by graph.c) has patients as rows and donors as
 *   columns. adj[row][col] == 1 means "this donor may give red blood cells to
 *   this patient right now" (blood group, availability and city filter were
 *   already checked by graph_build). This file only READS adj; it never
 *   recomputes compatibility.
 *
 *   A MATCHING is a set of edges in which no patient and no donor is used twice
 *   (one donor can serve only one patient). We want the MAXIMUM matching, i.e.
 *   as many patients as possible each getting their own donor.
 *
 * KEY WORDS
 *   Matched patient : a patient who currently has a donor assigned.
 *   Free donor      : a donor who is not assigned to anybody yet.
 *   Augmenting path : a path that starts at an UNMATCHED patient, ends at a
 *                     FREE donor, and alternates between edges that are NOT in
 *                     the matching and edges that ARE in the matching:
 *
 *                       patient0 --(new)--> donorA --(old)--> patient1
 *                                --(new)--> donorB (free)
 *
 *                     If we flip every edge on such a path (new edges become
 *                     matched, old edges become unmatched), the matching grows
 *                     by exactly one. Kuhn's algorithm just keeps looking for
 *                     augmenting paths with a depth-first search (DFS).
 *
 * MEMORY OWNERSHIP
 *   matching_compute() allocates m->patientMatch and m->donorMatch; the caller
 *   releases them with matching_free(). Both are NULL when either side of the
 *   graph is empty (we never call malloc(0)).
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "matching.h"

/* ------------------------------------------------------------------------ */
/* Small helpers                                                            */
/* ------------------------------------------------------------------------ */

/* Allocates an int array of n elements and fills every slot with -1
   ("nobody matched yet"). n must be > 0 so that we never call malloc(0).
   The multiplication n * sizeof(int) is checked for overflow first. */
static Status alloc_int_array_filled_with_minus_one(int n, int **out)
{
    int *array;
    int i;

    *out = NULL;
    if (n <= 0) {
        return STATUS_ERR_INTERNAL;
    }
    if ((size_t)n > SIZE_MAX / sizeof(int)) {
        return STATUS_ERR_NOMEM;
    }
    array = (int *)malloc((size_t)n * sizeof(int));
    if (array == NULL) {
        return STATUS_ERR_NOMEM;
    }
    for (i = 0; i < n; i++) {
        array[i] = -1;
    }
    *out = array;
    return STATUS_OK;
}

/* ------------------------------------------------------------------------ */
/* Kuhn's algorithm                                                         */
/* ------------------------------------------------------------------------ */

/*
 * try_augment: tries to find an augmenting path that starts at patient 'u'.
 * Returns 1 and updates the matching if such a path exists, otherwise returns
 * 0 and leaves the matching unchanged.
 *
 * It is RECURSIVE on purpose (this is the DFS):
 *   For every donor v that patient u can use (adj[u][v] == 1):
 *     - skip v if we already visited it during this search;
 *     - mark v visited;
 *     - if v is free, take it: that ends the augmenting path;
 *     - otherwise v belongs to some other patient w = donorMatch[v]. Ask w to
 *       move to a different donor by calling try_augment(w). If w can move, v
 *       becomes free for u, and the whole chain is flipped on the way back out
 *       of the recursion.
 *
 * Why visited[] matters inside ONE search: without it, two patients could ask
 * each other to move forever (infinite recursion), and the same donor would be
 * explored again and again. With it, each donor is explored at most once per
 * search, so the recursion depth is at most the number of donors.
 *
 * 'visited' belongs to matching_compute; this function only reads/writes it.
 */
static int try_augment(const BipartiteGraph *g, MatchingResult *m, char *visited, int u)
{
    int v;

    for (v = 0; v < g->numDonors; v++) {
        if (g->adj[u][v] != 1 || visited[v]) {
            continue;   /* no edge, or donor already tried in this search */
        }
        visited[v] = 1;

        /* Case 1: donor v is free -> u takes v (end of the augmenting path).
           Case 2: donor v is taken by patient w -> try to move w elsewhere. */
        if (m->donorMatch[v] == -1 || try_augment(g, m, visited, m->donorMatch[v])) {
            m->donorMatch[v] = u;
            m->patientMatch[u] = v;
            return 1;
        }
    }
    return 0;   /* every donor of u is either visited or cannot be freed */
}

/* ------------------------------------------------------------------------ */
/* Public API                                                               */
/* ------------------------------------------------------------------------ */

void matching_init(MatchingResult *m)
{
    if (m == NULL) {
        return;
    }
    m->numPatients = 0;
    m->numDonors = 0;
    m->matchCount = 0;
    m->patientMatch = NULL;
    m->donorMatch = NULL;
}

void matching_free(MatchingResult *m)
{
    if (m == NULL) {
        return;
    }
    free(m->patientMatch);   /* free(NULL) is allowed, so zeroed objects are fine */
    free(m->donorMatch);
    matching_init(m);        /* pointers become NULL -> calling this twice is safe */
}

Status matching_compute(const BipartiteGraph *g, MatchingResult *m)
{
    char *visited = NULL;
    Status st = STATUS_OK;
    int u;

    if (g == NULL || m == NULL || g->numPatients < 0 || g->numDonors < 0) {
        return STATUS_ERR_INTERNAL;
    }

    /* m must have been through matching_init() (or an earlier compute) so that
       its pointers are either NULL or valid; free() below relies on that. */
    matching_free(m);   /* drop any older result so that nothing leaks */
    m->numPatients = g->numPatients;
    m->numDonors = g->numDonors;

    /* Empty side: nothing can be matched. Arrays stay NULL (never malloc(0)). */
    if (g->numPatients == 0 || g->numDonors == 0) {
        return STATUS_OK;
    }
    /* graph_build only leaves adj NULL for empty graphs, so this is a bug guard. */
    if (g->adj == NULL) {
        matching_init(m);
        return STATUS_ERR_INTERNAL;
    }

    st = alloc_int_array_filled_with_minus_one(g->numPatients, &m->patientMatch);
    if (st == STATUS_OK) {
        st = alloc_int_array_filled_with_minus_one(g->numDonors, &m->donorMatch);
    }
    if (st == STATUS_OK) {
        /* calloc -> all zeros ("not visited"); one flag per donor column. */
        visited = (char *)calloc((size_t)g->numDonors, sizeof(char));
        if (visited == NULL) {
            st = STATUS_ERR_NOMEM;
        }
    }

    if (st == STATUS_OK) {
        for (u = 0; u < g->numPatients; u++) {
            /* Start a brand-new search for patient u. visited[] is cleared for
               every patient because the matching has just changed: a donor that
               was a dead end for the previous patient may be useful now. */
            memset(visited, 0, (size_t)g->numDonors);
            if (try_augment(g, m, visited, u)) {
                m->matchCount++;   /* each successful search adds exactly one pair */
            }
        }
    }

    free(visited);   /* the scratch array is never handed to the caller */
    if (st != STATUS_OK) {
        matching_free(m);   /* on failure the result is empty, not half-filled */
    }
    return st;
}

/* ------------------------------------------------------------------------ */
/* JSON output                                                              */
/* ------------------------------------------------------------------------ */

/* 1 if patient 'row' has a donor. Also safe when the arrays are NULL (empty graph). */
static int patient_is_matched(const MatchingResult *m, int row)
{
    return m->patientMatch != NULL && row >= 0 && row < m->numPatients
           && m->patientMatch[row] >= 0;
}

/* 1 if donor 'col' has a patient. Also safe when the arrays are NULL. */
static int donor_is_matched(const MatchingResult *m, int col)
{
    return m->donorMatch != NULL && col >= 0 && col < m->numDonors
           && m->donorMatch[col] >= 0;
}

/*
 * Writes the whole JSON object of "POST /api/matching" (the caller wraps it in
 * the {"ok":true,"data":...} envelope). Row i of the graph is
 * store->patients.items[i] and column j is store->donors.items[j] because the
 * graph was built from this same, unchanged store. As a safety net we never
 * read beyond the store's counts, even if somebody broke that rule.
 */
void matching_write_json(StrBuf *sb, const MatchingResult *m, const BipartiteGraph *g,
                         const RecordStore *store)
{
    int patientRows;
    int donorCols;
    int row;
    int col;
    int first;

    if (sb == NULL || m == NULL || g == NULL || store == NULL) {
        return;   /* nothing sensible to write; callers never pass NULL */
    }

    patientRows = m->numPatients < store->patients.count ? m->numPatients : store->patients.count;
    donorCols = m->numDonors < store->donors.count ? m->numDonors : store->donors.count;

    sb_append(sb, "{\"cityFilter\":");
    sb_append_json_string(sb, g->cityFilter);
    sb_appendf(sb, ",\"matchCount\":%d,\"numPatients\":%d,\"numDonors\":%d",
               m->matchCount, m->numPatients, m->numDonors);

    /* pairs: one entry per matched patient, in patient (row) order */
    sb_append(sb, ",\"pairs\":[");
    first = 1;
    for (row = 0; row < patientRows; row++) {
        if (!patient_is_matched(m, row)) {
            continue;
        }
        col = m->patientMatch[row];
        if (col >= donorCols) {
            continue;   /* safety net, see comment above */
        }
        if (!first) {
            sb_append_char(sb, ',');
        }
        first = 0;
        sb_appendf(sb, "{\"patientId\":%d,\"patientName\":", store->patients.items[row].id);
        sb_append_json_string(sb, store->patients.items[row].name);
        sb_appendf(sb, ",\"donorId\":%d,\"donorName\":", store->donors.items[col].id);
        sb_append_json_string(sb, store->donors.items[col].name);
        sb_append_char(sb, '}');
    }

    /* unmatchedPatients: nobody could be found for them */
    sb_append(sb, "],\"unmatchedPatients\":[");
    first = 1;
    for (row = 0; row < patientRows; row++) {
        if (patient_is_matched(m, row)) {
            continue;
        }
        if (!first) {
            sb_append_char(sb, ',');
        }
        first = 0;
        patient_write_json(sb, &store->patients.items[row]);
    }

    /* unmatchedDonors: every donor without a patient. This includes donors with
       degree 0 (unavailable, or no compatible patient) and donors who simply
       were not needed. */
    sb_append(sb, "],\"unmatchedDonors\":[");
    first = 1;
    for (col = 0; col < donorCols; col++) {
        if (donor_is_matched(m, col)) {
            continue;
        }
        if (!first) {
            sb_append_char(sb, ',');
        }
        first = 0;
        donor_write_json(sb, &store->donors.items[col]);
    }
    sb_append(sb, "]}");
}