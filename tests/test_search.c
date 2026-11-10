/* test_search.c - standalone tests for backend/search.c (Developer 2).
 *
 * Build and run from the project root:
 *   gcc -std=c11 -Wall -Wextra -pedantic -g -Ibackend -o test_search \
 *       tests/test_search.c backend/search.c backend/compatibility.c
 *   ./test_search
 *
 * search.c calls records_find_patient (Developer 1's module). Here it is replaced by a tiny
 * STUB defined below, and the RecordStore is filled by hand instead of through records_add_*.
 * The stub exists only inside tests/ and must never be copied into backend/.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "search.h"

static int g_checks = 0;
static int g_failures = 0;

static void check(int condition, const char *description)
{
    g_checks++;
    if (condition) {
        printf("PASS: %s\n", description);
    } else {
        printf("FAIL: %s\n", description);
        g_failures++;
    }
}

/* ---- test-only stub of Developer 1's lookup (linear scan, same contract: NULL if absent) ---- */
const Patient *records_find_patient(const RecordStore *store, int id)
{
    int i;
    for (i = 0; i < store->patients.count; i++) {
        if (store->patients.items[i].id == id) {
            return &store->patients.items[i];
        }
    }
    return NULL;
}

/* ---- helpers that fill a RecordStore by hand ---- */
#define TEST_DONOR_CAP   16
#define TEST_PATIENT_CAP 8

static int store_setup(RecordStore *s)
{
    memset(s, 0, sizeof *s);
    s->donors.items = calloc(TEST_DONOR_CAP, sizeof(Donor));
    s->patients.items = calloc(TEST_PATIENT_CAP, sizeof(Patient));
    if (s->donors.items == NULL || s->patients.items == NULL) {
        return 0;
    }
    s->donors.capacity = TEST_DONOR_CAP;
    s->patients.capacity = TEST_PATIENT_CAP;
    return 1;
}

static void store_teardown(RecordStore *s)
{
    free(s->donors.items);
    free(s->patients.items);
    memset(s, 0, sizeof *s);
}

static void add_donor(RecordStore *s, int id, const char *name, const char *bg, const char *city,
                      int available)
{
    Donor *d = &s->donors.items[s->donors.count++];     /* stays below TEST_DONOR_CAP in these tests */
    memset(d, 0, sizeof *d);
    d->id = id;
    snprintf(d->name, sizeof d->name, "%s", name);
    snprintf(d->bloodGroup, sizeof d->bloodGroup, "%s", bg);
    snprintf(d->city, sizeof d->city, "%s", city);
    snprintf(d->phone, sizeof d->phone, "9000000%03d", id % 1000);
    d->available = available;
}

static void add_patient(RecordStore *s, int id, const char *name, const char *bg, const char *city)
{
    Patient *p = &s->patients.items[s->patients.count++];
    memset(p, 0, sizeof *p);
    p->id = id;
    snprintf(p->name, sizeof p->name, "%s", name);
    snprintf(p->bloodGroup, sizeof p->bloodGroup, "%s", bg);
    snprintf(p->city, sizeof p->city, "%s", city);
}

/* Main data set (all names are fictional).
   Patients: 1 A-/Pune, 2 AB+/Mumbai, 3 O-/Nagpur, 4 B+/Pune
   Donors (id group city available):
     101 O- Pune 1    102 A- Nagpur 1   103 A+ Pune 1    104 O+ Mumbai 0
     105 B- Pune 1    106 AB- Mumbai 1  107 O- Nagpur 0  108 O+ pune 1 (lower-case city on purpose) */
static int build_main_store(RecordStore *s)
{
    if (!store_setup(s)) {
        return 0;
    }
    add_patient(s, 1, "Ravi", "A-", "Pune");
    add_patient(s, 2, "Meena", "AB+", "Mumbai");
    add_patient(s, 3, "Sunil", "O-", "Nagpur");
    add_patient(s, 4, "Latha", "B+", "Pune");

    add_donor(s, 101, "Donor 101", "O-", "Pune", 1);
    add_donor(s, 102, "Donor 102", "A-", "Nagpur", 1);
    add_donor(s, 103, "Donor 103", "A+", "Pune", 1);
    add_donor(s, 104, "Donor 104", "O+", "Mumbai", 0);
    add_donor(s, 105, "Donor 105", "B-", "Pune", 1);
    add_donor(s, 106, "Donor 106", "AB-", "Mumbai", 1);
    add_donor(s, 107, "Donor 107", "O-", "Nagpur", 0);
    add_donor(s, 108, "Donor 108", "O+", "pune", 1);
    return 1;
}

