/*
 * router.c - maps "METHOD /path" to the API handler functions.
 *
 * Each route is one row in a table. A path pattern is written like
 *     /api/donors/{id}/availability
 * where {id} stands for "a positive whole number". We compare the request path
 * to every pattern one path segment (the text between '/' characters) at a time.
 *
 * Outcomes:
 *   path + method match, id fine -> call the handler
 *   path + method match, id bad  -> 400 INVALID_ID   (e.g. /api/donors/abc)
 *   path matches, method differs -> 405 METHOD_NOT_ALLOWED
 *   nothing matches              -> 404 NOT_FOUND
 */
#include <limits.h>
#include <string.h>

#include "api.h"
#include "router.h"

typedef struct {
    const char *method;
    const char *pattern;
    ApiHandler  handler;
} Route;

/* static const lookup table (allowed by the coding standards). */
static const Route ROUTES[] = {
    { "GET",  "/api/donors",                      api_get_donors },
    { "POST", "/api/donors",                      api_post_donor },
    { "GET",  "/api/donors/{id}",                 api_get_donor },
    { "PUT",  "/api/donors/{id}",                 api_put_donor },
    { "PUT",  "/api/donors/{id}/availability",    api_put_donor_availability },
    { "GET",  "/api/patients",                    api_get_patients },
    { "POST", "/api/patients",                    api_post_patient },
    { "GET",  "/api/patients/{id}",               api_get_patient },
    { "GET",  "/api/search",                      api_get_search },
    { "GET",  "/api/graph",                       api_get_graph },
    { "GET",  "/api/graph/matrix",                api_get_graph_matrix },
    { "GET",  "/api/graph/degrees",               api_get_graph_degrees },
    { "POST", "/api/matching",                    api_post_matching },
    { "GET",  "/api/stats",                       api_get_stats }
};

#define ROUTE_COUNT (sizeof ROUTES / sizeof ROUTES[0])

typedef enum {
    PATH_NO_MATCH,    /* different shape of path */
    PATH_MATCH,       /* same shape, {id} (if any) is a good number */
    PATH_BAD_ID       /* same shape, but the {id} part is not a valid id */
} PathResult;

/* Length of the segment that starts at 's' (up to the next '/' or the end). */
static size_t segment_length(const char *s)
{
    size_t n = 0;

    while (s[n] != '\0' && s[n] != '/') {
        n++;
    }
    return n;
}

/* Parse a segment that must be digits only, value 1..INT_MAX.
 * Returns 1 and stores the value, or 0 if it is not a valid id. */
static int parse_id_segment(const char *s, size_t length, int *out)
{
    int value = 0;
    size_t i;

    if (length == 0) {
        return 0;
    }
    for (i = 0; i < length; i++) {
        int digit;

        if (s[i] < '0' || s[i] > '9') {
            return 0;
        }
        digit = s[i] - '0';
        /* Test BEFORE multiplying: value * 10 + digit must stay <= INT_MAX.
         * (On Windows a long is only 32 bits, so we cannot "check afterwards".) */
        if (value > (INT_MAX - digit) / 10) {
            return 0;
        }
        value = value * 10 + digit;
    }
    if (value < 1) {
        return 0;                            /* "0", "00" ... are not valid ids */
    }
    *out = (int)value;
    return 1;
}

/* Compare 'path' with 'pattern' segment by segment. */
static PathResult match_path(const char *pattern, const char *path, int *idOut)
{
    const char *p = pattern;
    const char *q = path;
    int badId = 0;

    /* Both strings always start with '/'. Each loop turn consumes "/segment". */
    while (*p == '/' && *q == '/') {
        size_t patternLen;
        size_t pathLen;

        p++;
        q++;
        patternLen = segment_length(p);
        pathLen = segment_length(q);

        if (patternLen == 4 && strncmp(p, "{id}", 4) == 0) {
            if (pathLen == 0) {
                return PATH_NO_MATCH;        /* "/api/donors/" has no id: treat as unknown path */
            }
            if (!parse_id_segment(q, pathLen, idOut)) {
                badId = 1;                   /* remember, but keep checking the rest of the shape */
            }
        } else if (patternLen != pathLen || strncmp(p, q, patternLen) != 0) {
            return PATH_NO_MATCH;
        }
        p += patternLen;
        q += pathLen;
    }

    /* A real match consumes both strings completely. */
    if (*p != '\0' || *q != '\0') {
        return PATH_NO_MATCH;
    }
    return badId ? PATH_BAD_ID : PATH_MATCH;
}

void http_route_request(AppState *app, HttpRequest *req, HttpResponse *res)
{
    size_t i;
    int pathKnown = 0;      /* some route has this path (any method) */
    int badIdSeen = 0;

    req->pathId = -1;       /* routes without {id} leave it at -1 */

    for (i = 0; i < ROUTE_COUNT; i++) {
        int id = -1;
        PathResult result = match_path(ROUTES[i].pattern, req->path, &id);

        if (result == PATH_NO_MATCH) {
            continue;
        }
        pathKnown = 1;
        if (strcmp(ROUTES[i].method, req->method) != 0) {
            continue;                        /* right path, wrong method: maybe another row fits */
        }
        if (result == PATH_BAD_ID) {
            badIdSeen = 1;
            break;
        }
        req->pathId = id;
        ROUTES[i].handler(app, req, res);
        return;
    }

    if (badIdSeen) {
        http_send_error(res, STATUS_ERR_INVALID_ID, NULL);
    } else if (pathKnown) {
        http_send_error(res, STATUS_ERR_METHOD_NOT_ALLOWED, NULL);
    } else {
        http_send_error(res, STATUS_ERR_NOT_FOUND, "Unknown API path");
    }
}