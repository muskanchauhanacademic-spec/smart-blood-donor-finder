/*
 * api_matching.c - HTTP handlers for POST /api/matching and GET /api/stats.
 * Owner: Developer 4.
 *
 * Both handlers follow the same life cycle:
 *     init local objects -> use them -> free them in ONE cleanup block.
 * The graph and the matching are LOCAL to the handler (see the memory
 * ownership rules): they are rebuilt on every request, so they can never be
 * out of date with respect to the RecordStore.
 *
 * The server guarantees that app, req and res are never NULL.
 */

#include <stddef.h>
#include <string.h>

#include "api.h"
#include "graph.h"
#include "matching.h"

/* ------------------------------------------------------------------------ */
/* Helpers (file-private)                                                   */
/* ------------------------------------------------------------------------ */

/* 1 if the request body is missing or contains only white space. */
static int body_is_blank(const HttpRequest *req)
{
    const char *p = req->body;

    if (p == NULL) {
        return 1;
    }
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    return *p == '\0';
}

/* Removes leading and trailing spaces in place (the spec trims spaces only).
   memmove is used because source and destination overlap. */
static void trim_spaces(char *text)
{
    size_t len = strlen(text);
    size_t start = 0;

    while (len > 0 && text[len - 1] == ' ') {
        len--;
    }
    text[len] = '\0';
    while (text[start] == ' ') {
        start++;
    }
    if (start > 0) {
        memmove(text, text + start, len - start + 1);   /* +1 keeps the '\0' */
    }
}

/*
 * Reads the optional "city" from the request body into 'city'.
 *   - blank body            -> OK, city = ""  (no filter)
 *   - body is not flat JSON -> BAD_REQUEST
 *   - "city" absent         -> OK, city = ""
 *   - "city" not a string, or too long for CITY_LEN -> BAD_REQUEST
 * On a non-OK result *detail may hold a message for the client (or NULL to use
 * the default text of the status).
 */
static Status read_city_filter(const HttpRequest *req, char *city, size_t citySize,
                               const char **detail)
{
    JsonObject obj = { NULL, 0 };
    Status st = STATUS_OK;

    city[0] = '\0';
    *detail = NULL;

    if (body_is_blank(req)) {
        return STATUS_OK;
    }

    st = json_parse_object(req->body, &obj);
    if (st != STATUS_OK) {
        if (st == STATUS_ERR_BAD_REQUEST) {
            *detail = "Body must be one flat JSON object, e.g. {\"city\":\"Pune\"}";
        }
        goto cleanup;
    }

    if (json_has_key(&obj, "city")) {
        st = json_get_string(&obj, "city", city, citySize);
        if (st == STATUS_ERR_NOT_FOUND) {
            /* the key exists, so NOT_FOUND here means "not a string" */
            st = STATUS_ERR_BAD_REQUEST;
            *detail = "\"city\" must be a string";
        } else if (st == STATUS_ERR_BAD_REQUEST) {
            *detail = "\"city\" is too long (at most 29 bytes)";
        } else if (st == STATUS_OK) {
            trim_spaces(city);
        }
    }

cleanup:
    json_object_free(&obj);   /* safe on an empty object */
    if (st != STATUS_OK) {
        city[0] = '\0';
    }
    return st;
}

/* Counts the 1s of the matrix through graph_has_edge. We count here instead of
   trusting g->edgeCount so that /api/stats does not depend on which graph
   function fills that field. */
static int count_edges(const BipartiteGraph *g)
{
    int row;
    int col;
    int total = 0;

    for (row = 0; row < g->numPatients; row++) {
        for (col = 0; col < g->numDonors; col++) {
            if (graph_has_edge(g, row, col)) {
                total++;
            }
        }
    }
    return total;
}

/* ------------------------------------------------------------------------ */
/* POST /api/matching                                                       */
/* ------------------------------------------------------------------------ */

void api_post_matching(AppState *app, const HttpRequest *req, HttpResponse *res)
{
    BipartiteGraph graph;
    MatchingResult matching;
    char city[CITY_LEN];
    const char *detail = NULL;
    Status st;

    graph_init(&graph);
    matching_init(&matching);

    st = read_city_filter(req, city, sizeof city, &detail);
    if (st != STATUS_OK) {
        http_send_error(res, st, detail);
        goto cleanup;
    }

    st = graph_build(&graph, &app->store, city);   /* city == "" means no filter */
    if (st != STATUS_OK) {
        http_send_error(res, st, NULL);
        goto cleanup;
    }

    st = matching_compute(&graph, &matching);
    if (st != STATUS_OK) {
        http_send_error(res, st, NULL);
        goto cleanup;
    }

    http_begin_ok(res, 200);
    matching_write_json(&res->body, &matching, &graph, &app->store);
    http_end_ok(res);   /* turns the response into a 500 if an append failed */

cleanup:
    matching_free(&matching);
    graph_free(&graph);
}

/* ------------------------------------------------------------------------ */
/* GET /api/stats                                                           */
/* ------------------------------------------------------------------------ */

void api_get_stats(AppState *app, const HttpRequest *req, HttpResponse *res)
{
    BipartiteGraph graph;
    MatchingResult matching;
    int donorCount;
    int availableCount;
    int patientCount;
    Status st;

    (void)req;   /* /api/stats takes no parameters */

    graph_init(&graph);
    matching_init(&matching);

    /* Stats always describe the whole system, so no city filter here. */
    st = graph_build(&graph, &app->store, "");
    if (st != STATUS_OK) {
        http_send_error(res, st, NULL);
        goto cleanup;
    }

    st = matching_compute(&graph, &matching);
    if (st != STATUS_OK) {
        http_send_error(res, st, NULL);
        goto cleanup;
    }

    donorCount = app->store.donors.count;
    patientCount = app->store.patients.count;
    availableCount = records_available_donor_count(&app->store);

    http_begin_ok(res, 200);
    sb_appendf(&res->body,
               "{\"donorCount\":%d,\"availableDonorCount\":%d,\"unavailableDonorCount\":%d,"
               "\"patientCount\":%d,\"edgeCount\":%d,\"maxMatching\":%d,"
               "\"unmatchedPatientCount\":%d}",
               donorCount, availableCount, donorCount - availableCount,
               patientCount, count_edges(&graph), matching.matchCount,
               patientCount - matching.matchCount);
    http_end_ok(res);

cleanup:
    matching_free(&matching);
    graph_free(&graph);
}