/* Run one search and check status, count and the exact donor ids in order. */
static void expect_search(const RecordStore *store, int patientId, const char *city, int availableOnly,
                          const int *expectedIds, int expectedCount, const char *description)
{
    DonorSearchResult r;
    Status st;
    int ok;
    int i;

    r.donors = NULL;
    r.count = -1;
    st = search_compatible_donors(store, patientId, city, availableOnly, &r);

    ok = (st == STATUS_OK) && (r.count == expectedCount);
    if (ok && expectedCount == 0) {
        ok = (r.donors == NULL);                         /* no malloc(0): pointer stays NULL */
    }
    if (ok && expectedCount > 0) {
        ok = (r.donors != NULL);
        for (i = 0; ok && i < expectedCount; i++) {
            if (r.donors[i].id != expectedIds[i]) {
                ok = 0;
            }
        }
    }
    check(ok, description);
    if (!ok) {
        printf("  status=%d count=%d (expected %d); ids:", (int)st, r.count, expectedCount);
        for (i = 0; r.donors != NULL && i < r.count; i++) {
            printf(" %d", r.donors[i].id);
        }
        printf("\n");
    }
    search_result_free(&r);
}

static void test_patient_a_negative(RecordStore *s)
{
    /* Patient 1 is A-: only A- and O- donors qualify -> donors 101 (O-), 102 (A-), 107 (O-, unavailable). */
    static const int availOnly[] = { 101, 102 };
    static const int all[] = { 101, 102, 107 };
    static const int pune[] = { 101 };
    static const int nagpurAvail[] = { 102 };
    static const int nagpurAll[] = { 102, 107 };

    expect_search(s, 1, NULL, 1, availOnly, 2, "A- patient, availableOnly=true: donors 101, 102");
    expect_search(s, 1, NULL, 0, all, 3, "A- patient, availableOnly=false: unavailable 107 is included, store order kept");
    expect_search(s, 1, "", 1, availOnly, 2, "empty city filter behaves like no filter");
    expect_search(s, 1, "pune", 1, pune, 1, "city filter 'pune' (case-insensitive) -> only 101");
    expect_search(s, 1, "NAGPUR", 1, nagpurAvail, 1, "city filter 'NAGPUR', available only -> 102");
    expect_search(s, 1, "Nagpur", 0, nagpurAll, 2, "city filter Nagpur, availableOnly=false -> 102 and 107");
    expect_search(s, 1, "Mumbai", 1, NULL, 0, "city with no compatible donors -> OK with count 0");
    expect_search(s, 1, "Delhi", 0, NULL, 0, "unknown city -> OK with count 0");
}

static void test_other_patients(RecordStore *s)
{
    /* Patient 2 (AB+) can receive from every group, so every available donor qualifies. */
    static const int p2Avail[] = { 101, 102, 103, 105, 106, 108 };
    static const int p2All[] = { 101, 102, 103, 104, 105, 106, 107, 108 };
    static const int p2Pune[] = { 101, 103, 105, 108 };
    /* Patient 3 (O-) can only receive from O-: donors 101 and 107. */
    static const int p3Avail[] = { 101 };
    static const int p3All[] = { 101, 107 };
    static const int p3NagpurAll[] = { 107 };
    /* Patient 4 (B+) can receive B+ B- O+ O-: donors 101 105 108 available, plus 104 and 107 unavailable. */
    static const int p4Avail[] = { 101, 105, 108 };
    static const int p4All[] = { 101, 104, 105, 107, 108 };

    expect_search(s, 2, NULL, 1, p2Avail, 6, "AB+ patient, available only: 6 donors");
    expect_search(s, 2, NULL, 0, p2All, 8, "AB+ patient, all donors: 8 donors in store order");
    expect_search(s, 2, "PUNE", 1, p2Pune, 4, "AB+ patient, city PUNE: matches 'Pune' and 'pune' donors");
    expect_search(s, 3, NULL, 1, p3Avail, 1, "O- patient, available only: just 101");
    expect_search(s, 3, NULL, 0, p3All, 2, "O- patient, all: 101 and 107");
    expect_search(s, 3, "Nagpur", 1, NULL, 0, "O- patient, Nagpur, available only: the only Nagpur O- donor is unavailable");
    expect_search(s, 3, "Nagpur", 0, p3NagpurAll, 1, "O- patient, Nagpur, all: 107 appears when unavailable donors are allowed");
    expect_search(s, 4, NULL, 1, p4Avail, 3, "B+ patient, available only");
    expect_search(s, 4, NULL, 0, p4All, 5, "B+ patient, all");
}

