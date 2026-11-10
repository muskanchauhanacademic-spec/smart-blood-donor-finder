/*
 * records.c - in-memory storage of donors and patients.
 *
 * Both kinds of record live in "dynamic arrays": a malloc'd block plus a count
 * (how many are used) and a capacity (how many fit). When the block is full we
 * ask realloc for a block twice as big. There is NO fixed limit on how many
 * records can be stored - only the computer's memory.
 *
 * VALIDATION: every record is checked here (rules from section G of the spec)
 * BEFORE anything is changed. If any check fails, the store is left exactly as
 * it was. Text is trimmed (leading/trailing spaces removed) and stored trimmed.
 *
 * !!! BORROWED POINTER WARNING !!!
 * records_find_donor() and records_find_patient() return a pointer INTO the
 * array. When records_add_* needs more room it calls realloc, which may MOVE
 * the whole array to a new address. After that, every pointer obtained earlier
 * points to freed memory ("dangling pointer"). So: use a found pointer
 * immediately, and never keep it across a records_add_* call. Copy the data
 * out if you need to keep it. (records_update_donor and
 * records_set_donor_availability never move the array, so they are safe.)
 */
#include <limits.h>
#include <stdint.h>   /* SIZE_MAX */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "records.h"
#include "compatibility.h"   /* compat_normalize_blood_group (Dev 2's module) */

/* The arrays are NAME_LEN etc. bytes long, and one byte is reserved for the
 * terminating '\0', so the longest legal text is one byte shorter. */
#define NAME_MAX_BYTES  (NAME_LEN - 1)    /* 49 */
#define CITY_MAX_BYTES  (CITY_LEN - 1)    /* 29 */
#define PHONE_MIN_CHARS 7
#define PHONE_MAX_CHARS (PHONE_LEN - 1)   /* 14, counting a leading '+' */

/* ------------------------------------------------------------------ */
/* Small private helpers (static = visible only inside this file)      */
/* ------------------------------------------------------------------ */

/* Find the part of 'text' that remains after removing leading and trailing
 * spaces. Nothing is copied: we only report where the trimmed text starts
 * (*start) and how many bytes it has (*length). */
static void trim_bounds(const char *text, const char **start, size_t *length)
{
    const char *first = text;
    const char *last;

    while (*first == ' ') {
        first++;
    }
    last = first + strlen(first);          /* points at the '\0' */
    while (last > first && *(last - 1) == ' ') {
        last--;                            /* step back over trailing spaces */
    }
    *start = first;
    *length = (size_t)(last - first);      /* pointer difference is >= 0 here */
}

/* Copy exactly srcLen bytes of src into dest as a '\0'-terminated string.
 * Returns 1 on success, 0 if it would not fit (we never truncate silently). */
static int copy_text(char *dest, size_t destSize, const char *src, size_t srcLen)
{
    int written;

    /* "%.*s" prints at most srcLen characters. srcLen is always small (<= 49)
     * here, so converting it to int cannot overflow. */
    written = snprintf(dest, destSize, "%.*s", (int)srcLen, src);
    return (written >= 0 && (size_t)written < destSize);
}

/* Validate a name or city: trimmed length 1..maxBytes, no control characters
 * (bytes below 0x20 or 0x7F). Bytes above 0x7F (UTF-8 letters) are allowed.
 * On success the trimmed text is copied to out. Returns 1 = valid, 0 = invalid. */
static int validate_text(const char *input, size_t maxBytes, char *out, size_t outSize)
{
    const char *start;
    size_t length;
    size_t i;

    if (input == NULL) {
        return 0;
    }
    trim_bounds(input, &start, &length);
    if (length == 0 || length > maxBytes) {
        return 0;
    }
    for (i = 0; i < length; i++) {
        unsigned char c = (unsigned char)start[i];   /* cast: char may be signed */
        if (c < 0x20 || c == 0x7F) {
            return 0;
        }
    }
    return copy_text(out, outSize, start, length);
}

/* Validate a phone number: trimmed length 7..14 characters in total, an
 * optional leading '+', and digits only after that. */
