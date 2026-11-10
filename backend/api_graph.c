/*
 * api_graph.c - HTTP handlers for the three graph endpoints:
 *
 *     GET /api/graph          [?city=Pune]   vertices, degrees, edges
 *     GET /api/graph/matrix   [?city=Pune]   adjacency matrix, row/column sums
 *     GET /api/graph/degrees  [?city=Pune]   degrees and the handshake sums
 *
 * All three do the same work (read ?city=, build the graph, write JSON); only
 * the JSON writer differs. So they share one private function.
 *
 * MEMORY: the BipartiteGraph is a LOCAL variable of the handler. It is built,
 * written out and freed before the handler returns, so it can never outlive
 * the RecordStore it describes (the store is not changed during a request).
 */

#include <stdio.h>

#include "api.h"
#include "graph.h"
#include "json_util.h"

/* Which of the three JSON writers a request needs. */
typedef enum {
    GRAPH_VIEW_FULL,      /* /api/graph          */
    GRAPH_VIEW_MATRIX,    /* /api/graph/matrix   */
    GRAPH_VIEW_DEGREES    /* /api/graph/degrees  */
} GraphView;

/*
 * Read the optional ?city= query parameter into 'city' (size CITY_LEN).
 *   - parameter missing  -> city = ""  (no filter), returns STATUS_OK
 *   - parameter present  -> city = its URL-decoded text, returns STATUS_OK
 *   - malformed or longer than CITY_LEN-1 characters -> STATUS_ERR_BAD_REQUEST
 */
static Status read_city_filter(const HttpRequest *req, char *city, size_t citySize)
{
    Status st;

    city[0] = '\0';
    st = http_query_get(req, "city", city, citySize);
    if (st == STATUS_ERR_NOT_FOUND) {
        city[0] = '\0';          /* absent parameter simply means "no filter" */
        return STATUS_OK;
    }
    return st;                   /* OK, or BAD_REQUEST for malformed/too long */
}

/* The work shared by all three handlers. */
static void handle_graph_request(AppState *app, const HttpRequest *req,
                                 HttpResponse *res, GraphView view)
{
    BipartiteGraph g;
    char   city[CITY_LEN];
    char   detail[96];
    Status st;

    if (res == NULL) {
        return;                  /* nothing we can answer on */
    }

    graph_init(&g);              /* a zeroed graph is safe to graph_free() */
    city[0] = '\0';

    if (app == NULL || req == NULL) {
        http_send_error(res, STATUS_ERR_INTERNAL, NULL);
        goto cleanup;
    }

    /* 1. Optional city filter. */
    st = read_city_filter(req, city, sizeof city);
    if (st != STATUS_OK) {
        snprintf(detail, sizeof detail,
                 "Query parameter 'city' is malformed or longer than %d characters",
                 CITY_LEN - 1);
        http_send_error(res, STATUS_ERR_BAD_REQUEST, detail);
        goto cleanup;
    }

    /* 2. Build the graph from the current records (also computes degrees). */
    st = graph_build(&g, &app->store, city);
    if (st != STATUS_OK) {
        http_send_error(res, st, NULL);   /* NULL -> default message for st */
        goto cleanup;
    }

    /* 3. Write {"ok":true,"data": <graph json> } into the response body.
          If the body ran out of memory, http_end_ok() turns it into a 500. */
    http_begin_ok(res, 200);
    switch (view) {
    case GRAPH_VIEW_FULL:
        graph_write_json(&res->body, &g, &app->store);
        break;
    case GRAPH_VIEW_MATRIX:
        graph_write_matrix_json(&res->body, &g);
        break;
    case GRAPH_VIEW_DEGREES:
        graph_write_degrees_json(&res->body, &g, &app->store);
        break;
    }
    http_end_ok(res);

cleanup:
    /* Single exit: the graph is released on success and on every error path. */
    graph_free(&g);
}

void api_get_graph(AppState *app, const HttpRequest *req, HttpResponse *res)
{
    handle_graph_request(app, req, res, GRAPH_VIEW_FULL);
}

void api_get_graph_matrix(AppState *app, const HttpRequest *req, HttpResponse *res)
{
    handle_graph_request(app, req, res, GRAPH_VIEW_MATRIX);
}

void api_get_graph_degrees(AppState *app, const HttpRequest *req, HttpResponse *res)
{
    handle_graph_request(app, req, res, GRAPH_VIEW_DEGREES);
}