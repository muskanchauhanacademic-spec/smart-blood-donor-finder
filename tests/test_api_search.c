/* test_api_search.c - standalone tests for backend/api_search.c (Developer 2).
 *
 * Build and run from the project root:
 *   gcc -std=c11 -Wall -Wextra -pedantic -g -Ibackend -o test_api_search \
 *       tests/test_api_search.c backend/api_search.c backend/search.c \
 *       backend/compatibility.c backend/json_util.c
 *   ./test_api_search
 *
 * api_search.c needs Developer 1's helpers (http_query_get, http_begin_ok, http_end_ok,
 * http_send_error, records_find_patient, patient_write_json, donor_write_json). They are replaced
 * below by small STUBS that follow the spec (sections H and I). The stubs live only in tests/;
 * the real behaviour of those functions is Developer 1's responsibility.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "json_util.h"
#include "records.h"

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

/* ================= stubs of Developer 1's functions (tests only) ================= */

static int g_begin_ok_calls = 0;
static int g_end_ok_calls = 0;
static int g_error_calls = 0;

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Finds key=value in req->query, URL-decodes the value (+ and %XX) into out. */
Status http_query_get(const HttpRequest *req, const char *key, char *out, size_t outSize)
{
    const char *p = req->query;
    size_t keyLen = strlen(key);

    while (*p != '\0') {
        const char *end = strchr(p, '&');
        if (end == NULL) {
            end = p + strlen(p);
        }
        if ((size_t)(end - p) > keyLen && strncmp(p, key, keyLen) == 0 && p[keyLen] == '=') {
            const char *v = p + keyLen + 1;
            size_t n = 0;
            while (v < end) {
                char c = *v;
                if (c == '+') {
                    c = ' ';
                    v++;
                } else if (c == '%') {
                    int hi, lo;
                    if (end - v < 3) return STATUS_ERR_BAD_REQUEST;
                    hi = hex_value(v[1]);
                    lo = hex_value(v[2]);
                    if (hi < 0 || lo < 0) return STATUS_ERR_BAD_REQUEST;
                    c = (char)(hi * 16 + lo);
                    v += 3;
                } else {
                    v++;
                }
                if (n + 1 >= outSize) return STATUS_ERR_BAD_REQUEST;     /* too long */
                out[n++] = c;
            }
            out[n] = '\0';
            return STATUS_OK;
        }
        p = (*end == '&') ? end + 1 : end;
    }
    return STATUS_ERR_NOT_FOUND;
}

void http_begin_ok(HttpResponse *res, int httpStatus)
{
    g_begin_ok_calls++;
    res->status = httpStatus;
    res->contentType = "application/json; charset=utf-8";
    sb_append(&res->body, "{\"ok\":true,\"data\":");
}

void http_end_ok(HttpResponse *res)
{
    g_end_ok_calls++;
    sb_append_char(&res->body, '}');
}

static int stub_http_code(Status st)
{
    switch (st) {
    case STATUS_OK:               return 200;
    case STATUS_ERR_NOT_FOUND:    return 404;
    case STATUS_ERR_NOMEM:
    case STATUS_ERR_INTERNAL:     return 500;
    default:                      return 400;      /* INVALID_ID, BAD_REQUEST ... */
    }
}

static const char *stub_code_string(Status st)
{
    switch (st) {
    case STATUS_ERR_INVALID_ID:   return "INVALID_ID";
    case STATUS_ERR_NOT_FOUND:    return "NOT_FOUND";
    case STATUS_ERR_BAD_REQUEST:  return "BAD_REQUEST";
    case STATUS_ERR_NOMEM:        return "OUT_OF_MEMORY";
    case STATUS_ERR_INTERNAL:     return "INTERNAL_ERROR";
    default:                      return "OTHER";
    }
}

void http_send_error(HttpResponse *res, Status st, const char *detail)
{
    g_error_calls++;
    sb_clear(&res->body);
    res->status = stub_http_code(st);
    res->contentType = "application/json; charset=utf-8";
    sb_append(&res->body, "{\"ok\":false,\"error\":{\"code\":");
    sb_append_json_string(&res->body, stub_code_string(st));
    sb_append(&res->body, ",\"message\":");
    sb_append_json_string(&res->body, detail != NULL ? detail : "default message");
    sb_append(&res->body, "}}");
}

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

void patient_write_json(StrBuf *sb, const Patient *p)
{
    sb_appendf(sb, "{\"id\":%d,\"name\":", p->id);
    sb_append_json_string(sb, p->name);
    sb_append(sb, ",\"bloodGroup\":");
    sb_append_json_string(sb, p->bloodGroup);
    sb_append(sb, ",\"city\":");
    sb_append_json_string(sb, p->city);
    sb_append_char(sb, '}');
}

