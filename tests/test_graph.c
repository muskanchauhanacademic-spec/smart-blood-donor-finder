/*
 * test_graph.c - Stand-alone tests for backend/graph.c (Developer 3).
 *
 * This file has its own main(). It needs NO other module of the project:
 *   - compat_is_edge()  is replaced by a small STUB below. The stub decides
 *     edges from a table that each test fills in by hand, so we can create
 *     exactly the graph we want (for example the 3x3 example of section H).
 *     The stub still honours "available" and the city filter, so the
 *     availability and city tests exercise real graph_build() behaviour.
 *   - the StrBuf functions (sb_*) are replaced by a tiny copy below, so the
 *     JSON writers can be tested without json_util.c.
 *     (Compile with -DTEST_USE_REAL_JSON_UTIL and add backend/json_util.c
 *      to the command to use the real ones instead.)
 *   - records_* are not needed: the RecordStore is filled in by hand.
 *
 * Build and run (from the project root):
 *   gcc -std=c11 -Wall -Wextra -pedantic -g -Ibackend -o tests/test_graph.exe \
 *       tests/test_graph.c backend/graph.c
 *   tests/test_graph.exe
 *
 * The program prints one line per test and a summary. Exit code 0 = all passed.
 */

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "graph.h"
#include "compatibility.h"      /* so our stub's prototype is checked by the compiler */

/* ======================================================================== */
/* Part 1: tiny test framework                                              */
/* (file-level counters are acceptable here: this is test code, not backend) */
/* ======================================================================== */

static int s_checks   = 0;      /* how many CHECKs ran      */
static int s_failures = 0;      /* how many of them failed  */