static int validate_phone(const char *input, char *out, size_t outSize)
{
    const char *start;
    size_t length;
    size_t i;

    if (input == NULL) {
        return 0;
    }
    trim_bounds(input, &start, &length);
    if (length < PHONE_MIN_CHARS || length > PHONE_MAX_CHARS) {
        return 0;
    }
    i = (start[0] == '+') ? 1 : 0;           /* skip one optional leading '+' */
    for (; i < length; i++) {
        if (start[i] < '0' || start[i] > '9') {
            return 0;
        }
    }
    return copy_text(out, outSize, start, length);
}

/* Use Dev 2's function to trim/upper-case/validate the blood group, then copy
 * the canonical text (e.g. "AB+") into out. Any problem -> INVALID_BLOOD_GROUP. */
static Status normalize_group(const char *input, char *out, size_t outSize)
{
    char canon[BG_LEN];

    if (input == NULL) {
        return STATUS_ERR_INVALID_BLOOD_GROUP;
    }
    memset(canon, 0, sizeof canon);
    if (compat_normalize_blood_group(input, canon) != STATUS_OK) {
        return STATUS_ERR_INVALID_BLOOD_GROUP;
    }
    canon[BG_LEN - 1] = '\0';                /* belt and braces: guarantee termination */
    if (!copy_text(out, outSize, canon, strlen(canon))) {
        return STATUS_ERR_INVALID_BLOOD_GROUP;
    }
    return STATUS_OK;
}

/* Work out the next capacity when an array is full: double it (or start at
 * RECORDS_INITIAL_CAPACITY). Returns 0 if the arithmetic would overflow. */
static int next_capacity(int current, size_t elementSize)
{
    int newCapacity;

    if (current < 1) {
        newCapacity = RECORDS_INITIAL_CAPACITY;
    } else if (current > INT_MAX / 2) {
        return 0;                            /* current * 2 would overflow an int */
    } else {
        newCapacity = current * 2;
    }
    /* newCapacity * elementSize must fit in a size_t, or realloc gets a wrong size. */
    if ((size_t)newCapacity > SIZE_MAX / elementSize) {
        return 0;
    }
    return newCapacity;
}

/* Linear search. Returns the array index of the donor, or -1. */
static int donor_index(const RecordStore *store, int id)
{
    int i;

    if (store->donors.items == NULL) {
        return -1;
    }
    for (i = 0; i < store->donors.count; i++) {
        if (store->donors.items[i].id == id) {
            return i;
        }
    }
    return -1;
}

