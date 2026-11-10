/* search.c - find donors that may donate to one patient.
 *
 * Owner: Developer 2.
 *
 * Two passes over the donor array: pass 1 only COUNTS the matches, pass 2 copies them into an
 * array of exactly the right size. This avoids guessing a capacity and avoids malloc(0).
 */
#include "search.h"

#include <stdint.h>
#include <stdlib.h>

#include "compatibility.h"

/* The one matching rule, used by BOTH passes so they can never disagree. */
static int donor_matches(const Donor *donor, const Patient *patient,
                         const char *cityFilter, int availableOnly)
{
    if (!compat_can_donate(donor->bloodGroup, patient->bloodGroup)) {
        return 0;
    }
    if (!compat_city_matches(donor->city, cityFilter)) {
        return 0;
    }
    if (availableOnly && donor->available != 1) {
        return 0;
    }
    return 1;
}

Status search_compatible_donors(const RecordStore *store, int patientId, const char *cityFilter,
                                int availableOnly, DonorSearchResult *out)
{
    const Patient *patient;
    Donor *copies;
    int count = 0;
    int filled = 0;
    int i;

    if (out == NULL) {
        return STATUS_ERR_INTERNAL;
    }
    out->donors = NULL;                     /* start empty so every error path leaves a clean result */
    out->count = 0;
    if (store == NULL) {
        return STATUS_ERR_INTERNAL;
    }

    /* 'patient' is BORROWED from the store: we only read it and never keep it. */
    patient = records_find_patient(store, patientId);
    if (patient == NULL) {
        return STATUS_ERR_NOT_FOUND;
    }

    /* Pass 1: count. */
    for (i = 0; i < store->donors.count; i++) {
        if (donor_matches(&store->donors.items[i], patient, cityFilter, availableOnly)) {
            count++;
        }
    }
    if (count == 0) {
        return STATUS_OK;                   /* donors stays NULL: we never call malloc(0) */
    }

    /* Guard the multiplication count * sizeof(Donor) against overflow. */
    if ((size_t)count > SIZE_MAX / sizeof(Donor)) {
        return STATUS_ERR_NOMEM;
    }
    copies = malloc((size_t)count * sizeof(Donor));
    if (copies == NULL) {
        return STATUS_ERR_NOMEM;
    }

    /* Pass 2: copy the matching Donor structs, keeping the store order. Struct assignment copies
       the char arrays too, so the result stays valid even if the store changes later. The
       'filled < count' test is a safety net; the store is not modified between the two passes. */
    for (i = 0; i < store->donors.count && filled < count; i++) {
        if (donor_matches(&store->donors.items[i], patient, cityFilter, availableOnly)) {
            copies[filled] = store->donors.items[i];
            filled++;
        }
    }

    out->donors = copies;
    out->count = filled;
    return STATUS_OK;
}

void search_result_free(DonorSearchResult *r)
{
    if (r == NULL) {
        return;
    }
    free(r->donors);                        /* free(NULL) is fine for an empty result */
    r->donors = NULL;
    r->count = 0;
}