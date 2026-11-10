/*
 * test_records.c - standalone tests for backend/records.c
 *
 * Build and run from the project root:   make test
 * or by hand:
 *   gcc -std=c11 -Wall -Wextra -pedantic -g -Ibackend -o tests/test_records.exe \
 *       tests/test_records.c backend/records.c backend/common.c
 *   tests/test_records.exe
 *
 * Only records.c and common.c are linked. The two things records.c needs from
 * OTHER developers' modules are replaced here by tiny throw-away STUBS:
 *   - compat_normalize_blood_group (Dev 2)   - the StrBuf functions sb_* (Dev 2)
 * The stubs live ONLY in this file, never in backend/.
 *
 * Every check prints PASS or FAIL. The program returns 0 only if all passed.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "json_util.h"
#include "records.h"
#include "compatibility.h"

/* ================================================================== */
/* STUBS of other developers' code (test only)                         */
/* ================================================================== */

/* Stub: trim spaces, upper-case, accept only the 8 real blood groups. */
Status compat_normalize_blood_group(const char *input, char out[BG_LEN])
{
    static const char *const GROUPS[BG_COUNT] = { "A+", "A-", "B+", "B-", "AB+", "AB-", "O+", "O-" };
    char text[16];
    size_t start = 0;
    size_t end = strlen(input);
    size_t length;
    size_t i;
    int g;

    while (input[start] == ' ') {
        start++;
    }
    while (end > start && input[end - 1] == ' ') {
        end--;
    }
    length = end - start;
    if (length == 0 || length >= sizeof text) {
        return STATUS_ERR_INVALID_BLOOD_GROUP;
    }
    for (i = 0; i < length; i++) {
        char c = input[start + i];
        text[i] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
    }
    text[length] = '\0';
    for (g = 0; g < BG_COUNT; g++) {
        if (strcmp(text, GROUPS[g]) == 0) {
            strcpy(out, GROUPS[g]);          /* safe: every group is at most 3 chars + NUL */
            return STATUS_OK;
        }
    }
    return STATUS_ERR_INVALID_BLOOD_GROUP;
}

/* Stub StrBuf: just enough for records.c's JSON writers and for this test. */
static void stub_append_bytes(StrBuf *sb, const char *data, size_t n)
{
    char *bigger;

    if (sb->failed) {
        return;
    }
    if (sb->length + n + 1 > sb->capacity) {
        size_t newCapacity = (sb->length + n + 1) * 2;
        bigger = realloc(sb->data, newCapacity);
        if (bigger == NULL) {
            sb->failed = 1;
            return;
        }
        sb->data = bigger;
        sb->capacity = newCapacity;
    }
    memcpy(sb->data + sb->length, data, n);
    sb->length += n;
    sb->data[sb->length] = '\0';
}

void sb_init(StrBuf *sb)                  { memset(sb, 0, sizeof *sb); }
void sb_free(StrBuf *sb)                  { free(sb->data); memset(sb, 0, sizeof *sb); }
void sb_append(StrBuf *sb, const char *t) { stub_append_bytes(sb, t, strlen(t)); }
void sb_append_char(StrBuf *sb, char c)   { stub_append_bytes(sb, &c, 1); }
const char *sb_cstr(const StrBuf *sb)     { return sb->data != NULL ? sb->data : ""; }

void sb_appendf(StrBuf *sb, const char *fmt, ...)
{
    char text[256];
    va_list args;
    int n;

    va_start(args, fmt);
    n = vsnprintf(text, sizeof text, fmt, args);
    va_end(args);
    if (n < 0 || (size_t)n >= sizeof text) {
        sb->failed = 1;
        return;
    }
    stub_append_bytes(sb, text, (size_t)n);
}

void sb_append_json_string(StrBuf *sb, const char *text)
{
    sb_append_char(sb, '"');
    for (; *text != '\0'; text++) {
        if (*text == '"' || *text == '\\') {
            sb_append_char(sb, '\\');
        }
        sb_append_char(sb, *text);
    }
    sb_append_char(sb, '"');
}

/* ================================================================== */
/* Tiny test framework                                                 */
/* ================================================================== */

typedef struct { int passed; int failed; } Tally;

static void check(Tally *t, int condition, const char *label)
{
    if (condition) {
        t->passed++;
        printf("PASS: %s\n", label);
    } else {
        t->failed++;
        printf("FAIL: %s\n", label);
    }
}

static int str_equal(const char *a, const char *b)
{
    return strcmp(a, b) == 0;
}

