/*
 * api_records.c - the 8 HTTP handlers for donors and patients.
 *
 * Every handler follows the same pattern:
 *   1. (POST/PUT only) parse the JSON body into a JsonObject.
 *   2. Read each field. If a field is missing or has the wrong type we do NOT
 *      write special error code: we pass an INVALID sentinel value (id 0, an
 *      empty string, available -1) into records_*, and the normal validation
 *      there produces the matching INVALID_* error.
 *   3. Call records_* and turn its Status into an HTTP response.
 *   4. Fall through to ONE cleanup block at the end, so nothing leaks on any
 *      error path. (We reach it with 'goto cleanup;' - the one common and
 *      accepted use of goto in C.)
 *
 * Response shape: http_begin_ok writes {"ok":true,"data": and http_end_ok
 * writes the closing }. In between we write the "data" object ourselves.
 */
#include <stddef.h>
#include <string.h>

#include "api.h"
#include "common.h"
#include "json_util.h"
#include "records.h"

/* Raw (not yet validated) text buffers. They are deliberately LARGER than the
 * stored fields so that padded input such as "   Asha Rao   " can still be
 * trimmed by records_*. Anything longer than these is replaced by "" and is
 * then reported as an invalid field - it is rejected, never truncated. */
#define RAW_TEXT_SIZE   256
#define RAW_GROUP_SIZE  16
#define RAW_PHONE_SIZE  64

/* ------------------------------------------------------------------ */
/* Private helpers                                                     */
/* ------------------------------------------------------------------ */

/* 1 if the request has no body or only whitespace. */
static int body_is_blank(const HttpRequest *req)
{
    const char *p;

    if (req->body == NULL) {
        return 1;
    }
    for (p = req->body; *p != '\0'; p++) {
        if (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
            return 0;
        }
    }
    return 1;
}

/* Parse the request body as one flat JSON object.
 * Blank body or invalid JSON -> BAD_REQUEST. The caller must json_object_free. */
static Status parse_body(const HttpRequest *req, JsonObject *obj)
{
    if (body_is_blank(req)) {
        return STATUS_ERR_BAD_REQUEST;
    }
    return json_parse_object(req->body, obj);
}

/* Read a string field; on ANY problem (missing, not a string, too long) leave
 * "" which later fails validation with the right INVALID_* code. */
static void read_text_field(const JsonObject *obj, const char *key, char *out, size_t outSize)
{
    out[0] = '\0';
    if (json_get_string(obj, key, out, outSize) != STATUS_OK) {
        out[0] = '\0';       /* json_get_string might have written part of the text */
    }
}

/* Read an integer field; on any problem return 0 (an invalid id). */
static int read_id_field(const JsonObject *obj, const char *key)
{
    int value = 0;

    if (json_get_int(obj, key, &value) != STATUS_OK) {
        return 0;
    }
    return value;
}

/* Read a boolean field; on any problem return -1 (invalid availability). */
static int read_availability_field(const JsonObject *obj, const char *key)
{
    int value = -1;

    if (json_get_bool(obj, key, &value) != STATUS_OK) {
        return -1;
    }
    return value;
}

/* Write {"ok":true,"data":{"donor":{...}}} with the given HTTP status. */
static void respond_with_donor(HttpResponse *res, int httpStatus, const Donor *donor)
{
    http_begin_ok(res, httpStatus);
    sb_append(&res->body, "{\"donor\":");
    donor_write_json(&res->body, donor);
    sb_append_char(&res->body, '}');
    http_end_ok(res);
}

static void respond_with_patient(HttpResponse *res, int httpStatus, const Patient *patient)
{
    http_begin_ok(res, httpStatus);
    sb_append(&res->body, "{\"patient\":");
    patient_write_json(&res->body, patient);
    sb_append_char(&res->body, '}');
    http_end_ok(res);
}

