/* compatibility.c - blood group rules (who may donate red blood cells to whom).
 *
 * Owner: Developer 2.
 *
 * This is the ONE place that knows the compatibility table and the edge rule, so the search page,
 * the graph and the matching can never disagree with each other.
 */
#include "compatibility.h"

#include <string.h>

/* Index order (frozen in the header): A+=0 A-=1 B+=2 B-=3 AB+=4 AB-=5 O+=6 O-=7 */
static const char *const BG_NAMES[BG_COUNT] = {
    "A+", "A-", "B+", "B-", "AB+", "AB-", "O+", "O-"
};

/* COMPAT[recipientIndex][donorIndex] == 1 means that donor group may give to that recipient.
   Columns are in the same order as the index list above: A+ A- B+ B- AB+ AB- O+ O-
   There are 27 ones in total. */
static const int COMPAT[BG_COUNT][BG_COUNT] = {
    /* A+  : */ { 1, 1, 0, 0, 0, 0, 1, 1 },
    /* A-  : */ { 0, 1, 0, 0, 0, 0, 0, 1 },
    /* B+  : */ { 0, 0, 1, 1, 0, 0, 1, 1 },
    /* B-  : */ { 0, 0, 0, 1, 0, 0, 0, 1 },
    /* AB+ : */ { 1, 1, 1, 1, 1, 1, 1, 1 },
    /* AB- : */ { 0, 1, 0, 1, 0, 1, 0, 1 },
    /* O+  : */ { 0, 0, 0, 0, 0, 0, 1, 1 },
    /* O-  : */ { 0, 0, 0, 0, 0, 0, 0, 1 }
};

/* Our own ASCII lower-casing, so behaviour does not depend on the C locale. */
static char lower_ascii(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return (char)(c + ('a' - 'A'));
    }
    return c;
}

/* 1 if both strings are equal when upper/lower case is ignored, else 0. */
static int equals_ignore_case(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (lower_ascii(*a) != lower_ascii(*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';        /* equal only if both ended together */
}

int compat_blood_group_index(const char *bloodGroup)
{
    int i;

    if (bloodGroup == NULL) {
        return -1;
    }
    for (i = 0; i < BG_COUNT; i++) {
        if (equals_ignore_case(bloodGroup, BG_NAMES[i])) {
            return i;
        }
    }
    return -1;                              /* no trimming here: " A+" is NOT a valid group */
}

const char *compat_blood_group_name(int index)
{
    if (index < 0 || index >= BG_COUNT) {
        return NULL;
    }
    return BG_NAMES[index];
}

Status compat_normalize_blood_group(const char *input, char out[BG_LEN])
{
    const char *start;
    const char *end;
    char trimmed[BG_LEN];
    size_t len;
    int index;

    if (out != NULL) {
        out[0] = '\0';                      /* callers see "" if we fail */
    }
    if (input == NULL || out == NULL) {
        return STATUS_ERR_INVALID_BLOOD_GROUP;
    }

    /* Trim spaces (only the space character) from both ends. */
    start = input;
    while (*start == ' ') {
        start++;
    }
    end = start + strlen(start);
    while (end > start && end[-1] == ' ') {
        end--;
    }

    /* The longest valid group is 3 chars. Anything that cannot fit 'trimmed' (with its NUL)
       is certainly invalid, and rejecting it here prevents a buffer overflow. */
    len = (size_t)(end - start);
    if (len == 0 || len >= BG_LEN) {
        return STATUS_ERR_INVALID_BLOOD_GROUP;
    }
    memcpy(trimmed, start, len);
    trimmed[len] = '\0';

    /* The index lookup ignores case. We then copy the canonical upper-case spelling
       from our table, which is how "ab-" becomes "AB-". */
    index = compat_blood_group_index(trimmed);
    if (index < 0) {
        return STATUS_ERR_INVALID_BLOOD_GROUP;
    }
    memcpy(out, BG_NAMES[index], strlen(BG_NAMES[index]) + 1);   /* at most 4 bytes: fits BG_LEN */
    return STATUS_OK;
}

int compat_can_donate(const char *donorGroup, const char *patientGroup)
{
    int donorIndex = compat_blood_group_index(donorGroup);
    int patientIndex = compat_blood_group_index(patientGroup);

    if (donorIndex < 0 || patientIndex < 0) {
        return 0;
    }
    return COMPAT[patientIndex][donorIndex];    /* table is [recipient][donor] */
}

int compat_city_matches(const char *donorCity, const char *cityFilter)
{
    if (cityFilter == NULL || cityFilter[0] == '\0') {
        return 1;                           /* no filter: every city matches */
    }
    if (donorCity == NULL) {
        return 0;
    }
    return equals_ignore_case(donorCity, cityFilter);
}

int compat_is_edge(const Donor *donor, const Patient *patient, const char *cityFilter)
{
    if (donor == NULL || patient == NULL) {
        return 0;
    }
    /* THE single edge rule: compatible blood group, donor available, city filter satisfied. */
    if (!compat_can_donate(donor->bloodGroup, patient->bloodGroup)) {
        return 0;
    }
    if (donor->available != 1) {
        return 0;
    }
    return compat_city_matches(donor->city, cityFilter);
}