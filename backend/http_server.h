#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H
#include "common.h"
#include "json_util.h"
#include "app.h"
typedef struct {
    char        method[8];
    char        path[256];          /* without query string */
    char        query[512];         /* raw text after '?', no '?' */
    const char *body;               /* NUL-terminated; "" if none. Owned by the server */
    size_t      bodyLength;
    int         pathId;             /* set by router for /{id} routes, otherwise -1 */
} HttpRequest;
typedef struct { int status; const char *contentType; StrBuf body; } HttpResponse;
typedef void (*ApiHandler)(AppState *app, const HttpRequest *req, HttpResponse *res);

/* URL-decoded query value. OK; NOT_FOUND if key absent; BAD_REQUEST if malformed or too long. */
Status http_query_get(const HttpRequest *req, const char *key, char *out, size_t outSize);
void   http_begin_ok(HttpResponse *res, int httpStatus);  /* sets status + JSON type, writes {"ok":true,"data": */
void   http_end_ok(HttpResponse *res);                    /* writes } ; if body.failed => replaces with 500 error */
void   http_send_error(HttpResponse *res, Status st, const char *detail);  /* detail NULL => status_message(st) */
Status server_run(AppState *app, int port);   /* blocking loop; returns after server_request_stop() or Ctrl+C */
void   server_request_stop(void);
#endif

