/*
 * sample_data.c - fictional demo records for "sbdf.exe --demo".
 * Owner: Developer 4.
 *
 * Everything is added through records_add_donor / records_add_patient so the
 * normal validation (and the duplicate-id check) applies to the demo data too.
 * ALL NAMES AND PHONE NUMBERS ARE INVENTED.
 *
 * WHY THE ORDER OF THE RECORDS MATTERS
 *   Kuhn's algorithm visits patients in store order and tries donors in store
 *   order, so the first patient normally grabs the first donor that fits. The
 *   O- donor is therefore placed FIRST (id 101) to create the situation that
 *   needs an augmenting path (see "Scenario 2").
 *
 * WHAT THE DATA DEMONSTRATES (no city filter; edge = compatible + available)
 *
 *   Scenario 1 - several blood groups and cities
 *       Donors and patients cover A+, A-, B+, B-, O+, O- (and AB+ for one
 *       donor) in Nagpur, Pune and Nashik.
 *
 *   Scenario 2 - an augmenting-path reassignment is REQUIRED
 *       Patient 1 (A-) can use donor 101 (O-) or donor 102 (A-). A greedy
 *       "first free donor" rule gives her donor 101. Patient 2 (O-) can ONLY
 *       use donor 101, so greedy leaves her without a donor. Kuhn's algorithm
 *       finds the augmenting path  patient 2 -> donor 101 -> patient 1 -> donor
 *       102: patient 1 moves to donor 102 and patient 2 gets donor 101.
 *
 *   Scenario 3 - two patients compete for ONE donor
 *       Patients 2 and 7 are both O-. Only one O- donor is available (101), so
 *       only one of them can be served.
 *
 *   Scenario 4 - an unavailable but compatible donor
 *       Donor 107 (O-, Pune) would be compatible with patients 2 and 7, but
 *       is marked unavailable, so there is no edge. If you mark 107 available
 *       in the UI, patient 7 becomes matchable.
 *
 *   Scenario 5 - an unmatched patient
 *       Patient 7 (see Scenario 3) ends the demo without a donor.
 *
 *   Scenario 6 - a donor with degree 0
 *       Donor 108 is AB+, which can only donate to AB+ patients, and there are
 *       none. Donor 107 also has degree 0, because he is unavailable.
 */

#include <stddef.h>

#include "sample_data.h"

typedef struct {
    int         id;
    const char *name;
    const char *bloodGroup;
    const char *city;
    const char *phone;
    int         available;
} SampleDonor;

typedef struct {
    int         id;
    const char *name;
    const char *bloodGroup;
    const char *city;
} SamplePatient;

/* 12 donors. Column order of the graph = this order. */
static const SampleDonor SAMPLE_DONORS[] = {
    { 101, "Meera Deshmukh",  "O-",  "Nashik", "9000000101", 1 },  /* the only available O- (Scenarios 2, 3)      */
    { 102, "Vikram Joshi",    "A-",  "Pune",   "9000000102", 1 },  /* second choice of patient 1 (Scenario 2)     */
    { 103, "Pooja Jadhav",    "B-",  "Nagpur", "9000000103", 1 },
    { 104, "Asha Rao",        "O+",  "Nagpur", "9000000104", 1 },
    { 105, "Neha Kulkarni",   "A+",  "Nagpur", "9000000105", 1 },
    { 106, "Sameer Kale",     "B+",  "Nashik", "9000000106", 1 },
    { 107, "Sanjay Patil",    "O-",  "Pune",   "9000000107", 0 },  /* compatible but UNAVAILABLE (Scenario 4)     */
    { 108, "Rohit Shinde",    "AB+", "Nashik", "9000000108", 1 },  /* degree 0: no AB+ patient exists (Scenario 6) */
    { 109, "Arjun More",      "O+",  "Pune",   "9000000109", 1 },
    { 110, "Kavita Bhosale",  "A+",  "Nashik", "9000000110", 1 },
    { 111, "Tanvi Gokhale",   "B+",  "Pune",   "9000000111", 1 },
    { 112, "Rekha Naik",      "A+",  "Nagpur", "9000000112", 1 }
};

/* 8 patients. Row order of the graph = this order. */
static const SamplePatient SAMPLE_PATIENTS[] = {
    { 1, "Ravi Kulkarni", "A-", "Pune"   },   /* may use 101 or 102 -> needs reassignment (Scenario 2) */
    { 2, "Sunita Wagh",   "O-", "Nashik" },   /* can ONLY use 101 (Scenarios 2, 3)                      */
    { 3, "Mohan Iyer",    "B-", "Nagpur" },   /* B- or O-: donors 101, 103                              */
    { 4, "Priya Nair",    "O+", "Nagpur" },   /* O+ or O-: donors 101, 104, 109                         */
    { 5, "Divya Mehta",   "A+", "Nagpur" },   /* many options: A+, A-, O+, O-                           */
    { 6, "Kiran Bhat",    "B+", "Pune"   },   /* many options: B+, B-, O+, O-                           */
    { 7, "Anil Chavan",   "O-", "Pune"   },   /* competes with patient 2, ends unmatched (Scenarios 3,5) */
    { 8, "Shweta Pande",  "A+", "Nashik" }    /* many options: A+, A-, O+, O-                           */
};

Status sample_data_load(RecordStore *store)
{
    size_t i;
    size_t donorTotal = sizeof SAMPLE_DONORS / sizeof SAMPLE_DONORS[0];
    size_t patientTotal = sizeof SAMPLE_PATIENTS / sizeof SAMPLE_PATIENTS[0];
    Status st = STATUS_OK;

    if (store == NULL) {
        return STATUS_ERR_INTERNAL;
    }

    /* Stop at the first error (for example STATUS_ERR_DUPLICATE_ID if the demo
       is loaded twice, or STATUS_ERR_NOMEM) and report it to the caller. */
    for (i = 0; i < donorTotal && st == STATUS_OK; i++) {
        st = records_add_donor(store, SAMPLE_DONORS[i].id, SAMPLE_DONORS[i].name,
                               SAMPLE_DONORS[i].bloodGroup, SAMPLE_DONORS[i].city,
                               SAMPLE_DONORS[i].phone, SAMPLE_DONORS[i].available);
    }
    for (i = 0; i < patientTotal && st == STATUS_OK; i++) {
        st = records_add_patient(store, SAMPLE_PATIENTS[i].id, SAMPLE_PATIENTS[i].name,
                                 SAMPLE_PATIENTS[i].bloodGroup, SAMPLE_PATIENTS[i].city);
    }
    return st;
}