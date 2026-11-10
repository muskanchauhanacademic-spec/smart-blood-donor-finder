/*
 * test_matching.c - standalone tests for backend/matching.c (Developer 4).
 *
 * No network and no other module's logic is used: every BipartiteGraph and
 * RecordStore below is built BY HAND. Only matching.c is under test.
 *
 * matching.c's JSON writer calls sb_* and donor_write_json/patient_write_json,
 * so the linker needs those. Two ways to build (see the report for commands):
 *   a) with the real backend/json_util.c and backend/records.c, or
 *   b) with the throwaway tests/matching_test_stubs.c.
 *
 * Exit code 0 = every check passed, 1 = at least one check failed.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../backend/matching.h"

/* Counters live in a struct that is passed around, so there are no globals. */
typedef struct {
    int checks;
    int failures;
} TestStats;

#define CHECK(t, condition)                                                   \
    do {                                                                      \
        (t)->checks++;                                                        \
        if (!(condition)) {                                                   \
            (t)->failures++;                                                  \
            printf("    FAILED (line %d): %s\n", __LINE__, #condition);       \
        }                                                                     \
    } while (0)

/* ------------------------------------------------------------------------ */
/* Helpers to build and release hand-made graphs                            */
/* ------------------------------------------------------------------------ */

static void fatal_out_of_memory(void)
{
    printf("test helper: out of memory\n");
    exit(2);
}

/* Builds a graph with 'rows' patients and 'cols' donors. 'cells' is the matrix
   in row-major order (rows * cols numbers, each 0 or 1); it may be NULL when
   rows == 0 or cols == 0. Like graph_build, an empty side leaves adj NULL. */
static void make_graph(BipartiteGraph *g, int rows, int cols, const int *cells)
{
    int i;
    int j;

    memset(g, 0, sizeof *g);
    g->numPatients = rows;
    g->numDonors = cols;
    if (rows == 0 || cols == 0) {
        return;
    }
    g->patientIds = (int *)malloc((size_t)rows * sizeof(int));
    g->donorIds = (int *)malloc((size_t)cols * sizeof(int));
    g->adj = (int **)calloc((size_t)rows, sizeof(int *));
    if (g->patientIds == NULL || g->donorIds == NULL || g->adj == NULL) {
        fatal_out_of_memory();
    }
    for (i = 0; i < rows; i++) {
        g->patientIds[i] = i + 1;
        g->adj[i] = (int *)calloc((size_t)cols, sizeof(int));
        if (g->adj[i] == NULL) {
            fatal_out_of_memory();
        }
        for (j = 0; j < cols; j++) {
            g->adj[i][j] = cells[i * cols + j];
            g->edgeCount += cells[i * cols + j];
        }
    }
    for (j = 0; j < cols; j++) {
        g->donorIds[j] = 101 + j;
    }
}

static void release_graph(BipartiteGraph *g)
{
    int i;

    if (g->adj != NULL) {
        for (i = 0; i < g->numPatients; i++) {
            free(g->adj[i]);
        }
    }
    free(g->adj);
    free(g->patientIds);
    free(g->donorIds);
    memset(g, 0, sizeof *g);
}

/* Checks the rules every valid matching must obey:
   - each matched pair uses a real edge,
   - patientMatch and donorMatch agree with each other,
   - no donor is assigned to two patients,
   - matchCount equals the number of matched patients. */
static void check_valid_matching(TestStats *t, const BipartiteGraph *g, const MatchingResult *m)
{
    int u;
    int v;
    int matchedPatients = 0;
    int matchedDonors = 0;
    int *timesUsed;

    CHECK(t, m->numPatients == g->numPatients);
    CHECK(t, m->numDonors == g->numDonors);
    if (g->numPatients == 0 || g->numDonors == 0) {
        return;   /* arrays are NULL; checked in the empty-graph test */
    }

    timesUsed = (int *)calloc((size_t)g->numDonors, sizeof(int));
    if (timesUsed == NULL) {
        fatal_out_of_memory();
    }
    for (u = 0; u < g->numPatients; u++) {
        v = m->patientMatch[u];
        if (v == -1) {
            continue;
        }
        matchedPatients++;
        CHECK(t, v >= 0 && v < g->numDonors);
        if (v >= 0 && v < g->numDonors) {
            CHECK(t, g->adj[u][v] == 1);          /* pair is a real edge        */
            CHECK(t, m->donorMatch[v] == u);      /* both arrays agree          */
            timesUsed[v]++;
        }
    }
    for (v = 0; v < g->numDonors; v++) {
        CHECK(t, timesUsed[v] <= 1);              /* donor never assigned twice */
        u = m->donorMatch[v];
        if (u != -1) {
            matchedDonors++;
            CHECK(t, u >= 0 && u < g->numPatients);
            if (u >= 0 && u < g->numPatients) {
                CHECK(t, m->patientMatch[u] == v);
            }
        }
    }
    CHECK(t, matchedPatients == matchedDonors);
    CHECK(t, matchedPatients == m->matchCount);
    free(timesUsed);
}

/* Builds the graph, runs Kuhn, checks the generic rules, and returns the result
   through g and m (the caller releases both). */
static void solve(TestStats *t, BipartiteGraph *g, MatchingResult *m, int rows, int cols,
                  const int *cells)
{
    Status st;

    make_graph(g, rows, cols, cells);
    matching_init(m);
    st = matching_compute(g, m);
    CHECK(t, st == STATUS_OK);
    check_valid_matching(t, g, m);
}

static void release_both(BipartiteGraph *g, MatchingResult *m)
{
    matching_free(m);
    release_graph(g);
}

/* ------------------------------------------------------------------------ */
/* Brute force reference (tiny graphs only) for the randomised test         */
/* ------------------------------------------------------------------------ */

/* Tries every way of giving each patient "no donor" or one unused donor and
   returns the best count. Exponential, but fine for at most 7 x 8. */
static int brute_force_max(const BipartiteGraph *g, int row, unsigned usedMask)
{
    int best;
    int v;
    int candidate;

    if (row >= g->numPatients) {
        return 0;
    }
    best = brute_force_max(g, row + 1, usedMask);   /* this patient stays unmatched */
    for (v = 0; v < g->numDonors; v++) {
        if (g->adj[row][v] == 1 && (usedMask & (1u << v)) == 0) {
            candidate = 1 + brute_force_max(g, row + 1, usedMask | (1u << v));
            if (candidate > best) {
                best = candidate;
            }
        }
    }
    return best;
}

/* Small deterministic pseudo random generator (LCG); the state is passed in. */
static int next_random(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return (int)(*state >> 16);
}

/* ------------------------------------------------------------------------ */
/* The tests                                                                */
/* ------------------------------------------------------------------------ */

static void test_no_edges(TestStats *t)
{
    static const int cells[9] = { 0, 0, 0,
                                  0, 0, 0,
                                  0, 0, 0 };
    BipartiteGraph g;
    MatchingResult m;
    int i;

    printf("test_no_edges\n");
    solve(t, &g, &m, 3, 3, cells);
    CHECK(t, m.matchCount == 0);
    for (i = 0; i < 3; i++) {
        CHECK(t, m.patientMatch[i] == -1);
        CHECK(t, m.donorMatch[i] == -1);
    }
    release_both(&g, &m);
}

static void test_empty_graphs(TestStats *t)
{
    BipartiteGraph g;
    MatchingResult m;
    Status st;

    printf("test_empty_graphs\n");

    /* 0 patients, 2 donors */
    make_graph(&g, 0, 2, NULL);
    matching_init(&m);
    st = matching_compute(&g, &m);
    CHECK(t, st == STATUS_OK);
    CHECK(t, m.matchCount == 0 && m.numPatients == 0 && m.numDonors == 2);
    CHECK(t, m.patientMatch == NULL && m.donorMatch == NULL);
    release_both(&g, &m);

    /* 3 patients, 0 donors */
    make_graph(&g, 3, 0, NULL);
    matching_init(&m);
    st = matching_compute(&g, &m);
    CHECK(t, st == STATUS_OK);
    CHECK(t, m.matchCount == 0 && m.numPatients == 3 && m.numDonors == 0);
    CHECK(t, m.patientMatch == NULL && m.donorMatch == NULL);
    release_both(&g, &m);

    /* 0 x 0 */
    make_graph(&g, 0, 0, NULL);
    matching_init(&m);
    st = matching_compute(&g, &m);
    CHECK(t, st == STATUS_OK);
    CHECK(t, m.matchCount == 0 && m.numPatients == 0 && m.numDonors == 0);
    CHECK(t, m.patientMatch == NULL && m.donorMatch == NULL);
    release_both(&g, &m);
}

static void test_one_match(TestStats *t)
{
    static const int cells[1] = { 1 };
    BipartiteGraph g;
    MatchingResult m;

    printf("test_one_match\n");
    solve(t, &g, &m, 1, 1, cells);
    CHECK(t, m.matchCount == 1);
    CHECK(t, m.patientMatch[0] == 0);
    CHECK(t, m.donorMatch[0] == 0);
    release_both(&g, &m);
}

static void test_independent_matches(TestStats *t)
{
    static const int cells[9] = { 1, 0, 0,
                                  0, 1, 0,
                                  0, 0, 1 };
    BipartiteGraph g;
    MatchingResult m;
    int i;

    printf("test_independent_matches\n");
    solve(t, &g, &m, 3, 3, cells);
    CHECK(t, m.matchCount == 3);
    for (i = 0; i < 3; i++) {
        CHECK(t, m.patientMatch[i] == i);
        CHECK(t, m.donorMatch[i] == i);
    }
    release_both(&g, &m);
}

static void test_competition_for_one_donor(TestStats *t)
{
    /* three patients, a single donor that all of them can use */
    static const int cells[3] = { 1, 1, 1 };
    BipartiteGraph g;
    MatchingResult m;

    printf("test_competition_for_one_donor\n");
    solve(t, &g, &m, 3, 1, cells);
    CHECK(t, m.matchCount == 1);
    CHECK(t, m.donorMatch[0] == 0);       /* first patient keeps the donor      */
    CHECK(t, m.patientMatch[0] == 0);
    CHECK(t, m.patientMatch[1] == -1);
    CHECK(t, m.patientMatch[2] == -1);
    release_both(&g, &m);
}

static void test_augmenting_path(TestStats *t)
{
    /* P1 {D1,D2}, P2 {D1}, P3 {D2,D3}. Greedy would give only 2 pairs. */
    static const int cells[9] = { 1, 1, 0,
                                  1, 0, 0,
                                  0, 1, 1 };
    BipartiteGraph g;
    MatchingResult m;

    printf("test_augmenting_path\n");
    solve(t, &g, &m, 3, 3, cells);
    CHECK(t, m.matchCount == 3);
    /* the exact final assignment follows from the algorithm (see the doc) */
    CHECK(t, m.patientMatch[0] == 1);     /* P1 -> D2 (moved away from D1)      */
    CHECK(t, m.patientMatch[1] == 0);     /* P2 -> D1                           */
    CHECK(t, m.patientMatch[2] == 2);     /* P3 -> D3                           */
    release_both(&g, &m);
}

static void test_more_patients_than_donors(TestStats *t)
{
    /* 5 patients, 2 donors: at most 2 pairs */
    static const int cells[10] = { 1, 0,
                                   1, 1,
                                   0, 1,
                                   1, 1,
                                   1, 0 };
    BipartiteGraph g;
    MatchingResult m;
    int unmatched = 0;
    int u;

    printf("test_more_patients_than_donors\n");
    solve(t, &g, &m, 5, 2, cells);
    CHECK(t, m.matchCount == 2);
    for (u = 0; u < 5; u++) {
        if (m.patientMatch[u] == -1) {
            unmatched++;
        }
    }
    CHECK(t, unmatched == 3);
    release_both(&g, &m);
}

static void test_more_donors_than_patients(TestStats *t)
{
    /* 2 patients, 5 donors: both can be served, three donors stay free */
    static const int cells[10] = { 1, 1, 0, 0, 0,
                                   1, 0, 0, 0, 1 };
    BipartiteGraph g;
    MatchingResult m;
    int freeDonors = 0;
    int v;

    printf("test_more_donors_than_patients\n");
    solve(t, &g, &m, 2, 5, cells);
    CHECK(t, m.matchCount == 2);
    for (v = 0; v < 5; v++) {
        if (m.donorMatch[v] == -1) {
            freeDonors++;
        }
    }
    CHECK(t, freeDonors == 3);
    release_both(&g, &m);
}

static void test_donor_with_degree_zero_stays_unmatched(TestStats *t)
{
    /* donor column 1 has no edge at all */
    static const int cells[4] = { 1, 0,
                                  1, 0 };
    BipartiteGraph g;
    MatchingResult m;

    printf("test_donor_with_degree_zero_stays_unmatched\n");
    solve(t, &g, &m, 2, 2, cells);
    CHECK(t, m.matchCount == 1);          /* both patients want donor 0 only */
    CHECK(t, m.donorMatch[1] == -1);
    release_both(&g, &m);
}

static void test_long_augmenting_chain(TestStats *t)
{
    /* P0 {D0,D1}, P1 {D1,D2}, P2 {D2,D3}, P3 {D3,D4}, P4 {D0}.
       The first four patients take D0..D3 (their first choice). P4 can only
       use D0, so the algorithm must push P0 to D1, P1 to D2, P2 to D3 and P3
       to D4 - one augmenting path of length 5. A greedy rule would stop at 4. */
    static const int cells[25] = { 1, 1, 0, 0, 0,
                                   0, 1, 1, 0, 0,
                                   0, 0, 1, 1, 0,
                                   0, 0, 0, 1, 1,
                                   1, 0, 0, 0, 0 };
    BipartiteGraph g;
    MatchingResult m;

    printf("test_long_augmenting_chain\n");
    solve(t, &g, &m, 5, 5, cells);
    CHECK(t, m.matchCount == 5);          /* needs a chain of 4 re-assignments */
    release_both(&g, &m);
}

static void test_recompute_and_double_free(TestStats *t)
{
    static const int cells[4] = { 1, 1,
                                  1, 0 };
    BipartiteGraph g;
    MatchingResult m;
    Status st;

    printf("test_recompute_and_double_free\n");
    solve(t, &g, &m, 2, 2, cells);
    CHECK(t, m.matchCount == 2);

    st = matching_compute(&g, &m);        /* second run must replace, not leak */
    CHECK(t, st == STATUS_OK);
    CHECK(t, m.matchCount == 2);
    check_valid_matching(t, &g, &m);

    matching_free(&m);
    matching_free(&m);                    /* idempotent */
    CHECK(t, m.patientMatch == NULL && m.donorMatch == NULL && m.matchCount == 0);
    matching_free(NULL);                  /* NULL safe */
    release_graph(&g);
}

static void test_random_against_brute_force(TestStats *t)
{
    uint32_t seed = 12345u;
    int round;
    int rows;
    int cols;
    int i;
    int density;
    int cells[7 * 8];
    BipartiteGraph g;
    MatchingResult m;
    int expected;

    printf("test_random_against_brute_force (300 random graphs)\n");
    for (round = 0; round < 300; round++) {
        rows = 1 + next_random(&seed) % 7;       /* 1..7 */
        cols = 1 + next_random(&seed) % 8;       /* 1..8 */
        density = 20 + 30 * (round % 3);         /* 20%, 50%, 80% */
        for (i = 0; i < rows * cols; i++) {
            cells[i] = (next_random(&seed) % 100) < density ? 1 : 0;
        }
        solve(t, &g, &m, rows, cols, cells);
        expected = brute_force_max(&g, 0, 0u);
        if (m.matchCount != expected) {
            printf("    mismatch in round %d (%d x %d): kuhn=%d brute=%d\n",
                   round, rows, cols, m.matchCount, expected);
        }
        CHECK(t, m.matchCount == expected);
        release_both(&g, &m);
    }
}

/* ---- JSON output ------------------------------------------------------- */

/* Compares two strings; on a mismatch prints both so the problem is easy to see. */
static void check_string(TestStats *t, const char *actual, const char *expected)
{
    t->checks++;
    if (strcmp(actual, expected) != 0) {
        t->failures++;
        printf("    FAILED: JSON differs\n      got:      %s\n      expected: %s\n", actual, expected);
    }
}


#define DONOR_101 "{\"id\":101,\"name\":\"Asha Rao\",\"bloodGroup\":\"O+\",\"city\":\"Nagpur\"," \
                  "\"phone\":\"9000000101\",\"available\":true}"
#define DONOR_102 "{\"id\":102,\"name\":\"Vikram Joshi\",\"bloodGroup\":\"A-\",\"city\":\"Pune\"," \
                  "\"phone\":\"9000000102\",\"available\":true}"
#define DONOR_103 "{\"id\":103,\"name\":\"Pooja Jadhav\",\"bloodGroup\":\"B-\",\"city\":\"Nagpur\"," \
                  "\"phone\":\"9000000103\",\"available\":true}"
#define PATIENT_2 "{\"id\":2,\"name\":\"Sunita Wagh\",\"bloodGroup\":\"O-\",\"city\":\"Nashik\"}"

/* Hand-made store: the arrays live on the caller's stack, so records_free()
   must NOT be called on it. */
static void fill_store(RecordStore *store, Patient *patients, int patientCount, Donor *donors,
                       int donorCount)
{
    memset(store, 0, sizeof *store);
    store->patients.items = patients;
    store->patients.count = patientCount;
    store->patients.capacity = patientCount;
    store->donors.items = donors;
    store->donors.count = donorCount;
    store->donors.capacity = donorCount;
}

static void test_json_output(TestStats *t)
{
    Patient patients[3] = {
        { 1, "Ravi Kulkarni", "A-", "Pune" },
        { 2, "Sunita Wagh",   "O-", "Nashik" },
        { 3, "Mohan Iyer",    "B-", "Nagpur" }
    };
    Donor donors[3] = {
        { 101, "Asha Rao",     "O+", "Nagpur", "9000000101", 1 },
        { 102, "Vikram Joshi", "A-", "Pune",   "9000000102", 1 },
        { 103, "Pooja Jadhav", "B-", "Nagpur", "9000000103", 1 }
    };
    static const int augmentCells[9] = { 1, 1, 0,
                                         1, 0, 0,
                                         0, 1, 1 };
    static const int partialCells[6] = { 1, 0, 0,
                                         0, 0, 0 };
    RecordStore store;
    BipartiteGraph g;
    MatchingResult m;
    StrBuf sb;

    printf("test_json_output\n");

    /* A: the 3x3 augmenting example, everybody matched */
    fill_store(&store, patients, 3, donors, 3);
    solve(t, &g, &m, 3, 3, augmentCells);
    sb_init(&sb);
    matching_write_json(&sb, &m, &g, &store);
    CHECK(t, sb.failed == 0);
    check_string(t, sb_cstr(&sb),
        "{\"cityFilter\":\"\",\"matchCount\":3,\"numPatients\":3,\"numDonors\":3,\"pairs\":["
        "{\"patientId\":1,\"patientName\":\"Ravi Kulkarni\",\"donorId\":102,\"donorName\":\"Vikram Joshi\"},"
        "{\"patientId\":2,\"patientName\":\"Sunita Wagh\",\"donorId\":101,\"donorName\":\"Asha Rao\"},"
        "{\"patientId\":3,\"patientName\":\"Mohan Iyer\",\"donorId\":103,\"donorName\":\"Pooja Jadhav\"}"
        "],\"unmatchedPatients\":[],\"unmatchedDonors\":[]}");
    sb_free(&sb);
    release_both(&g, &m);

    /* B: city filter text, one pair, an unmatched patient and two free donors */
    fill_store(&store, patients, 2, donors, 3);
    solve(t, &g, &m, 2, 3, partialCells);
    snprintf(g.cityFilter, sizeof g.cityFilter, "%s", "Pune");
    sb_init(&sb);
    matching_write_json(&sb, &m, &g, &store);
    check_string(t, sb_cstr(&sb),
        "{\"cityFilter\":\"Pune\",\"matchCount\":1,\"numPatients\":2,\"numDonors\":3,\"pairs\":["
        "{\"patientId\":1,\"patientName\":\"Ravi Kulkarni\",\"donorId\":101,\"donorName\":\"Asha Rao\"}"
        "],\"unmatchedPatients\":[" PATIENT_2 "],\"unmatchedDonors\":[" DONOR_102 "," DONOR_103 "]}");
    sb_free(&sb);
    release_both(&g, &m);

    /* C: empty graph (0 patients, 2 donors): arrays are NULL, donors listed as unmatched */
    fill_store(&store, patients, 0, donors, 2);
    make_graph(&g, 0, 2, NULL);
    matching_init(&m);
    CHECK(t, matching_compute(&g, &m) == STATUS_OK);
    sb_init(&sb);
    matching_write_json(&sb, &m, &g, &store);
    check_string(t, sb_cstr(&sb),
        "{\"cityFilter\":\"\",\"matchCount\":0,\"numPatients\":0,\"numDonors\":2,\"pairs\":[],"
        "\"unmatchedPatients\":[],\"unmatchedDonors\":[" DONOR_101 "," DONOR_102 "]}");
    sb_free(&sb);
    release_both(&g, &m);
}

/* ------------------------------------------------------------------------ */

int main(void)
{
    TestStats stats = { 0, 0 };

    test_no_edges(&stats);
    test_empty_graphs(&stats);
    test_one_match(&stats);
    test_independent_matches(&stats);
    test_competition_for_one_donor(&stats);
    test_augmenting_path(&stats);
    test_more_patients_than_donors(&stats);
    test_more_donors_than_patients(&stats);
    test_donor_with_degree_zero_stays_unmatched(&stats);
    test_long_augmenting_chain(&stats);
    test_recompute_and_double_free(&stats);
    test_random_against_brute_force(&stats);
    test_json_output(&stats);

    printf("\n%d checks, %d failed\n", stats.checks, stats.failures);
    printf(stats.failures == 0 ? "ALL TESTS PASSED\n" : "SOME TESTS FAILED\n");
    return stats.failures == 0 ? 0 : 1;
}