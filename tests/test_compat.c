/* test_compat.c - standalone tests for backend/compatibility.c (Developer 2).
 *
 * Build and run from the project root:
 *   gcc -std=c11 -Wall -Wextra -pedantic -g -Ibackend -o test_compat tests/test_compat.c backend/compatibility.c
 *   ./test_compat
 *
 * The expected answers are written here as the plain-text lists from section G of the spec
 * ("A+ : A+ A- O+ O-"), NOT as a second copy of the 0/1 matrix. That way a typo in the matrix
 * inside compatibility.c cannot be repeated here by accident.
 */
#include <stdio.h>
#include <string.h>

#include "compatibility.h"

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

static const char *const NAMES[8] = { "A+", "A-", "B+", "B-", "AB+", "AB-", "O+", "O-" };

/* ALLOWED[r] lists the donor groups that may give to recipient NAMES[r] (section G). */
static const char *const ALLOWED[8] = {
    "A+ A- O+ O-",                   /* recipient A+  */
    "A- O-",                         /* recipient A-  */
    "B+ B- O+ O-",                   /* recipient B+  */
    "B- O-",                         /* recipient B-  */
    "A+ A- B+ B- AB+ AB- O+ O-",     /* recipient AB+ */
    "AB- A- B- O-",                  /* recipient AB- */
    "O+ O-",                         /* recipient O+  */
    "O-"                             /* recipient O-  */
};

/* 1 if 'token' is one of the space-separated words in 'list'. */
static int list_contains(const char *list, const char *token)
{
    size_t tokenLen = strlen(token);
    const char *p = list;

    while (*p != '\0') {
        size_t len = 0;
        while (p[len] != '\0' && p[len] != ' ') {
            len++;
        }
        if (len == tokenLen && strncmp(p, token, len) == 0) {
            return 1;
        }
        p += len;
        while (*p == ' ') {
            p++;
        }
    }
    return 0;
}