void donor_write_json(StrBuf *sb, const Donor *d)
{
    sb_appendf(sb, "{\"id\":%d,\"name\":", d->id);
    sb_append_json_string(sb, d->name);
    sb_append(sb, ",\"bloodGroup\":");
    sb_append_json_string(sb, d->bloodGroup);
    sb_append(sb, ",\"city\":");
    sb_append_json_string(sb, d->city);
    sb_append(sb, ",\"phone\":");
    sb_append_json_string(sb, d->phone);
    sb_appendf(sb, ",\"available\":%s}", d->available ? "true" : "false");
}

/* ================= test fixture ================= */

static void add_patient(RecordStore *s, int id, const char *name, const char *bg, const char *city)
{
    Patient *p = &s->patients.items[s->patients.count++];
    memset(p, 0, sizeof *p);
    p->id = id;
    snprintf(p->name, sizeof p->name, "%s", name);
    snprintf(p->bloodGroup, sizeof p->bloodGroup, "%s", bg);
    snprintf(p->city, sizeof p->city, "%s", city);
}

static void add_donor(RecordStore *s, int id, const char *name, const char *bg, const char *city,
                      const char *phone, int available)
{
    Donor *d = &s->donors.items[s->donors.count++];
    memset(d, 0, sizeof *d);
    d->id = id;
    snprintf(d->name, sizeof d->name, "%s", name);
    snprintf(d->bloodGroup, sizeof d->bloodGroup, "%s", bg);
    snprintf(d->city, sizeof d->city, "%s", city);
    snprintf(d->phone, sizeof d->phone, "%s", phone);
    d->available = available;
}

/* Patients: 1 Ravi A-/Pune, 3 Sunil O-/Nagpur.
   Donors:   101 Asha Rao O- Pune avail, 102 Vikram Das A- Nagpur avail, 107 Kiran Pol O- Nagpur NOT avail. */
static int fixture_setup(AppState *app)
{
    memset(app, 0, sizeof *app);
    app->store.donors.items = calloc(8, sizeof(Donor));
    app->store.patients.items = calloc(4, sizeof(Patient));
    if (app->store.donors.items == NULL || app->store.patients.items == NULL) {
        return 0;
    }
    app->store.donors.capacity = 8;
    app->store.patients.capacity = 4;
    add_patient(&app->store, 1, "Ravi", "A-", "Pune");
    add_patient(&app->store, 3, "Sunil", "O-", "Nagpur");
    add_donor(&app->store, 101, "Asha Rao", "O-", "Pune", "9000000101", 1);
    add_donor(&app->store, 102, "Vikram Das", "A-", "Nagpur", "9000000102", 1);
    add_donor(&app->store, 107, "Kiran Pol", "O-", "Nagpur", "9000000107", 0);
    return 1;
}

static void fixture_teardown(AppState *app)
{
    free(app->store.donors.items);
    free(app->store.patients.items);
    memset(app, 0, sizeof *app);
}

/* Runs the handler for GET /api/search?<query>. The caller must sb_free(&res->body). */
static void run_get(AppState *app, const char *query, HttpResponse *res)
{
    HttpRequest req;

    memset(&req, 0, sizeof req);
    snprintf(req.method, sizeof req.method, "GET");
    snprintf(req.path, sizeof req.path, "/api/search");
    snprintf(req.query, sizeof req.query, "%s", query);
    req.body = "";
    req.bodyLength = 0;
    req.pathId = -1;

    memset(res, 0, sizeof *res);
    sb_init(&res->body);
    g_begin_ok_calls = 0;
    g_end_ok_calls = 0;
    g_error_calls = 0;
    api_get_search(app, &req, res);
}

/* Expect an error response with the given HTTP status and code string. */
static void expect_error(AppState *app, const char *query, int httpStatus, const char *code,
                         const char *description)
{
    HttpResponse res;
    char needle[64];
    int ok;

    run_get(app, query, &res);
    snprintf(needle, sizeof needle, "\"code\":\"%s\"", code);
    ok = res.status == httpStatus && strstr(sb_cstr(&res.body), needle) != NULL &&
         strstr(sb_cstr(&res.body), "\"ok\":false") != NULL &&
         g_error_calls == 1 && g_begin_ok_calls == 0;
    check(ok, description);
    if (!ok) {
        printf("  status=%d body=%s\n", res.status, sb_cstr(&res.body));
    }
    sb_free(&res.body);
}