static int patient_index(const RecordStore *store, int id)
{
    int i;

    if (store->patients.items == NULL) {
        return -1;
    }
    for (i = 0; i < store->patients.count; i++) {
        if (store->patients.items[i].id == id) {
            return i;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Store life cycle                                                    */
/* ------------------------------------------------------------------ */

Status records_init(RecordStore *store)
{
    if (store == NULL) {
        return STATUS_ERR_INTERNAL;
    }
    /* Start from all-zero so a failure below leaves a store that is safe to free.
     * (We do not free first: we cannot know whether old contents are valid.) */
    memset(store, 0, sizeof *store);

    store->donors.items = malloc(RECORDS_INITIAL_CAPACITY * sizeof(Donor));
    if (store->donors.items == NULL) {
        return STATUS_ERR_NOMEM;
    }
    store->donors.capacity = RECORDS_INITIAL_CAPACITY;

    store->patients.items = malloc(RECORDS_INITIAL_CAPACITY * sizeof(Patient));
    if (store->patients.items == NULL) {
        free(store->donors.items);           /* undo the first allocation */
        memset(store, 0, sizeof *store);
        return STATUS_ERR_NOMEM;
    }
    store->patients.capacity = RECORDS_INITIAL_CAPACITY;
    return STATUS_OK;
}

void records_free(RecordStore *store)
{
    if (store == NULL) {
        return;
    }
    free(store->donors.items);               /* free(NULL) is allowed and does nothing */
    free(store->patients.items);
    /* Reset everything so a second records_free (or a late read) is harmless. */
    store->donors.items = NULL;
    store->donors.count = 0;
    store->donors.capacity = 0;
    store->patients.items = NULL;
    store->patients.count = 0;
    store->patients.capacity = 0;
}

/* ------------------------------------------------------------------ */
/* Adding records                                                      */
/* ------------------------------------------------------------------ */

Status records_add_donor(RecordStore *store, int id, const char *name, const char *bloodGroup,
                         const char *city, const char *phone, int available)
{
    Donor fresh;          /* built and validated locally; copied in only if everything is OK */
    Status st;

    if (store == NULL) {
        return STATUS_ERR_INTERNAL;
    }
    memset(&fresh, 0, sizeof fresh);

    /* Checks run in the order required by the spec: id, name, bloodGroup, city,
     * phone, available, duplicate id. The first failure is returned. */
    if (id < 1) {                            /* the upper limit INT_MAX is automatic for an int */
        return STATUS_ERR_INVALID_ID;
    }
    fresh.id = id;

    if (!validate_text(name, NAME_MAX_BYTES, fresh.name, sizeof fresh.name)) {
        return STATUS_ERR_INVALID_NAME;
    }
    st = normalize_group(bloodGroup, fresh.bloodGroup, sizeof fresh.bloodGroup);
    if (st != STATUS_OK) {
        return st;
    }
    if (!validate_text(city, CITY_MAX_BYTES, fresh.city, sizeof fresh.city)) {
        return STATUS_ERR_INVALID_CITY;
    }
    if (!validate_phone(phone, fresh.phone, sizeof fresh.phone)) {
        return STATUS_ERR_INVALID_PHONE;
    }
    if (available != 0 && available != 1) {
        return STATUS_ERR_INVALID_AVAILABILITY;
    }
    fresh.available = available;

    if (donor_index(store, id) >= 0) {
        return STATUS_ERR_DUPLICATE_ID;
    }

    /* Grow the array if it is full. */
    if (store->donors.count >= store->donors.capacity) {
        int newCapacity = next_capacity(store->donors.capacity, sizeof(Donor));
        Donor *bigger;

        if (newCapacity == 0) {
            return STATUS_ERR_NOMEM;
        }
        /* Use a TEMPORARY pointer: if realloc fails it returns NULL but the old
         * block is still valid, and assigning NULL straight to items would leak it. */
        bigger = realloc(store->donors.items, (size_t)newCapacity * sizeof(Donor));
        if (bigger == NULL) {
            return STATUS_ERR_NOMEM;         /* store unchanged */
        }
        store->donors.items = bigger;
        store->donors.capacity = newCapacity;
    }

    store->donors.items[store->donors.count] = fresh;   /* struct copy */
    store->donors.count++;
    return STATUS_OK;
}

Status records_add_patient(RecordStore *store, int id, const char *name, const char *bloodGroup,
                           const char *city)
{
    Patient fresh;
    Status st;

    if (store == NULL) {
        return STATUS_ERR_INTERNAL;
    }
    memset(&fresh, 0, sizeof fresh);

    /* Same order as for donors (a patient has no phone / availability). */
    if (id < 1) {
        return STATUS_ERR_INVALID_ID;
    }
    fresh.id = id;

    if (!validate_text(name, NAME_MAX_BYTES, fresh.name, sizeof fresh.name)) {
        return STATUS_ERR_INVALID_NAME;
    }
    st = normalize_group(bloodGroup, fresh.bloodGroup, sizeof fresh.bloodGroup);
    if (st != STATUS_OK) {
        return st;
    }
    if (!validate_text(city, CITY_MAX_BYTES, fresh.city, sizeof fresh.city)) {
        return STATUS_ERR_INVALID_CITY;
    }
    /* Duplicates are only checked among patients: a donor may share the id. */
    if (patient_index(store, id) >= 0) {
        return STATUS_ERR_DUPLICATE_ID;
    }

    if (store->patients.count >= store->patients.capacity) {
        int newCapacity = next_capacity(store->patients.capacity, sizeof(Patient));
        Patient *bigger;

        if (newCapacity == 0) {
            return STATUS_ERR_NOMEM;
        }
        bigger = realloc(store->patients.items, (size_t)newCapacity * sizeof(Patient));
        if (bigger == NULL) {
            return STATUS_ERR_NOMEM;
        }
        store->patients.items = bigger;
        store->patients.capacity = newCapacity;
    }

    store->patients.items[store->patients.count] = fresh;
    store->patients.count++;
    return STATUS_OK;
}

/* ------------------------------------------------------------------ */
/* Finding and changing records                                        */
/* ------------------------------------------------------------------ */

const Donor *records_find_donor(const RecordStore *store, int id)
{
    int index;

    if (store == NULL) {
        return NULL;
    }
    index = donor_index(store, id);
    if (index < 0) {
        return NULL;
    }
    return &store->donors.items[index];      /* BORROWED: see warning at top of file */
}

const Patient *records_find_patient(const RecordStore *store, int id)
{
    int index;

    if (store == NULL) {
        return NULL;
    }
    index = patient_index(store, id);
    if (index < 0) {
        return NULL;
    }
    return &store->patients.items[index];    /* BORROWED */
}

Status records_update_donor(RecordStore *store, int id, const char *name, const char *city,
                            const char *phone)
{
    /* New values are validated into these temporaries first, so that an error
     * in (say) the phone cannot leave the donor with a new name and old phone. */
    char newName[NAME_LEN];
    char newCity[CITY_LEN];
    char newPhone[PHONE_LEN];
    int index;
    Donor *target;

    if (store == NULL) {
        return STATUS_ERR_INTERNAL;
    }
    if (id < 1) {
        return STATUS_ERR_INVALID_ID;
    }
    if (!validate_text(name, NAME_MAX_BYTES, newName, sizeof newName)) {
        return STATUS_ERR_INVALID_NAME;
    }
    if (!validate_text(city, CITY_MAX_BYTES, newCity, sizeof newCity)) {
        return STATUS_ERR_INVALID_CITY;
    }
    if (!validate_phone(phone, newPhone, sizeof newPhone)) {
        return STATUS_ERR_INVALID_PHONE;
    }
    index = donor_index(store, id);
    if (index < 0) {
        return STATUS_ERR_NOT_FOUND;
    }

    target = &store->donors.items[index];    /* we are non-const here, so we may write */
    /* The texts already fit (same array sizes), so these copies cannot fail. */
    if (!copy_text(target->name, sizeof target->name, newName, strlen(newName)) ||
        !copy_text(target->city, sizeof target->city, newCity, strlen(newCity)) ||
        !copy_text(target->phone, sizeof target->phone, newPhone, strlen(newPhone))) {
        return STATUS_ERR_INTERNAL;
    }
    /* id, bloodGroup and availability are intentionally untouched. */
    return STATUS_OK;
}

Status records_set_donor_availability(RecordStore *store, int id, int available)
{
    int index;

    if (store == NULL) {
        return STATUS_ERR_INTERNAL;
    }
    if (id < 1) {
        return STATUS_ERR_INVALID_ID;
    }
    if (available != 0 && available != 1) {
        return STATUS_ERR_INVALID_AVAILABILITY;
    }
    index = donor_index(store, id);
    if (index < 0) {
        return STATUS_ERR_NOT_FOUND;
    }
    store->donors.items[index].available = available;
    return STATUS_OK;
}

int records_available_donor_count(const RecordStore *store)
{
    int i;
    int total = 0;

    if (store == NULL || store->donors.items == NULL) {
        return 0;
    }
    for (i = 0; i < store->donors.count; i++) {
        if (store->donors.items[i].available == 1) {
            total++;
        }
    }
    return total;
}

/* ------------------------------------------------------------------ */
/* JSON output (formats from section H)                                */
/* ------------------------------------------------------------------ */

void donor_write_json(StrBuf *sb, const Donor *d)
{
    if (sb == NULL || d == NULL) {
        return;
    }
    /* sb_append_json_string adds the quotes and escapes special characters. */
    sb_appendf(sb, "{\"id\":%d,\"name\":", d->id);
    sb_append_json_string(sb, d->name);
    sb_append(sb, ",\"bloodGroup\":");
    sb_append_json_string(sb, d->bloodGroup);
    sb_append(sb, ",\"city\":");
    sb_append_json_string(sb, d->city);
    sb_append(sb, ",\"phone\":");
    sb_append_json_string(sb, d->phone);
    sb_appendf(sb, ",\"available\":%s}", d->available == 1 ? "true" : "false");
}

void patient_write_json(StrBuf *sb, const Patient *p)
{
    if (sb == NULL || p == NULL) {
        return;
    }
    sb_appendf(sb, "{\"id\":%d,\"name\":", p->id);
    sb_append_json_string(sb, p->name);
    sb_append(sb, ",\"bloodGroup\":");
    sb_append_json_string(sb, p->bloodGroup);
    sb_append(sb, ",\"city\":");
    sb_append_json_string(sb, p->city);
    sb_append_char(sb, '}');
}