/* Build a text of 'length' copies of 'letter' (for over-long input tests). */
static void fill_text(char *buffer, size_t bufferSize, size_t length, char letter)
{
    size_t i;

    if (length + 1 > bufferSize) {
        length = bufferSize - 1;
    }
    for (i = 0; i < length; i++) {
        buffer[i] = letter;
    }
    buffer[length] = '\0';
}

/* ================================================================== */
/* Tests                                                               */
/* ================================================================== */

static void test_empty_store(Tally *t)
{
    RecordStore store;
    RecordStore zeroed;

    printf("\n--- empty store ---\n");
    check(t, records_init(&store) == STATUS_OK, "records_init succeeds");
    check(t, store.donors.count == 0 && store.patients.count == 0, "new store has 0 donors and 0 patients");
    check(t, store.donors.capacity == RECORDS_INITIAL_CAPACITY, "initial donor capacity is RECORDS_INITIAL_CAPACITY");
    check(t, records_find_donor(&store, 1) == NULL, "find_donor on empty store is NULL");
    check(t, records_find_patient(&store, 1) == NULL, "find_patient on empty store is NULL");
    check(t, records_available_donor_count(&store) == 0, "available count on empty store is 0");
    records_free(&store);
    check(t, store.donors.items == NULL && store.patients.items == NULL, "records_free sets pointers to NULL");
    records_free(&store);                    /* second free must not crash */
    check(t, 1, "records_free twice does not crash");

    memset(&zeroed, 0, sizeof zeroed);
    records_free(&zeroed);                   /* never initialised */
    check(t, 1, "records_free on a zeroed store does not crash");
    check(t, records_find_donor(&zeroed, 1) == NULL, "find on a zeroed store is NULL");
    check(t, records_add_donor(&zeroed, 1, "Asha", "O+", "Pune", "9000000001", 1) == STATUS_OK,
           "adding to a zeroed (never initialised) store still works");
    records_free(&zeroed);
    records_free(NULL);
    check(t, 1, "records_free(NULL) does not crash");
}

static void test_add_one_donor(Tally *t)
{
    RecordStore store;
    const Donor *d;

    printf("\n--- add one donor ---\n");
    records_init(&store);
    check(t, records_add_donor(&store, 101, "  Asha Rao  ", " o+ ", "  Nagpur ", " 9000000101 ", 1) == STATUS_OK,
          "add valid donor (padded input, lower-case group)");
    check(t, store.donors.count == 1, "donor count is 1");
    d = records_find_donor(&store, 101);
    check(t, d != NULL, "donor 101 can be found");
    if (d != NULL) {
        check(t, d->id == 101, "id stored");
        check(t, str_equal(d->name, "Asha Rao"), "name stored trimmed");
        check(t, str_equal(d->bloodGroup, "O+"), "blood group stored canonical upper-case");
        check(t, str_equal(d->city, "Nagpur"), "city stored trimmed");
        check(t, str_equal(d->phone, "9000000101"), "phone stored trimmed");
        check(t, d->available == 1, "available stored");
    }
    check(t, records_add_donor(&store, 102, "Zo\xc3\xab", "AB-", "Pune", "+919876543210", 0) == STATUS_OK,
          "UTF-8 bytes in a name and a leading + in a phone are accepted");
    records_free(&store);
}