#define CHECK(cond)                                                          \
    do {                                                                     \
        s_checks++;                                                          \
        if (!(cond)) {                                                       \
            s_failures++;                                                    \
            printf("    FAIL (line %d): %s\n", __LINE__, #cond);             \
        }                                                                    \
    } while (0)

/* Compare the text inside a StrBuf with the exact text we expect. */
static void check_text(int line, const StrBuf *sb, const char *expected)
{
    s_checks++;
    if (strcmp(sb_cstr(sb), expected) != 0) {
        s_failures++;
        printf("    FAIL (line %d): JSON differs\n      got:      %s\n      expected: %s\n",
               line, sb_cstr(sb), expected);
    }
}
#define CHECK_JSON(sb, expected) check_text(__LINE__, (sb), (expected))

/* ======================================================================== */
/* Part 2: stubs                                                            */
/* ======================================================================== */

#define MAXN 10   /* tests use at most 10 patients (ids 1..10) and 10 donors (ids 101..110) */

/* allowed[patientId][donorId-100] == 1  means "blood groups are compatible".
   Each test fills this table with set_allowed(). */
static int s_allowed[MAXN + 1][MAXN + 1];

/* Case-insensitive text equality using our own tolower loop (like the real rule). */
static int stub_city_matches(const char *donorCity, const char *filter)
{
    size_t k;
    if (filter == NULL || filter[0] == '\0') {
        return 1;                                   /* no filter */
    }
    if (strlen(donorCity) != strlen(filter)) {
        return 0;
    }
    for (k = 0; donorCity[k] != '\0'; k++) {
        if (tolower((unsigned char)donorCity[k]) != tolower((unsigned char)filter[k])) {
            return 0;
        }
    }
    return 1;
}

/* STUB of the single edge rule: compatible (table) && available && city matches. */
int compat_is_edge(const Donor *donor, const Patient *patient, const char *cityFilter)
{
    if (donor == NULL || patient == NULL) {
        return 0;
    }
    if (patient->id < 1 || patient->id > MAXN ||
        donor->id < 101 || donor->id > 100 + MAXN) {
        return 0;
    }
    if (!s_allowed[patient->id][donor->id - 100]) {
        return 0;
    }
    if (donor->available != 1) {
        return 0;
    }
    return stub_city_matches(donor->city, cityFilter);
}

#ifndef TEST_USE_REAL_JSON_UTIL
/* Minimal StrBuf, just enough for graph.c. (The real one is Developer 2's.) */
void sb_init(StrBuf *sb) { sb->data = NULL; sb->length = 0; sb->capacity = 0; sb->failed = 0; }
void sb_free(StrBuf *sb) { free(sb->data); sb_init(sb); }
void sb_clear(StrBuf *sb) { sb->length = 0; sb->failed = 0; if (sb->data) sb->data[0] = '\0'; }

void sb_append_n(StrBuf *sb, const char *data, size_t n)
{
    if (sb->failed || n == 0) { return; }
    if (sb->length + n + 1 > sb->capacity) {
        size_t newCap = (sb->capacity ? sb->capacity : 64);
        char  *tmp;
        while (newCap < sb->length + n + 1) { newCap *= 2; }
        tmp = realloc(sb->data, newCap);
        if (tmp == NULL) { sb->failed = 1; return; }
        sb->data = tmp;
        sb->capacity = newCap;
    }
    memcpy(sb->data + sb->length, data, n);
    sb->length += n;
    sb->data[sb->length] = '\0';
}
void sb_append(StrBuf *sb, const char *text) { sb_append_n(sb, text, strlen(text)); }
void sb_append_char(StrBuf *sb, char c) { sb_append_n(sb, &c, 1); }

void sb_appendf(StrBuf *sb, const char *fmt, ...)
{
    char    small[256];
    va_list ap;
    int     needed;

    va_start(ap, fmt);
    needed = vsnprintf(small, sizeof small, fmt, ap);
    va_end(ap);
    if (needed < 0) { sb->failed = 1; return; }
    if ((size_t)needed < sizeof small) {
        sb_append_n(sb, small, (size_t)needed);
    } else {
        char *big = malloc((size_t)needed + 1);
        if (big == NULL) { sb->failed = 1; return; }
        va_start(ap, fmt);
        vsnprintf(big, (size_t)needed + 1, fmt, ap);
        va_end(ap);
        sb_append_n(sb, big, (size_t)needed);
        free(big);
    }
}

void sb_append_json_string(StrBuf *sb, const char *text)
{
    sb_append_char(sb, '"');
    for (; *text != '\0'; text++) {
        unsigned char c = (unsigned char)*text;
        if (c == '"' || c == '\\') { sb_append_char(sb, '\\'); sb_append_char(sb, (char)c); }
        else if (c < 0x20) { sb_appendf(sb, "\\u%04x", (unsigned)c); }
        else { sb_append_char(sb, (char)c); }
    }
    sb_append_char(sb, '"');
}

const char *sb_cstr(const StrBuf *sb) { return sb->data ? sb->data : ""; }
#endif /* TEST_USE_REAL_JSON_UTIL */

/* ======================================================================== */
/* Part 3: helpers to build test data                                       */
/* ======================================================================== */

/* Fill s_allowed from a flat rows x cols table (row-major). */
static void set_allowed(int rows, int cols, const int *flat)
{
    int i;
    int j;
    memset(s_allowed, 0, sizeof s_allowed);
    for (i = 0; i < rows; i++) {
        for (j = 0; j < cols; j++) {
            s_allowed[i + 1][j + 1] = flat[i * cols + j];
        }
    }
}

/* Same, but every pair is compatible. */
static void set_all_allowed(int rows, int cols)
{
    int i;
    int j;
    memset(s_allowed, 0, sizeof s_allowed);
    for (i = 1; i <= rows; i++) {
        for (j = 1; j <= cols; j++) {
            s_allowed[i][j] = 1;
        }
    }
}

/* Build a RecordStore by hand: patients get ids 1..nP, donors ids 101..100+nD.
   Names are "Patient 1", "Donor 1", ...; every donor is available, in Pune.
   (No malloc of zero bytes: an empty side keeps items == NULL.) */
static void make_store(RecordStore *store, int nP, int nD)
{
    int i;

    store->patients.items    = NULL;
    store->patients.count    = nP;
    store->patients.capacity = nP;
    store->donors.items      = NULL;
    store->donors.count      = nD;
    store->donors.capacity   = nD;

    if (nP > 0) {
        store->patients.items = calloc((size_t)nP, sizeof(Patient));
        if (store->patients.items == NULL) { printf("out of memory in test\n"); exit(2); }
        for (i = 0; i < nP; i++) {
            Patient *p = &store->patients.items[i];
            p->id = i + 1;
            snprintf(p->name, sizeof p->name, "Patient %d", i + 1);
            snprintf(p->bloodGroup, sizeof p->bloodGroup, "A+");
            snprintf(p->city, sizeof p->city, "Pune");
        }
    }
    if (nD > 0) {
        store->donors.items = calloc((size_t)nD, sizeof(Donor));
        if (store->donors.items == NULL) { printf("out of memory in test\n"); exit(2); }
        for (i = 0; i < nD; i++) {
            Donor *d = &store->donors.items[i];
            d->id = 101 + i;
            snprintf(d->name, sizeof d->name, "Donor %d", i + 1);
            snprintf(d->bloodGroup, sizeof d->bloodGroup, "O+");
            snprintf(d->city, sizeof d->city, "Pune");
            snprintf(d->phone, sizeof d->phone, "9000000000");
            d->available = 1;
        }
    }
}

static void free_store(RecordStore *store)
{
    free(store->patients.items);
    free(store->donors.items);
    store->patients.items = NULL;
    store->donors.items   = NULL;
    store->patients.count = store->donors.count = 0;
}

/* The handshake lemma: sum(patient degrees) == sum(donor degrees) == edgeCount,
   and edgeCount also equals the number of 1s found by walking the matrix. */
static void check_handshake(const BipartiteGraph *g)
{
    int i;
    int j;
    int sumP = 0;
    int sumD = 0;
    int ones = 0;

    for (i = 0; g->patientDegree != NULL && i < g->numPatients; i++) { sumP += g->patientDegree[i]; }
    for (j = 0; g->donorDegree   != NULL && j < g->numDonors;   j++) { sumD += g->donorDegree[j]; }
    for (i = 0; i < g->numPatients; i++) {
        for (j = 0; j < g->numDonors; j++) {
            ones += graph_has_edge(g, i, j);
        }
    }
    CHECK(sumP == g->edgeCount);
    CHECK(sumD == g->edgeCount);
    CHECK(ones == g->edgeCount);
}

/* Check that the matrix of g equals the expected flat rows x cols table. */
static void check_matrix(const BipartiteGraph *g, int rows, int cols, const int *flat)
{
    int i;
    int j;
    int same = 1;
    CHECK(g->numPatients == rows);
    CHECK(g->numDonors == cols);
    for (i = 0; i < rows; i++) {
        for (j = 0; j < cols; j++) {
            if (graph_has_edge(g, i, j) != flat[i * cols + j]) { same = 0; }
        }
    }
    CHECK(same);
}

/* ======================================================================== */
/* Part 4: the tests                                                        */
/* ======================================================================== */

/* 0 patients and 0 donors: nothing allocated, JSON still valid. */
static void test_empty_graph(void)
{
    RecordStore    store;
    BipartiteGraph g;
    StrBuf         sb;

    printf("test_empty_graph (0 patients, 0 donors)\n");
    make_store(&store, 0, 0);
    graph_init(&g);
    sb_init(&sb);

    CHECK(graph_build(&g, &store, NULL) == STATUS_OK);
    CHECK(g.numPatients == 0 && g.numDonors == 0 && g.edgeCount == 0);
    CHECK(g.adj == NULL && g.patientDegree == NULL && g.donorDegree == NULL);
    CHECK(g.patientIds == NULL && g.donorIds == NULL);
    CHECK(g.cityFilter[0] == '\0');
    CHECK(graph_compute_degrees(&g) == STATUS_OK);        /* harmless on an empty graph */

    graph_write_json(&sb, &g, &store);
    CHECK_JSON(&sb, "{\"cityFilter\":\"\",\"numPatients\":0,\"numDonors\":0,\"edgeCount\":0,"
                    "\"patients\":[],\"donors\":[],\"edges\":[]}");
    sb_clear(&sb);
    graph_write_matrix_json(&sb, &g);
    CHECK_JSON(&sb, "{\"cityFilter\":\"\",\"rows\":0,\"cols\":0,\"patientIds\":[],\"donorIds\":[],"
                    "\"matrix\":[],\"rowSums\":[],\"columnSums\":[]}");
    sb_clear(&sb);
    graph_write_degrees_json(&sb, &g, &store);
    CHECK_JSON(&sb, "{\"cityFilter\":\"\",\"patients\":[],\"donors\":[],"
                    "\"edgeCount\":0,\"patientDegreeSum\":0,\"donorDegreeSum\":0}");

    sb_free(&sb);
    graph_free(&g);
    free_store(&store);
}

/* 1 patient and 0 donors (and the mirror case): one side empty. */
static void test_one_side_empty(void)
{
    RecordStore    store;
    BipartiteGraph g;
    StrBuf         sb;

    printf("test_one_side_empty (1 patient + 0 donors, then 0 patients + 1 donor)\n");
    set_all_allowed(MAXN, MAXN);
    graph_init(&g);
    sb_init(&sb);

    /* 1 patient, 0 donors */
    make_store(&store, 1, 0);
    CHECK(graph_build(&g, &store, NULL) == STATUS_OK);
    CHECK(g.numPatients == 1 && g.numDonors == 0 && g.edgeCount == 0);
    CHECK(g.adj == NULL && g.patientDegree == NULL && g.donorDegree == NULL);
    CHECK(g.patientIds != NULL && g.patientIds[0] == 1);
    CHECK(g.donorIds == NULL);
    CHECK(graph_has_edge(&g, 0, 0) == 0);

    graph_write_json(&sb, &g, &store);
    CHECK_JSON(&sb, "{\"cityFilter\":\"\",\"numPatients\":1,\"numDonors\":0,\"edgeCount\":0,"
                    "\"patients\":[{\"id\":1,\"name\":\"Patient 1\",\"bloodGroup\":\"A+\","
                    "\"city\":\"Pune\",\"degree\":0}],\"donors\":[],\"edges\":[]}");
    sb_clear(&sb);
    graph_write_matrix_json(&sb, &g);
    CHECK_JSON(&sb, "{\"cityFilter\":\"\",\"rows\":1,\"cols\":0,\"patientIds\":[1],\"donorIds\":[],"
                    "\"matrix\":[[]],\"rowSums\":[0],\"columnSums\":[]}");
    sb_clear(&sb);
    graph_write_degrees_json(&sb, &g, &store);
    CHECK_JSON(&sb, "{\"cityFilter\":\"\",\"patients\":[{\"id\":1,\"name\":\"Patient 1\",\"degree\":0}],"
                    "\"donors\":[],\"edgeCount\":0,\"patientDegreeSum\":0,\"donorDegreeSum\":0}");
    check_handshake(&g);
    free_store(&store);

    /* 0 patients, 1 donor (rebuild into the same graph object) */
    sb_clear(&sb);
    make_store(&store, 0, 1);
    CHECK(graph_build(&g, &store, NULL) == STATUS_OK);
    CHECK(g.numPatients == 0 && g.numDonors == 1 && g.edgeCount == 0);
    CHECK(g.adj == NULL && g.patientIds == NULL && g.donorIds != NULL && g.donorIds[0] == 101);
    graph_write_matrix_json(&sb, &g);
    CHECK_JSON(&sb, "{\"cityFilter\":\"\",\"rows\":0,\"cols\":1,\"patientIds\":[],\"donorIds\":[101],"
                    "\"matrix\":[],\"rowSums\":[],\"columnSums\":[0]}");
    check_handshake(&g);

    sb_free(&sb);
    graph_free(&g);
    free_store(&store);
}

/* Exactly one patient and one donor, with an edge. */
static void test_one_patient_one_donor(void)
{
    RecordStore    store;
    BipartiteGraph g;
    StrBuf         sb;

    printf("test_one_patient_one_donor\n");
    set_all_allowed(1, 1);
    make_store(&store, 1, 1);
    graph_init(&g);
    sb_init(&sb);

    CHECK(graph_build(&g, &store, NULL) == STATUS_OK);
    CHECK(g.numPatients == 1 && g.numDonors == 1 && g.edgeCount == 1);
    CHECK(g.adj != NULL && g.adj[0][0] == 1);
    CHECK(graph_has_edge(&g, 0, 0) == 1);
    CHECK(g.patientDegree[0] == 1 && g.donorDegree[0] == 1);
    CHECK(g.patientIds[0] == 1 && g.donorIds[0] == 101);
    check_handshake(&g);

    graph_write_matrix_json(&sb, &g);
    CHECK_JSON(&sb, "{\"cityFilter\":\"\",\"rows\":1,\"cols\":1,\"patientIds\":[1],\"donorIds\":[101],"
                    "\"matrix\":[[1]],\"rowSums\":[1],\"columnSums\":[1]}");

    sb_free(&sb);
    graph_free(&g);
    free_store(&store);
}

/* Patients and donors exist but no pair is compatible: all zeros, valid arrays. */
static void test_no_edges(void)
{
    RecordStore    store;
    BipartiteGraph g;
    StrBuf         sb;

    printf("test_no_edges (2 patients x 2 donors, no compatible pair)\n");
    set_allowed(0, 0, NULL);                     /* clears the table */
    make_store(&store, 2, 2);
    graph_init(&g);
    sb_init(&sb);

    CHECK(graph_build(&g, &store, NULL) == STATUS_OK);
    CHECK(g.edgeCount == 0);
    CHECK(g.adj != NULL && g.patientDegree != NULL && g.donorDegree != NULL);
    CHECK(g.patientDegree[0] == 0 && g.patientDegree[1] == 0);
    CHECK(g.donorDegree[0] == 0 && g.donorDegree[1] == 0);
    check_handshake(&g);

    graph_write_matrix_json(&sb, &g);
    CHECK_JSON(&sb, "{\"cityFilter\":\"\",\"rows\":2,\"cols\":2,\"patientIds\":[1,2],\"donorIds\":[101,102],"
                    "\"matrix\":[[0,0],[0,0]],\"rowSums\":[0,0],\"columnSums\":[0,0]}");
    sb_clear(&sb);
    graph_write_json(&sb, &g, &store);
    CHECK(strstr(sb_cstr(&sb), "\"edges\":[]") != NULL);   /* empty edge list */

    sb_free(&sb);
    graph_free(&g);
    free_store(&store);
}

/* Several edges, including a patient with every donor and a donor with none. */
static void test_multiple_edges(void)
{
    static const int table[4 * 3] = {
        1, 1, 1,      /* patient 1 can receive from every donor */
        0, 0, 0,      /* patient 2 from nobody                  */
        1, 0, 1,      /* patient 3                              */
        1, 0, 0       /* patient 4                              */
    };
    RecordStore    store;
    BipartiteGraph g;

    printf("test_multiple_edges (4 patients x 3 donors)\n");
    set_allowed(4, 3, table);
    make_store(&store, 4, 3);
    graph_init(&g);

    CHECK(graph_build(&g, &store, NULL) == STATUS_OK);
    check_matrix(&g, 4, 3, table);
    CHECK(g.edgeCount == 6);
    CHECK(g.patientDegree[0] == 3 && g.patientDegree[1] == 0 &&
          g.patientDegree[2] == 2 && g.patientDegree[3] == 1);
    CHECK(g.donorDegree[0] == 3 && g.donorDegree[1] == 1 && g.donorDegree[2] == 2);
    check_handshake(&g);

    graph_free(&g);
    free_store(&store);
}

/* The exact example of section H. */
static void test_spec_example_3x3(void)
{
    static const int table[3 * 3] = {
        1, 1, 0,
        1, 0, 0,
        0, 1, 1
    };
    RecordStore    store;
    BipartiteGraph g;
    StrBuf         sb;

    printf("test_spec_example_3x3 (section H example)\n");
    set_allowed(3, 3, table);
    make_store(&store, 3, 3);
    graph_init(&g);
    sb_init(&sb);

    CHECK(graph_build(&g, &store, NULL) == STATUS_OK);
    check_matrix(&g, 3, 3, table);
    CHECK(g.edgeCount == 5);
    CHECK(g.patientDegree[0] == 2 && g.patientDegree[1] == 1 && g.patientDegree[2] == 2);
    CHECK(g.donorDegree[0] == 2 && g.donorDegree[1] == 2 && g.donorDegree[2] == 1);
    check_handshake(&g);

    graph_write_matrix_json(&sb, &g);
    CHECK_JSON(&sb, "{\"cityFilter\":\"\",\"rows\":3,\"cols\":3,\"patientIds\":[1,2,3],"
                    "\"donorIds\":[101,102,103],\"matrix\":[[1,1,0],[1,0,0],[0,1,1]],"
                    "\"rowSums\":[2,1,2],\"columnSums\":[2,2,1]}");

    sb_clear(&sb);
    graph_write_degrees_json(&sb, &g, &store);
    CHECK_JSON(&sb, "{\"cityFilter\":\"\","
                    "\"patients\":[{\"id\":1,\"name\":\"Patient 1\",\"degree\":2},"
                    "{\"id\":2,\"name\":\"Patient 2\",\"degree\":1},"
                    "{\"id\":3,\"name\":\"Patient 3\",\"degree\":2}],"
                    "\"donors\":[{\"id\":101,\"name\":\"Donor 1\",\"degree\":2},"
                    "{\"id\":102,\"name\":\"Donor 2\",\"degree\":2},"
                    "{\"id\":103,\"name\":\"Donor 3\",\"degree\":1}],"
                    "\"edgeCount\":5,\"patientDegreeSum\":5,\"donorDegreeSum\":5}");

    sb_clear(&sb);
    graph_write_json(&sb, &g, &store);
    CHECK_JSON(&sb, "{\"cityFilter\":\"\",\"numPatients\":3,\"numDonors\":3,\"edgeCount\":5,"
                    "\"patients\":["
                    "{\"id\":1,\"name\":\"Patient 1\",\"bloodGroup\":\"A+\",\"city\":\"Pune\",\"degree\":2},"
                    "{\"id\":2,\"name\":\"Patient 2\",\"bloodGroup\":\"A+\",\"city\":\"Pune\",\"degree\":1},"
                    "{\"id\":3,\"name\":\"Patient 3\",\"bloodGroup\":\"A+\",\"city\":\"Pune\",\"degree\":2}],"
                    "\"donors\":["
                    "{\"id\":101,\"name\":\"Donor 1\",\"bloodGroup\":\"O+\",\"city\":\"Pune\",\"available\":true,\"degree\":2},"
                    "{\"id\":102,\"name\":\"Donor 2\",\"bloodGroup\":\"O+\",\"city\":\"Pune\",\"available\":true,\"degree\":2},"
                    "{\"id\":103,\"name\":\"Donor 3\",\"bloodGroup\":\"O+\",\"city\":\"Pune\",\"available\":true,\"degree\":1}],"
                    "\"edges\":[{\"patientId\":1,\"donorId\":101},{\"patientId\":1,\"donorId\":102},"
                    "{\"patientId\":2,\"donorId\":101},{\"patientId\":3,\"donorId\":102},"
                    "{\"patientId\":3,\"donorId\":103}]}");

    sb_free(&sb);
    graph_free(&g);
    free_store(&store);
}

/* Handshake lemma on a bigger graph with a regular-looking pattern. */
static void test_handshake(void)
{
    enum { ROWS = 7, COLS = 8 };
    int            table[ROWS * COLS];
    int            i;
    int            j;
    int            expectedEdges = 0;
    RecordStore    store;
    BipartiteGraph g;

    printf("test_handshake (7 x 8 pattern)\n");
    for (i = 0; i < ROWS; i++) {
        for (j = 0; j < COLS; j++) {
            table[i * COLS + j] = ((i * 3 + j * 5) % 4 != 0) ? 1 : 0;
            expectedEdges += table[i * COLS + j];
        }
    }
    set_allowed(ROWS, COLS, table);
    make_store(&store, ROWS, COLS);
    graph_init(&g);

    CHECK(graph_build(&g, &store, NULL) == STATUS_OK);
    CHECK(g.edgeCount == expectedEdges);
    check_matrix(&g, ROWS, COLS, table);
    check_handshake(&g);

    /* The degree of one vertex checked by hand: row 0 and column 0. */
    {
        int row0 = 0;
        int col0 = 0;
        for (j = 0; j < COLS; j++) { row0 += table[0 * COLS + j]; }
        for (i = 0; i < ROWS; i++) { col0 += table[i * COLS + 0]; }
        CHECK(g.patientDegree[0] == row0);
        CHECK(g.donorDegree[0] == col0);
    }

    graph_free(&g);
    free_store(&store);
}

/* City filter: applies to the DONOR's city; every patient stays a vertex. */
static void test_city_filter(void)
{
    RecordStore    store;
    BipartiteGraph g;
    StrBuf         sb;
    char           longFilter[61];

    printf("test_city_filter\n");
    set_all_allowed(3, 3);                    /* everyone compatible: only city/availability matter */
    make_store(&store, 3, 3);
    snprintf(store.donors.items[1].city, CITY_LEN, "Nagpur");   /* donor 102 */
    snprintf(store.donors.items[2].city, CITY_LEN, "pune");     /* donor 103, different case */
    graph_init(&g);
    sb_init(&sb);

    /* No filter: all 9 pairs. */
    CHECK(graph_build(&g, &store, NULL) == STATUS_OK);
    CHECK(g.edgeCount == 9 && g.cityFilter[0] == '\0');
    check_handshake(&g);

    /* Empty-string filter behaves like no filter. */
    CHECK(graph_build(&g, &store, "") == STATUS_OK);
    CHECK(g.edgeCount == 9);

    /* "PUNE" matches donors 101 and 103 (case-insensitive). */
    CHECK(graph_build(&g, &store, "PUNE") == STATUS_OK);
    CHECK(strcmp(g.cityFilter, "PUNE") == 0);
    CHECK(g.numPatients == 3 && g.numDonors == 3);        /* all vertices remain */
    CHECK(g.edgeCount == 6);
    CHECK(g.patientDegree[0] == 2 && g.patientDegree[1] == 2 && g.patientDegree[2] == 2);
    CHECK(g.donorDegree[0] == 3 && g.donorDegree[1] == 0 && g.donorDegree[2] == 3);
    check_handshake(&g);
    graph_write_matrix_json(&sb, &g);
    CHECK_JSON(&sb, "{\"cityFilter\":\"PUNE\",\"rows\":3,\"cols\":3,\"patientIds\":[1,2,3],"
                    "\"donorIds\":[101,102,103],\"matrix\":[[1,0,1],[1,0,1],[1,0,1]],"
                    "\"rowSums\":[2,2,2],\"columnSums\":[3,0,3]}");

    /* A city nobody lives in: no edges, but arrays are still valid. */
    CHECK(graph_build(&g, &store, "Mumbai") == STATUS_OK);
    CHECK(g.edgeCount == 0 && g.numPatients == 3 && g.numDonors == 3);
    CHECK(g.patientDegree != NULL && g.patientDegree[0] == 0);
    check_handshake(&g);

    /* An absurdly long filter must be cut safely (no overflow, '\0' kept). */
    memset(longFilter, 'x', sizeof longFilter - 1);
    longFilter[sizeof longFilter - 1] = '\0';
    CHECK(graph_build(&g, &store, longFilter) == STATUS_OK);
    CHECK(strlen(g.cityFilter) == CITY_LEN - 1);
    CHECK(g.edgeCount == 0);

    sb_free(&sb);
    graph_free(&g);
    free_store(&store);
}

/* Rebuild the SAME graph object after a donor becomes unavailable. */
static void test_rebuild_after_availability_change(void)
{
    static const int table[3 * 3] = { 1, 1, 0,   1, 0, 0,   0, 1, 1 };
    static const int afterOff[3 * 3] = { 1, 0, 0,   1, 0, 0,   0, 0, 1 };
    RecordStore    store;
    BipartiteGraph g;

    printf("test_rebuild_after_availability_change\n");
    set_allowed(3, 3, table);
    make_store(&store, 3, 3);
    graph_init(&g);

    CHECK(graph_build(&g, &store, NULL) == STATUS_OK);
    CHECK(g.edgeCount == 5);

    /* Donor 102 (column 1) is no longer available. */
    store.donors.items[1].available = 0;
    CHECK(graph_build(&g, &store, NULL) == STATUS_OK);     /* no graph_free needed first */
    check_matrix(&g, 3, 3, afterOff);
    CHECK(g.edgeCount == 3);
    CHECK(g.donorDegree[1] == 0);
    CHECK(g.patientDegree[0] == 1 && g.patientDegree[1] == 1 && g.patientDegree[2] == 1);
    check_handshake(&g);

    /* Available again: the original graph comes back. */
    store.donors.items[1].available = 1;
    CHECK(graph_build(&g, &store, NULL) == STATUS_OK);
    check_matrix(&g, 3, 3, table);
    CHECK(g.edgeCount == 5);

    graph_free(&g);
    free_store(&store);
}

/* Building repeatedly (also with changing sizes) must not leak or crash.
   Run under AddressSanitizer / valgrind to see leaks; here we check results. */
static void test_rebuild_twice_and_free_safety(void)
{
    RecordStore    big;
    RecordStore    small;
    RecordStore    empty;
    BipartiteGraph g;
    int            round;

    printf("test_rebuild_twice_and_free_safety\n");
    set_all_allowed(MAXN, MAXN);
    make_store(&big, 6, 5);
    make_store(&small, 2, 2);
    make_store(&empty, 0, 0);
    graph_init(&g);

    for (round = 0; round < 3; round++) {
        CHECK(graph_build(&g, &big, NULL) == STATUS_OK);
        CHECK(g.numPatients == 6 && g.numDonors == 5 && g.edgeCount == 30);
        check_handshake(&g);
    }
    CHECK(graph_build(&g, &small, NULL) == STATUS_OK);       /* big -> small */
    CHECK(g.numPatients == 2 && g.numDonors == 2 && g.edgeCount == 4);
    CHECK(graph_build(&g, &empty, NULL) == STATUS_OK);       /* small -> empty */
    CHECK(g.numPatients == 0 && g.adj == NULL && g.edgeCount == 0);
    CHECK(graph_build(&g, &big, NULL) == STATUS_OK);         /* empty -> big */
    CHECK(g.edgeCount == 30);

    /* Degrees computed twice give the same answer (and do not leak). */
    CHECK(graph_compute_degrees(&g) == STATUS_OK);
    CHECK(graph_compute_degrees(&g) == STATUS_OK);
    CHECK(g.patientDegree[0] == 5 && g.donorDegree[0] == 6);
    check_handshake(&g);

    /* graph_free is idempotent and NULL-safe. */
    graph_free(&g);
    graph_free(&g);
    graph_free(NULL);
    CHECK(g.adj == NULL && g.patientIds == NULL && g.donorIds == NULL);
    CHECK(g.patientDegree == NULL && g.donorDegree == NULL);
    CHECK(g.numPatients == 0 && g.numDonors == 0 && g.edgeCount == 0);

    /* A never-built (only initialised) graph can be freed too. */
    graph_init(&g);
    graph_free(&g);

    /* Bad arguments are reported, not crashed on. */
    CHECK(graph_build(NULL, &big, NULL) == STATUS_ERR_INTERNAL);
    CHECK(graph_build(&g, NULL, NULL) == STATUS_ERR_INTERNAL);
    CHECK(graph_compute_degrees(NULL) == STATUS_ERR_INTERNAL);

    graph_free(&g);
    free_store(&big);
    free_store(&small);
    free_store(&empty);
}

/* Unequal partition sizes: more donors than patients and the opposite. */
static void test_unequal_partitions(void)
{
    static const int wide[2 * 5] = {            /* 2 patients, 5 donors */
        1, 0, 1, 1, 0,
        0, 1, 1, 0, 0
    };
    static const int tall[6 * 2] = {            /* 6 patients, 2 donors */
        1, 0,
        1, 1,
        0, 0,
        0, 1,
        1, 0,
        1, 1
    };
    RecordStore    store;
    BipartiteGraph g;

    printf("test_unequal_partitions (2x5 and 6x2)\n");
    graph_init(&g);

    set_allowed(2, 5, wide);
    make_store(&store, 2, 5);
    CHECK(graph_build(&g, &store, NULL) == STATUS_OK);
    check_matrix(&g, 2, 5, wide);
    CHECK(g.edgeCount == 5);
    CHECK(g.patientDegree[0] == 3 && g.patientDegree[1] == 2);
    CHECK(g.donorDegree[0] == 1 && g.donorDegree[1] == 1 && g.donorDegree[2] == 2 &&
          g.donorDegree[3] == 1 && g.donorDegree[4] == 0);
    check_handshake(&g);
    free_store(&store);

    set_allowed(6, 2, tall);
    make_store(&store, 6, 2);
    CHECK(graph_build(&g, &store, NULL) == STATUS_OK);
    check_matrix(&g, 6, 2, tall);
    CHECK(g.edgeCount == 7);
    CHECK(g.patientDegree[0] == 1 && g.patientDegree[1] == 2 && g.patientDegree[2] == 0 &&
          g.patientDegree[3] == 1 && g.patientDegree[4] == 1 && g.patientDegree[5] == 2);
    CHECK(g.donorDegree[0] == 4 && g.donorDegree[1] == 3);
    check_handshake(&g);

    graph_free(&g);
    free_store(&store);
}

/* graph_has_edge must never read outside the matrix. */
static void test_has_edge_bounds(void)
{
    static const int table[2 * 2] = { 1, 0,   0, 1 };
    RecordStore    store;
    BipartiteGraph g;

    printf("test_has_edge_bounds\n");
    set_allowed(2, 2, table);
    make_store(&store, 2, 2);
    graph_init(&g);
    CHECK(graph_build(&g, &store, NULL) == STATUS_OK);

    CHECK(graph_has_edge(&g, 0, 0) == 1);
    CHECK(graph_has_edge(&g, 0, 1) == 0);
    CHECK(graph_has_edge(&g, 1, 1) == 1);
    CHECK(graph_has_edge(&g, -1, 0) == 0);
    CHECK(graph_has_edge(&g, 0, -1) == 0);
    CHECK(graph_has_edge(&g, 2, 0) == 0);
    CHECK(graph_has_edge(&g, 0, 2) == 0);
    CHECK(graph_has_edge(&g, 1000000, 1000000) == 0);
    CHECK(graph_has_edge(NULL, 0, 0) == 0);

    graph_free(&g);
    CHECK(graph_has_edge(&g, 0, 0) == 0);                    /* freed graph: still safe */

    free_store(&store);
}

/* Writers given NULL arguments still produce valid (empty) JSON. */
static void test_writers_null_safety(void)
{
    StrBuf sb;

    printf("test_writers_null_safety\n");
    sb_init(&sb);
    graph_write_matrix_json(&sb, NULL);
    CHECK_JSON(&sb, "{\"cityFilter\":\"\",\"rows\":0,\"cols\":0,\"patientIds\":[],\"donorIds\":[],"
                    "\"matrix\":[],\"rowSums\":[],\"columnSums\":[]}");
    sb_clear(&sb);
    graph_write_json(&sb, NULL, NULL);
    CHECK(strstr(sb_cstr(&sb), "\"edges\":[]}") != NULL);
    sb_clear(&sb);
    graph_write_degrees_json(&sb, NULL, NULL);
    CHECK(strstr(sb_cstr(&sb), "\"donorDegreeSum\":0}") != NULL);
    sb_free(&sb);
}

/* ======================================================================== */
/* Part 5: main                                                             */
/* ======================================================================== */

int main(void)
{
    test_empty_graph();
    test_one_side_empty();
    test_one_patient_one_donor();
    test_no_edges();
    test_multiple_edges();
    test_spec_example_3x3();
    test_handshake();
    test_city_filter();
    test_rebuild_after_availability_change();
    test_rebuild_twice_and_free_safety();
    test_unequal_partitions();
    test_has_edge_bounds();
    test_writers_null_safety();

    printf("\n%d checks run, %d failed\n", s_checks, s_failures);
    if (s_failures == 0) {
        printf("ALL TESTS PASSED\n");
        return 0;
    }
    printf("SOME TESTS FAILED\n");
    return 1;
}