/* Copy 'src' to 'dst' in lower case (ASCII only). */
static void to_lower_copy(char *dst, size_t dstSize, const char *src)
{
    size_t i;
    for (i = 0; src[i] != '\0' && i + 1 < dstSize; i++) {
        char c = src[i];
        dst[i] = (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
    }
    dst[i] = '\0';
}

static Donor make_donor(const char *bloodGroup, const char *city, int available)
{
    Donor d;
    memset(&d, 0, sizeof d);
    d.id = 1;
    snprintf(d.name, sizeof d.name, "Test Donor");
    snprintf(d.bloodGroup, sizeof d.bloodGroup, "%s", bloodGroup);
    snprintf(d.city, sizeof d.city, "%s", city);
    snprintf(d.phone, sizeof d.phone, "9000000001");
    d.available = available;
    return d;
}

static Patient make_patient(const char *bloodGroup, const char *city)
{
    Patient p;
    memset(&p, 0, sizeof p);
    p.id = 1;
    snprintf(p.name, sizeof p.name, "Test Patient");
    snprintf(p.bloodGroup, sizeof p.bloodGroup, "%s", bloodGroup);
    snprintf(p.city, sizeof p.city, "%s", city);
    return p;
}

static void test_index_and_name(void)
{
    int i;
    int roundTripOk = 1;

    for (i = 0; i < 8; i++) {
        const char *name = compat_blood_group_name(i);
        if (name == NULL || strcmp(name, NAMES[i]) != 0 || compat_blood_group_index(NAMES[i]) != i) {
            roundTripOk = 0;
        }
    }
    check(roundTripOk, "index <-> name round trip for all 8 groups (A+=0 ... O-=7)");

    check(compat_blood_group_index("a+") == 0, "index is case-insensitive: a+ -> 0");
    check(compat_blood_group_index("ab-") == 5, "index is case-insensitive: ab- -> 5");
    check(compat_blood_group_index("o-") == 7, "index is case-insensitive: o- -> 7");
    check(compat_blood_group_index(" A+") == -1, "index does NOT trim: leading space -> -1");
    check(compat_blood_group_index("A+ ") == -1, "index does NOT trim: trailing space -> -1");
    check(compat_blood_group_index("") == -1, "index of empty string -> -1");
    check(compat_blood_group_index(NULL) == -1, "index of NULL -> -1");
    check(compat_blood_group_index("C+") == -1, "index of C+ -> -1");
    check(compat_blood_group_index("A") == -1, "index of A (no sign) -> -1");
    check(compat_blood_group_index("AB") == -1, "index of AB (no sign) -> -1");
    check(compat_blood_group_index("A++") == -1, "index of A++ -> -1");

    check(compat_blood_group_name(-1) == NULL, "name(-1) -> NULL");
    check(compat_blood_group_name(8) == NULL, "name(8) -> NULL");
}

static void test_normalize(void)
{
    char out[BG_LEN];

    check(compat_normalize_blood_group(" a+ ", out) == STATUS_OK && strcmp(out, "A+") == 0,
          "normalize ' a+ ' -> A+");
    check(compat_normalize_blood_group("ab-", out) == STATUS_OK && strcmp(out, "AB-") == 0,
          "normalize 'ab-' -> AB-");
    check(compat_normalize_blood_group("   o-", out) == STATUS_OK && strcmp(out, "O-") == 0,
          "normalize '   o-' -> O-");
    check(compat_normalize_blood_group("Ab+   ", out) == STATUS_OK && strcmp(out, "AB+") == 0,
          "normalize 'Ab+   ' -> AB+");
    check(compat_normalize_blood_group("B+", out) == STATUS_OK && strcmp(out, "B+") == 0,
          "normalize already-canonical B+ unchanged");

    check(compat_normalize_blood_group("", out) == STATUS_ERR_INVALID_BLOOD_GROUP && out[0] == '\0',
          "normalize empty string -> INVALID_BLOOD_GROUP, out is empty");
    check(compat_normalize_blood_group("    ", out) == STATUS_ERR_INVALID_BLOOD_GROUP,
          "normalize only spaces -> INVALID_BLOOD_GROUP");
    check(compat_normalize_blood_group("A +", out) == STATUS_ERR_INVALID_BLOOD_GROUP,
          "normalize with inner space 'A +' -> INVALID_BLOOD_GROUP");
    check(compat_normalize_blood_group("xyz", out) == STATUS_ERR_INVALID_BLOOD_GROUP,
          "normalize 'xyz' -> INVALID_BLOOD_GROUP");
    check(compat_normalize_blood_group("A+B", out) == STATUS_ERR_INVALID_BLOOD_GROUP,
          "normalize 'A+B' -> INVALID_BLOOD_GROUP");
    check(compat_normalize_blood_group("AAAAAAAAAAAAAAAAAAAA", out) == STATUS_ERR_INVALID_BLOOD_GROUP,
          "normalize over-long text -> INVALID_BLOOD_GROUP (no overflow)");
    check(compat_normalize_blood_group(NULL, out) == STATUS_ERR_INVALID_BLOOD_GROUP,
          "normalize NULL -> INVALID_BLOOD_GROUP");
    /* Only the space character is trimmed (documented choice in compatibility.c). */
    check(compat_normalize_blood_group("O+\t", out) == STATUS_ERR_INVALID_BLOOD_GROUP,
          "normalize 'O+<tab>' -> INVALID_BLOOD_GROUP (only spaces are trimmed)");
}

static void test_all_64_combinations(void)
{
    int r;
    int d;
    int mismatches = 0;
    int lowerMismatches = 0;
    int notZeroOrOne = 0;
    int ones = 0;

    for (r = 0; r < 8; r++) {
        for (d = 0; d < 8; d++) {
            int expected = list_contains(ALLOWED[r], NAMES[d]);
            int actual = compat_can_donate(NAMES[d], NAMES[r]);   /* (donor, patient) */
            char lowDonor[BG_LEN];
            char lowPatient[BG_LEN];
            int actualLower;

            if (actual != expected) {
                mismatches++;
                printf("  mismatch: donor %s -> patient %s expected %d got %d\n",
                       NAMES[d], NAMES[r], expected, actual);
            }
            if (actual != 0 && actual != 1) {
                notZeroOrOne++;
            }
            if (actual == 1) {
                ones++;
            }

            to_lower_copy(lowDonor, sizeof lowDonor, NAMES[d]);
            to_lower_copy(lowPatient, sizeof lowPatient, NAMES[r]);
            actualLower = compat_can_donate(lowDonor, lowPatient);
            if (actualLower != expected) {
                lowerMismatches++;
            }
        }
    }

    check(mismatches == 0, "all 64 donor/recipient pairs match the section G lists");
    check(ones == 27, "exactly 27 of the 64 pairs are compatible");
    check(notZeroOrOne == 0, "compat_can_donate returns only 0 or 1");
    check(lowerMismatches == 0, "all 64 pairs give the same answer with lower-case input");

    /* A few well-known facts, as a sanity net on top of the table check. */
    {
        int allGiveToThemselves = 1;
        int oNegGivesToAll = 1;
        int abPosReceivesFromAll = 1;
        int i;
        for (i = 0; i < 8; i++) {
            if (!compat_can_donate(NAMES[i], NAMES[i])) {
                allGiveToThemselves = 0;
            }
            if (!compat_can_donate("O-", NAMES[i])) {
                oNegGivesToAll = 0;
            }
            if (!compat_can_donate(NAMES[i], "AB+")) {
                abPosReceivesFromAll = 0;
            }
        }
        check(allGiveToThemselves, "every group can donate to its own group");
        check(oNegGivesToAll, "O- (universal donor) can donate to every group");
        check(abPosReceivesFromAll, "AB+ (universal recipient) can receive from every group");
    }
    check(compat_can_donate("A+", "O-") == 0, "A+ cannot donate to O-");
    check(compat_can_donate("AB+", "AB-") == 0, "AB+ cannot donate to AB- (Rh+ to Rh-)");

    check(compat_can_donate("X+", "A+") == 0, "invalid donor group -> 0");
    check(compat_can_donate("A+", "X+") == 0, "invalid patient group -> 0");
    check(compat_can_donate("", "") == 0, "both empty -> 0");
    check(compat_can_donate(NULL, "A+") == 0, "NULL donor group -> 0");
    check(compat_can_donate("A+", NULL) == 0, "NULL patient group -> 0");
    check(compat_can_donate(" O-", "A+") == 0, "no trimming: ' O-' is invalid -> 0");
}

static void test_city_matching(void)
{
    check(compat_city_matches("Pune", NULL) == 1, "NULL filter matches any city");
    check(compat_city_matches("Pune", "") == 1, "empty filter matches any city");
    check(compat_city_matches(NULL, NULL) == 1, "NULL city with no filter -> 1");
    check(compat_city_matches("Pune", "Pune") == 1, "same city matches");
    check(compat_city_matches("Pune", "pune") == 1, "match ignores case (filter lower-case)");
    check(compat_city_matches("pune", "PUNE") == 1, "match ignores case (city lower-case)");
    check(compat_city_matches("Nagpur", "Pune") == 0, "different city does not match");
    check(compat_city_matches("Pune", "Pun") == 0, "filter that is a prefix does not match");
    check(compat_city_matches("Pun", "Pune") == 0, "city that is a prefix of the filter does not match");
    check(compat_city_matches("Pune", "Pune ") == 0, "no trimming: trailing space in filter does not match");
    check(compat_city_matches(NULL, "Pune") == 0, "NULL city with a filter -> 0");
}

static void test_is_edge(void)
{
    Donor oneg = make_donor("O-", "Pune", 1);
    Donor oneg_off = make_donor("O-", "Pune", 0);
    Donor apos = make_donor("A+", "Pune", 1);
    Donor oneg_nagpur = make_donor("O-", "Nagpur", 1);
    Patient abpos = make_patient("AB+", "Mumbai");   /* patient city must NOT matter */
    Patient opos = make_patient("O+", "Pune");

    check(compat_is_edge(&oneg, &abpos, NULL) == 1, "edge: compatible + available, no filter");
    check(compat_is_edge(&oneg, &abpos, "") == 1, "edge: compatible + available, empty filter");
    check(compat_is_edge(&oneg, &abpos, "pune") == 1, "edge: city filter matches donor city (case-insensitive)");
    check(compat_is_edge(&oneg, &abpos, "Mumbai") == 0,
          "no edge: filter is checked against the DONOR city, not the patient city");
    check(compat_is_edge(&oneg_off, &abpos, NULL) == 0, "no edge: donor unavailable");
    check(compat_is_edge(&apos, &opos, NULL) == 0, "no edge: A+ donor cannot give to O+ patient");
    check(compat_is_edge(&oneg_nagpur, &abpos, "Pune") == 0, "no edge: donor in another city");
    check(compat_is_edge(NULL, &abpos, NULL) == 0, "no edge: NULL donor");
    check(compat_is_edge(&oneg, NULL, NULL) == 0, "no edge: NULL patient");

    /* The edge rule must equal can_donate && available && city_matches for every combination. */
    {
        int r;
        int d;
        int a;
        int bad = 0;
        const char *filters[3] = { NULL, "Pune", "Delhi" };
        int f;

        for (r = 0; r < 8; r++) {
            for (d = 0; d < 8; d++) {
                for (a = 0; a < 2; a++) {
                    for (f = 0; f < 3; f++) {
                        Donor dn = make_donor(NAMES[d], "Pune", a);
                        Patient pt = make_patient(NAMES[r], "Pune");
                        int expected = compat_can_donate(dn.bloodGroup, pt.bloodGroup) &&
                                       dn.available == 1 &&
                                       compat_city_matches(dn.city, filters[f]);
                        if (compat_is_edge(&dn, &pt, filters[f]) != expected) {
                            bad++;
                        }
                    }
                }
            }
        }
        check(bad == 0, "compat_is_edge equals can_donate && available && city_matches (all 384 cases)");
    }
}

int main(void)
{
    test_index_and_name();
    test_normalize();
    test_all_64_combinations();
    test_city_matching();
    test_is_edge();

    printf("\n%d checks, %d failed\n", g_checks, g_failures);
    printf(g_failures == 0 ? "ALL TESTS PASSED\n" : "SOME TESTS FAILED\n");
    return g_failures == 0 ? 0 : 1;
}