/* Shared error text for a bad body. */
static void respond_body_error(HttpResponse *res, Status st)
{
    if (st == STATUS_ERR_BAD_REQUEST) {
        http_send_error(res, st, "Request body must be a flat JSON object");
    } else {
        http_send_error(res, st, NULL);      /* e.g. out of memory: default text */
    }
}

/* ------------------------------------------------------------------ */
/* Donors                                                              */
/* ------------------------------------------------------------------ */

/* GET /api/donors */
void api_get_donors(AppState *app, const HttpRequest *req, HttpResponse *res)
{
    int i;

    (void)req;                               /* this handler needs nothing from the request */
    http_begin_ok(res, 200);
    sb_appendf(&res->body, "{\"count\":%d,\"donors\":[", app->store.donors.count);
    for (i = 0; i < app->store.donors.count; i++) {
        if (i > 0) {
            sb_append_char(&res->body, ',');
        }
        donor_write_json(&res->body, &app->store.donors.items[i]);
    }
    sb_append(&res->body, "]}");
    http_end_ok(res);
}

/* POST /api/donors   body {"id","name","bloodGroup","city","phone","available"} */
void api_post_donor(AppState *app, const HttpRequest *req, HttpResponse *res)
{
    JsonObject obj;
    char name[RAW_TEXT_SIZE];
    char bloodGroup[RAW_GROUP_SIZE];
    char city[RAW_TEXT_SIZE];
    char phone[RAW_PHONE_SIZE];
    const Donor *stored;
    Status st;
    int id;
    int available;

    obj.pairs = NULL;                        /* so json_object_free is safe if we jump early */
    obj.count = 0;

    st = parse_body(req, &obj);
    if (st != STATUS_OK) {
        respond_body_error(res, st);
        goto cleanup;
    }

    id = read_id_field(&obj, "id");
    read_text_field(&obj, "name", name, sizeof name);
    read_text_field(&obj, "bloodGroup", bloodGroup, sizeof bloodGroup);
    read_text_field(&obj, "city", city, sizeof city);
    read_text_field(&obj, "phone", phone, sizeof phone);
    available = read_availability_field(&obj, "available");

    st = records_add_donor(&app->store, id, name, bloodGroup, city, phone, available);
    if (st != STATUS_OK) {
        http_send_error(res, st, NULL);
        goto cleanup;
    }

    /* Look the donor up again so we answer with the STORED (trimmed, canonical)
     * values. Safe: nothing has been added since, so the pointer is still valid. */
    stored = records_find_donor(&app->store, id);
    if (stored == NULL) {
        http_send_error(res, STATUS_ERR_INTERNAL, NULL);   /* cannot happen, but never crash */
        goto cleanup;
    }
    respond_with_donor(res, 201, stored);

cleanup:
    json_object_free(&obj);
}

/* GET /api/donors/{id} */
void api_get_donor(AppState *app, const HttpRequest *req, HttpResponse *res)
{
    const Donor *donor = records_find_donor(&app->store, req->pathId);

    if (donor == NULL) {
        http_send_error(res, STATUS_ERR_NOT_FOUND, "Donor not found");
        return;
    }
    respond_with_donor(res, 200, donor);
}

/* PUT /api/donors/{id}   body {"name","city","phone"} */
void api_put_donor(AppState *app, const HttpRequest *req, HttpResponse *res)
{
    JsonObject obj;
    char name[RAW_TEXT_SIZE];
    char city[RAW_TEXT_SIZE];
    char phone[RAW_PHONE_SIZE];
    const Donor *stored;
    Status st;

    obj.pairs = NULL;
    obj.count = 0;

    st = parse_body(req, &obj);
    if (st != STATUS_OK) {
        respond_body_error(res, st);
        goto cleanup;
    }

    read_text_field(&obj, "name", name, sizeof name);
    read_text_field(&obj, "city", city, sizeof city);
    read_text_field(&obj, "phone", phone, sizeof phone);

    st = records_update_donor(&app->store, req->pathId, name, city, phone);
    if (st == STATUS_ERR_NOT_FOUND) {
        http_send_error(res, st, "Donor not found");
        goto cleanup;
    }
    if (st != STATUS_OK) {
        http_send_error(res, st, NULL);
        goto cleanup;
    }

    stored = records_find_donor(&app->store, req->pathId);
    if (stored == NULL) {
        http_send_error(res, STATUS_ERR_INTERNAL, NULL);
        goto cleanup;
    }
    respond_with_donor(res, 200, stored);

cleanup:
    json_object_free(&obj);
}