/* Expect a 200 response; 'mustContain' is a substring that has to be in the body. */
static void expect_ok_contains(AppState *app, const char *query, const char *mustContain,
                               const char *description)
{
    HttpResponse res;
    int ok;

    run_get(app, query, &res);
    ok = res.status == 200 && strstr(sb_cstr(&res.body), mustContain) != NULL &&
         g_begin_ok_calls == 1 && g_end_ok_calls == 1 && g_error_calls == 0 && res.body.failed == 0;
    check(ok, description);
    if (!ok) {
        printf("  status=%d\n  body=%s\n  wanted substring=%s\n", res.status, sb_cstr(&res.body), mustContain);
    }
    sb_free(&res.body);
}

static void test_success_cases(AppState *app)
{
    HttpResponse res;
    const char *expectedDefault =
        "{\"ok\":true,\"data\":{"
        "\"patient\":{\"id\":3,\"name\":\"Sunil\",\"bloodGroup\":\"O-\",\"city\":\"Nagpur\"},"
        "\"filters\":{\"city\":\"\",\"availableOnly\":true},"
        "\"count\":1,"
        "\"donors\":[{\"id\":101,\"name\":\"Asha Rao\",\"bloodGroup\":\"O-\",\"city\":\"Pune\","
        "\"phone\":\"9000000101\",\"available\":true}]}}";
    const char *expectedAll =
        "{\"ok\":true,\"data\":{"
        "\"patient\":{\"id\":3,\"name\":\"Sunil\",\"bloodGroup\":\"O-\",\"city\":\"Nagpur\"},"
        "\"filters\":{\"city\":\"\",\"availableOnly\":false},"
        "\"count\":2,"
        "\"donors\":[{\"id\":101,\"name\":\"Asha Rao\",\"bloodGroup\":\"O-\",\"city\":\"Pune\","
        "\"phone\":\"9000000101\",\"available\":true},"
        "{\"id\":107,\"name\":\"Kiran Pol\",\"bloodGroup\":\"O-\",\"city\":\"Nagpur\","
        "\"phone\":\"9000000107\",\"available\":false}]}}";

    /* Whole body compared character by character: section H layout, key order, and envelope. */
    run_get(app, "patientId=3", &res);
    check(res.status == 200 && strcmp(sb_cstr(&res.body), expectedDefault) == 0,
          "patientId=3 -> exact section H JSON (availableOnly defaults to true)");
    if (strcmp(sb_cstr(&res.body), expectedDefault) != 0) {
        printf("  actual:   %s\n  expected: %s\n", sb_cstr(&res.body), expectedDefault);
    }
    check(strcmp(res.contentType, "application/json; charset=utf-8") == 0, "content type is JSON");
    sb_free(&res.body);

    run_get(app, "patientId=3&availableOnly=false", &res);
    check(res.status == 200 && strcmp(sb_cstr(&res.body), expectedAll) == 0,
          "availableOnly=false -> exact JSON including the unavailable donor with \"available\":false");
    if (strcmp(sb_cstr(&res.body), expectedAll) != 0) {
        printf("  actual:   %s\n  expected: %s\n", sb_cstr(&res.body), expectedAll);
    }
    sb_free(&res.body);

    expect_ok_contains(app, "patientId=3&availableOnly=true", "\"availableOnly\":true},\"count\":1",
                       "availableOnly=true is accepted explicitly");
    expect_ok_contains(app, "availableOnly=false&patientId=1", "\"count\":3",
                       "parameter order does not matter (A- patient, all donors -> 3)");
    expect_ok_contains(app, "patientId=1&city=pune",
                       "\"filters\":{\"city\":\"pune\",\"availableOnly\":true},\"count\":1",
                       "city=pune echoes the filter as typed and finds only the Pune donor");
    expect_ok_contains(app, "patientId=1&city=NAGPUR&availableOnly=false", "\"count\":2",
                       "city is case-insensitive: NAGPUR + all donors -> 102 and 107");
    expect_ok_contains(app, "patientId=1&city=", "\"filters\":{\"city\":\"\"",
                       "empty city= behaves as no filter");
    expect_ok_contains(app, "patientId=1&city=Mumbai", "\"count\":0,\"donors\":[]}}",
                       "zero matches is still 200 with \"donors\":[]");
    expect_ok_contains(app, "patientId=1&city=Pune+Nagar", "\"city\":\"Pune Nagar\"",
                       "URL-decoded city with a space is echoed");
    expect_ok_contains(app, "patientId=1&city=a%22b", "\"city\":\"a\\\"b\"",
                       "a quote inside the city filter is JSON-escaped in the answer");
    expect_ok_contains(app, "patientId=0001", "\"patient\":{\"id\":1,",
                       "patientId with leading zeros 0001 is read as 1");
}