static void test_add_many(Tally *t)
{
    static const char *const GROUPS[BG_COUNT] = { "A+", "A-", "B+", "B-", "AB+", "AB-", "O+", "O-" };
    RecordStore store;
    char name[NAME_LEN];
    char phone[PHONE_LEN];
    int i;
    int allDonorsOk = 1;
    int allPatientsOk = 1;
    int addsOk = 1;

    printf("\n--- many records (forces several reallocs) ---\n");
    records_init(&store);
    for (i = 1; i <= 100; i++) {
        snprintf(name, sizeof name, "Donor %d", i);
        snprintf(phone, sizeof phone, "90000%05d", i);
        if (records_add_donor(&store, i, name, GROUPS[i % BG_COUNT], "Pune", phone, i % 2) != STATUS_OK) {
            addsOk = 0;
        }
        snprintf(name, sizeof name, "Patient %d", i);
        if (records_add_patient(&store, i, name, GROUPS[(i + 3) % BG_COUNT], "Nagpur") != STATUS_OK) {
            addsOk = 0;
        }
    }
    check(t, addsOk, "100 donors and 100 patients all added");
    check(t, store.donors.count == 100 && store.patients.count == 100, "counts are 100 and 100");
    check(t, store.donors.capacity >= 100 && store.donors.capacity > RECORDS_INITIAL_CAPACITY,
          "donor array grew beyond its initial capacity");
    check(t, store.patients.capacity >= 100, "patient array grew");

    /* After many reallocs, every record must still hold the right data. */
    for (i = 1; i <= 100; i++) {
        const Donor *d = records_find_donor(&store, i);
        const Patient *p = records_find_patient(&store, i);

        snprintf(name, sizeof name, "Donor %d", i);
        if (d == NULL || !str_equal(d->name, name) || !str_equal(d->bloodGroup, GROUPS[i % BG_COUNT]) ||
            d->available != i % 2) {
            allDonorsOk = 0;
        }
        snprintf(name, sizeof name, "Patient %d", i);
        if (p == NULL || !str_equal(p->name, name) || !str_equal(p->bloodGroup, GROUPS[(i + 3) % BG_COUNT])) {
            allPatientsOk = 0;
        }
    }
    check(t, allDonorsOk, "all 100 donors intact after reallocs");
    check(t, allPatientsOk, "all 100 patients intact after reallocs");
    check(t, records_available_donor_count(&store) == 50, "50 of the 100 donors are available");
    records_free(&store);
}

static void test_duplicates(Tally *t)
{
    RecordStore store;

    printf("\n--- duplicate ids ---\n");
    records_init(&store);
    check(t, records_add_donor(&store, 7, "Asha", "O+", "Pune", "9000000007", 1) == STATUS_OK, "add donor 7");
    check(t, records_add_donor(&store, 7, "Other", "A+", "Nagpur", "9000000008", 0) == STATUS_ERR_DUPLICATE_ID,
          "second donor with id 7 -> DUPLICATE_ID");
    check(t, store.donors.count == 1, "donor count still 1 after duplicate");
    check(t, str_equal(records_find_donor(&store, 7)->name, "Asha"), "original donor 7 not overwritten");

    check(t, records_add_patient(&store, 7, "Ravi", "A-", "Pune") == STATUS_OK,
          "patient with the SAME id as a donor is allowed");
    check(t, records_add_patient(&store, 7, "Ravi 2", "B+", "Pune") == STATUS_ERR_DUPLICATE_ID,
          "second patient with id 7 -> DUPLICATE_ID");
    check(t, store.patients.count == 1, "patient count still 1 after duplicate");
    records_free(&store);
}

