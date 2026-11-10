#ifndef ROUTER_H
#define ROUTER_H

/*
 * router.h - INTERNAL header (not part of the frozen section F set).
 * Only http_server.c includes it, to hand API requests to the router.
 */
#include "http_server.h"

/* Finds the handler for req->method + req->path, sets req->pathId, and calls
 * the handler. If nothing matches, writes a 400 / 404 / 405 error into res.
 * 'req' is non-const because the router stores the parsed {id} in req->pathId. */
void http_route_request(AppState *app, HttpRequest *req, HttpResponse *res);

#endif