static void test_invalid_patient(RecordStore *s)
{
    DonorSearchResult r;
    Status st;

    r.donors = NULL;
    r.count = 77;                                       /* garbage: the function must reset it */
    st = search_compatible_donors(s, 999, NULL, 1, &r);
    check(st == STATUS_ERR_NOT_FOUND && r.donors == NULL && r.count == 0,
          "unknown patient id 999 -> NOT_FOUND with an empty result");

    r.count = 77;
    st = search_compatible_donors(s, 0, NULL, 1, &r);
    check(st == STATUS_ERR_NOT_FOUND && r.count == 0, "patient id 0 -> NOT_FOUND");
    st = search_compatible_donors(s, -5, NULL, 1, &r);
    check(st == STATUS_ERR_NOT_FOUND && r.count == 0, "negative patient id -> NOT_FOUND");

    check(search_compatible_donors(s, 1, NULL, 1, NULL) == STATUS_ERR_INTERNAL,
          "NULL output pointer -> INTERNAL (no crash)");
    check(search_compatible_donors(NULL, 1, NULL, 1, &r) == STATUS_ERR_INTERNAL && r.count == 0,
          "NULL store -> INTERNAL, result left empty");
}

static void test_copies_and_free(RecordStore *s)
{
    DonorSearchResult r;

    r.donors = NULL;
    r.count = 0;
    check(search_compatible_donors(s, 1, NULL, 1, &r) == STATUS_OK && r.count == 2, "search for copy test");

    check(r.donors != NULL && r.donors != s->donors.items && r.donors != &s->donors.items[0],
          "result is a separate allocation (copies, not pointers into the store)");
    check(strcmp(r.donors[0].name, "Donor 101") == 0 && strcmp(r.donors[0].bloodGroup, "O-") == 0 &&
          strcmp(r.donors[0].city, "Pune") == 0 && r.donors[0].available == 1,
          "copied donor has all fields (name, group, city, available)");

    r.donors[0].available = 7;                          /* change the copy... */
    snprintf(r.donors[0].city, sizeof r.donors[0].city, "Changed");
    check(s->donors.items[0].available == 1 && strcmp(s->donors.items[0].city, "Pune") == 0,
          "...and the store record is unaffected");

    search_result_free(&r);
    check(r.donors == NULL && r.count == 0, "search_result_free resets the result");
    search_result_free(&r);
    check(r.donors == NULL && r.count == 0, "search_result_free twice is safe");
    search_result_free(NULL);
    check(1, "search_result_free(NULL) does not crash");
    {
        DonorSearchResult zeroed = { NULL, 0 };
        search_result_free(&zeroed);
        check(zeroed.donors == NULL, "search_result_free on a zeroed result is safe");
    }
}

static void test_no_compatible_and_empty_stores(void)
{
    RecordStore s;

    /* Only A+ and B+ donors exist; an O- patient can only receive O-, so nothing matches. */
    if (!store_setup(&s)) {
        check(0, "allocation for the small store");
        return;
    }
    add_patient(&s, 9, "Zoya", "O-", "Pune");
    add_donor(&s, 201, "Donor 201", "A+", "Pune", 1);
    add_donor(&s, 202, "Donor 202", "B+", "Pune", 1);
    expect_search(&s, 9, NULL, 0, NULL, 0, "no compatible donors at all -> OK, count 0, donors NULL");
    store_teardown(&s);

    /* A patient but no donors at all (items array may even be NULL). */
    memset(&s, 0, sizeof s);
    s.patients.items = calloc(1, sizeof(Patient));
    if (s.patients.items == NULL) {
        check(0, "allocation for the empty-donor store");
        return;
    }
    s.patients.capacity = 1;
    add_patient(&s, 5, "Omar", "AB+", "Pune");
    expect_search(&s, 5, NULL, 1, NULL, 0, "zero donors (items == NULL) -> OK, count 0");
    free(s.patients.items);
    memset(&s, 0, sizeof s);

    /* No patients at all: every id is unknown. */
    {
        DonorSearchResult r;
        r.donors = NULL;
        r.count = 0;
        check(search_compatible_donors(&s, 1, NULL, 1, &r) == STATUS_ERR_NOT_FOUND,
              "zero patients -> NOT_FOUND");
    }
}

int main(void)
{
    RecordStore s;

    if (!build_main_store(&s)) {
        printf("FAIL: could not allocate the test store\n");
        store_teardown(&s);
        return 1;
    }

    test_patient_a_negative(&s);
    test_other_patients(&s);
    test_invalid_patient(&s);
    test_copies_and_free(&s);
    store_teardown(&s);
    test_no_compatible_and_empty_stores();

    printf("\n%d checks, %d failed\n", g_checks, g_failures);
    printf(g_failures == 0 ? "ALL TESTS PASSED\n" : "SOME TESTS FAILED\n");
    return g_failures == 0 ? 0 : 1;
}