static void test_invalid_inputs(Tally *t)
{
    RecordStore store;
    char longText[128];

    printf("\n--- validation ---\n");
    records_init(&store);

    /* blood group */
    check(t, records_add_donor(&store, 1, "A", "Z+", "Pune", "9000000001", 1) == STATUS_ERR_INVALID_BLOOD_GROUP,
          "unknown blood group Z+");
    check(t, records_add_donor(&store, 1, "A", "", "Pune", "9000000001", 1) == STATUS_ERR_INVALID_BLOOD_GROUP,
          "empty blood group");
    check(t, records_add_donor(&store, 1, "A", NULL, "Pune", "9000000001", 1) == STATUS_ERR_INVALID_BLOOD_GROUP,
          "NULL blood group");
    check(t, records_add_patient(&store, 1, "A", "AB", "Pune") == STATUS_ERR_INVALID_BLOOD_GROUP,
          "patient blood group AB (no sign)");

    /* availability */
    check(t, records_add_donor(&store, 1, "A", "O+", "Pune", "9000000001", 2) == STATUS_ERR_INVALID_AVAILABILITY,
          "availability 2");
    check(t, records_add_donor(&store, 1, "A", "O+", "Pune", "9000000001", -1) == STATUS_ERR_INVALID_AVAILABILITY,
          "availability -1");

    /* id */
    check(t, records_add_donor(&store, 0, "A", "O+", "Pune", "9000000001", 1) == STATUS_ERR_INVALID_ID, "id 0");
    check(t, records_add_donor(&store, -5, "A", "O+", "Pune", "9000000001", 1) == STATUS_ERR_INVALID_ID, "id -5");
    check(t, records_add_patient(&store, 0, "A", "O+", "Pune") == STATUS_ERR_INVALID_ID, "patient id 0");
    check(t, records_add_donor(&store, 2147483647, "A", "O+", "Pune", "9000000001", 1) == STATUS_OK,
          "id 2147483647 (the maximum) is accepted");

    /* name */
    fill_text(longText, sizeof longText, 50, 'x');
    check(t, records_add_donor(&store, 2, longText, "O+", "Pune", "9000000001", 1) == STATUS_ERR_INVALID_NAME,
          "50-byte name is rejected, not truncated");
    fill_text(longText, sizeof longText, 49, 'x');
    check(t, records_add_donor(&store, 2, longText, "O+", "Pune", "9000000001", 1) == STATUS_OK,
          "49-byte name is accepted");
    check(t, strlen(records_find_donor(&store, 2)->name) == 49, "49-byte name stored in full");
    check(t, records_add_donor(&store, 3, "   ", "O+", "Pune", "9000000001", 1) == STATUS_ERR_INVALID_NAME,
          "name of only spaces");
    check(t, records_add_donor(&store, 3, "A\tB", "O+", "Pune", "9000000001", 1) == STATUS_ERR_INVALID_NAME,
          "name with a tab (control character)");
    check(t, records_add_donor(&store, 3, "A\x7F" "B", "O+", "Pune", "9000000001", 1) == STATUS_ERR_INVALID_NAME,
          "name with DEL (0x7F)");
    check(t, records_add_donor(&store, 3, NULL, "O+", "Pune", "9000000001", 1) == STATUS_ERR_INVALID_NAME,
          "NULL name");

    /* city */
    fill_text(longText, sizeof longText, 30, 'c');
    check(t, records_add_donor(&store, 3, "A", "O+", longText, "9000000001", 1) == STATUS_ERR_INVALID_CITY,
          "30-byte city rejected");
    fill_text(longText, sizeof longText, 29, 'c');
    check(t, records_add_donor(&store, 3, "A", "O+", longText, "9000000001", 1) == STATUS_OK,
          "29-byte city accepted");
    check(t, records_add_donor(&store, 4, "A", "O+", "", "9000000001", 1) == STATUS_ERR_INVALID_CITY,
          "empty city");
    check(t, records_add_patient(&store, 4, "A", "O+", "Pu\nne") == STATUS_ERR_INVALID_CITY,
          "patient city with newline");

    /* phone */
    check(t, records_add_donor(&store, 4, "A", "O+", "Pune", "123456", 1) == STATUS_ERR_INVALID_PHONE,
          "phone with 6 digits (too short)");
    check(t, records_add_donor(&store, 4, "A", "O+", "Pune", "1234567", 1) == STATUS_OK,
          "phone with exactly 7 digits accepted");
    check(t, records_add_donor(&store, 5, "A", "O+", "Pune", "12345678901234", 1) == STATUS_OK,
          "phone with exactly 14 digits accepted");
    check(t, records_add_donor(&store, 6, "A", "O+", "Pune", "123456789012345", 1) == STATUS_ERR_INVALID_PHONE,
          "phone with 15 digits rejected");
    check(t, records_add_donor(&store, 6, "A", "O+", "Pune", "98765abc10", 1) == STATUS_ERR_INVALID_PHONE,
          "phone with letters");
    check(t, records_add_donor(&store, 6, "A", "O+", "Pune", "98765-43210", 1) == STATUS_ERR_INVALID_PHONE,
          "phone with a dash");
    check(t, records_add_donor(&store, 6, "A", "O+", "Pune", "98+7654321", 1) == STATUS_ERR_INVALID_PHONE,
          "plus sign in the middle");
    check(t, records_add_donor(&store, 6, "A", "O+", "Pune", "+", 1) == STATUS_ERR_INVALID_PHONE,
          "phone that is only +");
    check(t, records_add_donor(&store, 6, "A", "O+", "Pune", NULL, 1) == STATUS_ERR_INVALID_PHONE,
          "NULL phone");

    /* order of checks: id, name, bloodGroup, city, phone, available, duplicate */
    check(t, records_add_donor(&store, 0, "", "??", "", "x", 9) == STATUS_ERR_INVALID_ID,
          "everything wrong -> INVALID_ID is reported first");
    check(t, records_add_donor(&store, 9, "", "??", "", "x", 9) == STATUS_ERR_INVALID_NAME,
          "then INVALID_NAME");
    check(t, records_add_donor(&store, 9, "A", "??", "", "x", 9) == STATUS_ERR_INVALID_BLOOD_GROUP,
          "then INVALID_BLOOD_GROUP");
    check(t, records_add_donor(&store, 9, "A", "O+", "", "x", 9) == STATUS_ERR_INVALID_CITY,
          "then INVALID_CITY");
    check(t, records_add_donor(&store, 9, "A", "O+", "Pune", "x", 9) == STATUS_ERR_INVALID_PHONE,
          "then INVALID_PHONE");
    check(t, records_add_donor(&store, 2, "A", "O+", "Pune", "9000000001", 9) == STATUS_ERR_INVALID_AVAILABILITY,
          "then INVALID_AVAILABILITY (before the duplicate check)");
    check(t, records_add_donor(&store, 2, "A", "O+", "Pune", "9000000001", 1) == STATUS_ERR_DUPLICATE_ID,
          "finally DUPLICATE_ID");

    /* only the 5 valid donors above were stored: ids 2147483647, 2, 3, 4, 5 */
    check(t, store.donors.count == 5, "failed adds did not change the donor count");
    check(t, store.patients.count == 0, "failed patient adds did not change the patient count");
    records_free(&store);
}