static void test_error_cases(AppState *app)
{
    char longCity[128];

    expect_error(app, "", 400, "INVALID_ID", "missing patientId -> 400 INVALID_ID");
    expect_error(app, "city=Pune", 400, "INVALID_ID", "patientId missing but other params present -> 400 INVALID_ID");
    expect_error(app, "patientId=", 400, "INVALID_ID", "empty patientId -> 400 INVALID_ID");
    expect_error(app, "patientId=abc", 400, "INVALID_ID", "non-numeric patientId -> 400 INVALID_ID");
    expect_error(app, "patientId=5x", 400, "INVALID_ID", "patientId with trailing letters -> 400 INVALID_ID");
    expect_error(app, "patientId=1.5", 400, "INVALID_ID", "decimal patientId -> 400 INVALID_ID");
    expect_error(app, "patientId=0", 400, "INVALID_ID", "patientId 0 -> 400 INVALID_ID");
    expect_error(app, "patientId=-3", 400, "INVALID_ID", "negative patientId -> 400 INVALID_ID");
    expect_error(app, "patientId=%2B1", 400, "INVALID_ID", "patientId with a plus sign -> 400 INVALID_ID");
    expect_error(app, "patientId=2147483648", 400, "INVALID_ID", "patientId above INT_MAX -> 400 INVALID_ID");
    expect_error(app, "patientId=99999999999999999999", 400, "INVALID_ID", "huge patientId (too long) -> 400 INVALID_ID");
    expect_error(app, "patientId=%zz", 400, "INVALID_ID", "malformed percent-encoding in patientId -> 400 INVALID_ID");

    expect_error(app, "patientId=999", 404, "NOT_FOUND", "unknown patient 999 -> 404 NOT_FOUND");
    expect_error(app, "patientId=2147483647", 404, "NOT_FOUND", "patientId INT_MAX (valid but absent) -> 404 NOT_FOUND");

    expect_error(app, "patientId=1&availableOnly=maybe", 400, "BAD_REQUEST", "availableOnly=maybe -> 400 BAD_REQUEST");
    expect_error(app, "patientId=1&availableOnly=TRUE", 400, "BAD_REQUEST", "availableOnly=TRUE (wrong case) -> 400 BAD_REQUEST");
    expect_error(app, "patientId=1&availableOnly=1", 400, "BAD_REQUEST", "availableOnly=1 -> 400 BAD_REQUEST");
    expect_error(app, "patientId=1&availableOnly=", 400, "BAD_REQUEST", "empty availableOnly -> 400 BAD_REQUEST");
    expect_error(app, "patientId=1&availableOnly=truefalse", 400, "BAD_REQUEST", "over-long availableOnly -> 400 BAD_REQUEST");

    memset(longCity, 'x', 40);
    longCity[40] = '\0';
    {
        char query[200];
        snprintf(query, sizeof query, "patientId=1&city=%s", longCity);
        expect_error(app, query, 400, "BAD_REQUEST", "city longer than 29 bytes -> 400 BAD_REQUEST (not truncated)");
    }
    expect_error(app, "patientId=1&city=%zz", 400, "BAD_REQUEST", "malformed percent-encoding in city -> 400 BAD_REQUEST");

    /* Priority: patientId is checked first. */
    expect_error(app, "patientId=abc&availableOnly=maybe", 400, "INVALID_ID", "invalid patientId is reported before availableOnly");
    /* A bad flag is reported before the patient lookup. */
    expect_error(app, "patientId=999&availableOnly=maybe", 400, "BAD_REQUEST", "bad availableOnly beats unknown patient");
}

static void test_null_arguments(AppState *app)
{
    HttpResponse res;
    HttpRequest req;

    memset(&req, 0, sizeof req);
    req.body = "";
    req.pathId = -1;

    memset(&res, 0, sizeof res);
    sb_init(&res.body);
    g_error_calls = 0;
    api_get_search(NULL, &req, &res);
    check(res.status == 500 && g_error_calls == 1, "NULL app -> 500 error response, no crash");
    sb_free(&res.body);

    memset(&res, 0, sizeof res);
    sb_init(&res.body);
    g_error_calls = 0;
    api_get_search(app, NULL, &res);
    check(res.status == 500 && g_error_calls == 1, "NULL request -> 500 error response, no crash");
    sb_free(&res.body);

    api_get_search(app, &req, NULL);
    check(1, "NULL response -> returns without crashing");
}

int main(void)
{
    AppState app;

    if (!fixture_setup(&app)) {
        printf("FAIL: could not allocate the test fixture\n");
        fixture_teardown(&app);
        return 1;
    }

    test_success_cases(&app);
    test_error_cases(&app);
    test_null_arguments(&app);

    fixture_teardown(&app);

    printf("\n%d checks, %d failed\n", g_checks, g_failures);
    printf(g_failures == 0 ? "ALL TESTS PASSED\n" : "SOME TESTS FAILED\n");
    return g_failures == 0 ? 0 : 1;
}