/* PUT /api/donors/{id}/availability   body {"available":true|false} */
void api_put_donor_availability(AppState *app, const HttpRequest *req, HttpResponse *res)
{
    JsonObject obj;
    const Donor *stored;
    Status st;
    int available;

    obj.pairs = NULL;
    obj.count = 0;

    st = parse_body(req, &obj);
    if (st != STATUS_OK) {
        respond_body_error(res, st);
        goto cleanup;
    }

    available = read_availability_field(&obj, "available");

    st = records_set_donor_availability(&app->store, req->pathId, available);
    if (st == STATUS_ERR_NOT_FOUND) {
        http_send_error(res, st, "Donor not found");
        goto cleanup;
    }
    if (st != STATUS_OK) {
        http_send_error(res, st, NULL);
        goto cleanup;
    }

    stored = records_find_donor(&app->store, req->pathId);
    if (stored == NULL) {
        http_send_error(res, STATUS_ERR_INTERNAL, NULL);
        goto cleanup;
    }
    respond_with_donor(res, 200, stored);

cleanup:
    json_object_free(&obj);
}

/* ------------------------------------------------------------------ */
/* Patients                                                            */
/* ------------------------------------------------------------------ */

/* GET /api/patients */
void api_get_patients(AppState *app, const HttpRequest *req, HttpResponse *res)
{
    int i;

    (void)req;
    http_begin_ok(res, 200);
    sb_appendf(&res->body, "{\"count\":%d,\"patients\":[", app->store.patients.count);
    for (i = 0; i < app->store.patients.count; i++) {
        if (i > 0) {
            sb_append_char(&res->body, ',');
        }
        patient_write_json(&res->body, &app->store.patients.items[i]);
    }
    sb_append(&res->body, "]}");
    http_end_ok(res);
}

/* POST /api/patients   body {"id","name","bloodGroup","city"} */
void api_post_patient(AppState *app, const HttpRequest *req, HttpResponse *res)
{
    JsonObject obj;
    char name[RAW_TEXT_SIZE];
    char bloodGroup[RAW_GROUP_SIZE];
    char city[RAW_TEXT_SIZE];
    const Patient *stored;
    Status st;
    int id;

    obj.pairs = NULL;
    obj.count = 0;

    st = parse_body(req, &obj);
    if (st != STATUS_OK) {
        respond_body_error(res, st);
        goto cleanup;
    }

    id = read_id_field(&obj, "id");
    read_text_field(&obj, "name", name, sizeof name);
    read_text_field(&obj, "bloodGroup", bloodGroup, sizeof bloodGroup);
    read_text_field(&obj, "city", city, sizeof city);

    st = records_add_patient(&app->store, id, name, bloodGroup, city);
    if (st != STATUS_OK) {
        http_send_error(res, st, NULL);
        goto cleanup;
    }

    stored = records_find_patient(&app->store, id);
    if (stored == NULL) {
        http_send_error(res, STATUS_ERR_INTERNAL, NULL);
        goto cleanup;
    }
    respond_with_patient(res, 201, stored);

cleanup:
    json_object_free(&obj);
}

/* GET /api/patients/{id} */
void api_get_patient(AppState *app, const HttpRequest *req, HttpResponse *res)
{
    const Patient *patient = records_find_patient(&app->store, req->pathId);

    if (patient == NULL) {
        http_send_error(res, STATUS_ERR_NOT_FOUND, "Patient not found");
        return;
    }
    respond_with_patient(res, 200, patient);
}