static void test_find(Tally *t)
{
    RecordStore store;

    printf("\n--- find ---\n");
    records_init(&store);
    records_add_donor(&store, 10, "Asha", "O+", "Pune", "9000000010", 1);
    records_add_patient(&store, 20, "Ravi", "A-", "Pune");
    check(t, records_find_donor(&store, 10) != NULL, "present donor found");
    check(t, records_find_donor(&store, 11) == NULL, "absent donor -> NULL");
    check(t, records_find_donor(&store, 20) == NULL, "patient id is not found as a donor");
    check(t, records_find_patient(&store, 20) != NULL, "present patient found");
    check(t, records_find_patient(&store, 10) == NULL, "donor id is not found as a patient");
    check(t, records_find_donor(NULL, 10) == NULL, "find_donor(NULL store) -> NULL");
    records_free(&store);
}

static void test_update_donor(Tally *t)
{
    RecordStore store;
    const Donor *d;

    printf("\n--- update donor ---\n");
    records_init(&store);
    records_add_donor(&store, 10, "Asha Rao", "O+", "Nagpur", "9000000010", 1);

    check(t, records_update_donor(&store, 10, " Asha R. ", " Pune ", "+919000000010") == STATUS_OK,
          "valid update succeeds");
    d = records_find_donor(&store, 10);
    check(t, d != NULL && str_equal(d->name, "Asha R.") && str_equal(d->city, "Pune") &&
             str_equal(d->phone, "+919000000010"), "name, city, phone changed (trimmed)");
    check(t, d != NULL && str_equal(d->bloodGroup, "O+") && d->available == 1 && d->id == 10,
          "blood group, availability and id unchanged");

    check(t, records_update_donor(&store, 10, "New Name", "New City", "abc") == STATUS_ERR_INVALID_PHONE,
          "update with bad phone -> INVALID_PHONE");
    d = records_find_donor(&store, 10);
    check(t, d != NULL && str_equal(d->name, "Asha R.") && str_equal(d->city, "Pune"),
          "failed update changed NOTHING (not even the valid name)");
    check(t, records_update_donor(&store, 10, "", "Pune", "9000000010") == STATUS_ERR_INVALID_NAME, "empty name");
    check(t, records_update_donor(&store, 10, "A", "", "9000000010") == STATUS_ERR_INVALID_CITY, "empty city");
    check(t, records_update_donor(&store, 99, "A", "B", "9000000010") == STATUS_ERR_NOT_FOUND,
          "unknown donor -> NOT_FOUND");
    check(t, records_update_donor(&store, 0, "A", "B", "9000000010") == STATUS_ERR_INVALID_ID, "id 0 -> INVALID_ID");
    records_free(&store);
}

static void test_availability(Tally *t)
{
    RecordStore store;

    printf("\n--- availability ---\n");
    records_init(&store);
    records_add_donor(&store, 1, "A", "O+", "Pune", "9000000001", 1);
    records_add_donor(&store, 2, "B", "O-", "Pune", "9000000002", 1);
    check(t, records_available_donor_count(&store) == 2, "2 available at the start");

    check(t, records_set_donor_availability(&store, 1, 0) == STATUS_OK, "set donor 1 unavailable");
    check(t, records_find_donor(&store, 1)->available == 0, "donor 1 now 0");
    check(t, records_available_donor_count(&store) == 1, "available count is 1");
    check(t, records_set_donor_availability(&store, 1, 1) == STATUS_OK, "set donor 1 available again");
    check(t, records_available_donor_count(&store) == 2, "available count back to 2");
    check(t, records_set_donor_availability(&store, 1, 2) == STATUS_ERR_INVALID_AVAILABILITY, "value 2 rejected");
    check(t, records_find_donor(&store, 1)->available == 1, "rejected value left donor unchanged");
    check(t, records_set_donor_availability(&store, 99, 1) == STATUS_ERR_NOT_FOUND, "unknown donor -> NOT_FOUND");
    check(t, records_set_donor_availability(&store, 0, 1) == STATUS_ERR_INVALID_ID, "id 0 -> INVALID_ID");
    records_free(&store);
}

