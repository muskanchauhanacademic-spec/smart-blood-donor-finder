/* api_search.c - handler for GET /api/search?patientId=5[&city=Pune][&availableOnly=true|false]
 *
 * Owner: Developer 2.
 *
 * Order of checks (first failure wins):
 *   1. patientId  -> 400 INVALID_ID  if missing, not a whole number, or <= 0
 *   2. city       -> 400 BAD_REQUEST if malformed or longer than 29 bytes (optional parameter)
 *   3. availableOnly -> 400 BAD_REQUEST unless exactly "true" or "false" (default: true)
 *   4. unknown patient -> 404 NOT_FOUND
 * Zero matching donors is NOT an error: it is a 200 with an empty list.
 *
 * Memory: the only allocation is the DonorSearchResult, which is local to this handler and is
 * released in the single cleanup block at the end.
 */
#include "api.h"

#include <limits.h>
#include <string.h>

#include "json_util.h"
#include "records.h"
#include "search.h"

#define PATIENT_ID_TEXT_LEN 16      /* "2147483647" has 10 digits; longer text is rejected */
#define FLAG_TEXT_LEN       8       /* room for "false" and a bit more; longer text is rejected */

/* Accept only plain digits (no sign, no spaces) with a value in 1..INT_MAX.
   Returns 1 and sets *out on success, 0 otherwise. */
static int parse_patient_id(const char *text, int *out)
{
    long long value = 0;            /* long long: 'long' is only 32 bits on Windows */
    const char *p;

    if (text[0] == '\0') {
        return 0;
    }
    for (p = text; *p != '\0'; p++) {
        if (*p < '0' || *p > '9') {
            return 0;
        }
        value = value * 10 + (*p - '0');
        if (value > INT_MAX) {      /* checked every step, so it cannot overflow */
            return 0;
        }
    }
    if (value < 1) {
        return 0;
    }
    *out = (int)value;
    return 1;
}

void api_get_search(AppState *app, const HttpRequest *req, HttpResponse *res)
{
    DonorSearchResult result;
    char idText[PATIENT_ID_TEXT_LEN];
    char city[CITY_LEN];
    char flagText[FLAG_TEXT_LEN];
    const Patient *patient;         /* BORROWED from the store; valid while the store is unchanged */
    int patientId = 0;
    int availableOnly = 1;
    Status st;
    int i;

    /* Set up everything the cleanup block touches BEFORE the first possible jump to it. */
    result.donors = NULL;
    result.count = 0;
    city[0] = '\0';

    if (res == NULL) {
        return;                     /* nowhere to write an answer; nothing allocated yet */
    }
    if (app == NULL || req == NULL) {
        http_send_error(res, STATUS_ERR_INTERNAL, NULL);
        goto done;
    }

    /* 1. patientId (required) */
    st = http_query_get(req, "patientId", idText, sizeof idText);
    if (st == STATUS_ERR_NOT_FOUND) {
        http_send_error(res, STATUS_ERR_INVALID_ID, "patientId query parameter is required");
        goto done;
    }
    if (st != STATUS_OK || !parse_patient_id(idText, &patientId)) {
        http_send_error(res, STATUS_ERR_INVALID_ID, "patientId must be a positive whole number");
        goto done;
    }

    /* 2. city (optional). An absent city means "no filter". */
    st = http_query_get(req, "city", city, sizeof city);
    if (st == STATUS_ERR_NOT_FOUND) {
        city[0] = '\0';
    } else if (st != STATUS_OK) {
        http_send_error(res, STATUS_ERR_BAD_REQUEST,
                        "city filter is malformed or longer than 29 characters");
        goto done;
    }

    /* 3. availableOnly (optional, default true). Only the exact words are accepted. */
    st = http_query_get(req, "availableOnly", flagText, sizeof flagText);
    if (st == STATUS_ERR_NOT_FOUND) {
        availableOnly = 1;
    } else if (st == STATUS_OK && strcmp(flagText, "true") == 0) {
        availableOnly = 1;
    } else if (st == STATUS_OK && strcmp(flagText, "false") == 0) {
        availableOnly = 0;
    } else {
        http_send_error(res, STATUS_ERR_BAD_REQUEST, "availableOnly must be true or false");
        goto done;
    }

    /* 4. The patient must exist: we also need the record to print it in the answer. */
    patient = records_find_patient(&app->store, patientId);
    if (patient == NULL) {
        http_send_error(res, STATUS_ERR_NOT_FOUND, "no patient with that id");
        goto done;
    }

    st = search_compatible_donors(&app->store, patientId, city, availableOnly, &result);
    if (st != STATUS_OK) {
        http_send_error(res, st, NULL);     /* NULL detail: use the default message for this status */
        goto done;
    }

    /* Success: write {"patient":..,"filters":..,"count":N,"donors":[..]} inside the envelope. */
    http_begin_ok(res, 200);
    sb_append(&res->body, "{\"patient\":");
    patient_write_json(&res->body, patient);
    sb_append(&res->body, ",\"filters\":{\"city\":");
    sb_append_json_string(&res->body, city);        /* escapes quotes, so the JSON stays valid */
    sb_appendf(&res->body, ",\"availableOnly\":%s},\"count\":%d,\"donors\":[",
               availableOnly ? "true" : "false", result.count);
    for (i = 0; i < result.count; i++) {
        if (i > 0) {
            sb_append_char(&res->body, ',');
        }
        donor_write_json(&res->body, &result.donors[i]);
    }
    sb_append(&res->body, "]}");
    http_end_ok(res);                               /* adds the closing brace; turns an
                                                       out-of-memory body into a 500 error */

done:
    /* Single cleanup block: runs on success and on every error path. */
    search_result_free(&result);
}