static void test_json(Tally *t)
{
    RecordStore store;
    StrBuf sb;

    printf("\n--- JSON writers ---\n");
    records_init(&store);
    records_add_donor(&store, 101, "Asha Rao", "O+", "Nagpur", "9000000101", 1);
    records_add_donor(&store, 102, "Quote \"Q\"", "A-", "Pune", "9000000102", 0);
    records_add_patient(&store, 1, "Ravi Kulkarni", "A-", "Pune");

    sb_init(&sb);
    donor_write_json(&sb, records_find_donor(&store, 101));
    check(t, str_equal(sb_cstr(&sb),
          "{\"id\":101,\"name\":\"Asha Rao\",\"bloodGroup\":\"O+\",\"city\":\"Nagpur\","
          "\"phone\":\"9000000101\",\"available\":true}"), "donor JSON matches section H exactly");
    sb_free(&sb);

    sb_init(&sb);
    donor_write_json(&sb, records_find_donor(&store, 102));
    check(t, strstr(sb_cstr(&sb), "\"available\":false}") != NULL, "unavailable donor writes false");
    check(t, strstr(sb_cstr(&sb), "Quote \\\"Q\\\"") != NULL, "quotes in a name are escaped");
    sb_free(&sb);

    sb_init(&sb);
    patient_write_json(&sb, records_find_patient(&store, 1));
    check(t, str_equal(sb_cstr(&sb),
          "{\"id\":1,\"name\":\"Ravi Kulkarni\",\"bloodGroup\":\"A-\",\"city\":\"Pune\"}"),
          "patient JSON matches section H exactly");
    sb_free(&sb);
    records_free(&store);
}

static void test_status_helpers(Tally *t)
{
    printf("\n--- common.c status table ---\n");
    check(t, status_http_code(STATUS_OK) == 200 && str_equal(status_code_string(STATUS_OK), "OK"), "OK -> 200 \"OK\"");
    check(t, status_http_code(STATUS_ERR_NOMEM) == 500 && str_equal(status_code_string(STATUS_ERR_NOMEM), "OUT_OF_MEMORY"),
          "NOMEM -> 500 OUT_OF_MEMORY");
    check(t, status_http_code(STATUS_ERR_DUPLICATE_ID) == 409, "DUPLICATE_ID -> 409");
    check(t, status_http_code(STATUS_ERR_NOT_FOUND) == 404, "NOT_FOUND -> 404");
    check(t, status_http_code(STATUS_ERR_METHOD_NOT_ALLOWED) == 405, "METHOD_NOT_ALLOWED -> 405");
    check(t, status_http_code(STATUS_ERR_PAYLOAD_TOO_LARGE) == 413, "PAYLOAD_TOO_LARGE -> 413");
    check(t, status_http_code(STATUS_ERR_INVALID_PHONE) == 400 &&
             str_equal(status_code_string(STATUS_ERR_INVALID_PHONE), "INVALID_PHONE"), "INVALID_PHONE -> 400");
    check(t, str_equal(status_code_string(STATUS_ERR_INTERNAL), "INTERNAL_ERROR"), "INTERNAL -> INTERNAL_ERROR");
    check(t, status_http_code((Status)999) == 500, "unknown status value -> 500");
    check(t, status_message(STATUS_ERR_BAD_REQUEST)[0] != '\0', "every status has a message");
}

int main(void)
{
    Tally tally;

    tally.passed = 0;
    tally.failed = 0;

    test_empty_store(&tally);
    test_add_one_donor(&tally);
    test_add_many(&tally);
    test_duplicates(&tally);
    test_invalid_inputs(&tally);
    test_find(&tally);
    test_update_donor(&tally);
    test_availability(&tally);
    test_json(&tally);
    test_status_helpers(&tally);

    printf("\n==== %d passed, %d failed ====\n", tally.passed, tally.failed);
    return (tally.failed == 0) ? 0 : 1;
}