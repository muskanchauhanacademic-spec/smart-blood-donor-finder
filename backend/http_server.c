/*
 * http_server.c - a tiny, single-threaded HTTP/1.1 server built on Winsock2.
 *
 * THIS IS THE ONLY FILE ALLOWED TO INCLUDE winsock2.h / windows.h.
 *
 * How it works (read top to bottom, it follows the life of one request):
 *   server_run()      opens a listening socket on 127.0.0.1 and waits.
 *   handle_client()   serves ONE connection: read request -> route -> reply -> close.
 *   receive_request() reads and parses the request line, the headers and the body.
 *   serve_static()    sends a file from ./frontend for non-API GET requests.
 *   http_route_request() (router.c) calls the right api_* handler for /api/... paths.
 *   send_response()   writes the status line, headers and body to the socket.
 *
 * The server handles one client at a time. That is simple and plenty for a
 * demo, but it also means a browser that opens a connection and sends nothing
 * can block the server until the 5-second receive timeout expires.
 *
 * Memory ownership (section E of the spec):
 *   - the request body buffer is malloc'd here and freed after the handler returns;
 *   - the response StrBuf is owned here and freed after the response is sent.
 */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>

#include <ctype.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "http_server.h"
#include "router.h"

#define MAX_HEADER_BYTES       16384u       /* request line + headers */
#define MAX_BODY_BYTES         65536u       /* larger bodies get 413 */
#define CONTENT_LENGTH_CAP     1000000u     /* parse limit so the number cannot overflow */
#define SOCKET_TIMEOUT_MS      5000         /* per recv()/send() call */
#define MAX_STATIC_FILE_BYTES  (8L * 1024L * 1024L)
#define FRONTEND_DIR           "frontend"

/* The ONLY global variable besides lookup tables (allowed by the coding
 * standards). A signal handler may only safely write a sig_atomic_t. */
static volatile sig_atomic_t g_stopRequested = 0;

static const char JSON_TYPE[] = "application/json; charset=utf-8";

/* ================================================================== */
/* Response helpers (declared in http_server.h)                        */
/* ================================================================== */

void http_begin_ok(HttpResponse *res, int httpStatus)
{
    if (res == NULL) {
        return;
    }
    res->status = httpStatus;
    res->contentType = JSON_TYPE;
    sb_clear(&res->body);                    /* start from an empty body */
    sb_append(&res->body, "{\"ok\":true,\"data\":");
}

void http_end_ok(HttpResponse *res)
{
    if (res == NULL) {
        return;
    }
    sb_append_char(&res->body, '}');         /* closes the envelope opened in http_begin_ok */
    if (res->body.failed) {
        /* Some append ran out of memory, so the JSON is incomplete. Throw it away
         * and report a 500 instead (sb_clear inside http_send_error resets 'failed'). */
        http_send_error(res, STATUS_ERR_NOMEM, NULL);
    }
}

void http_send_error(HttpResponse *res, Status st, const char *detail)
{
    if (res == NULL) {
        return;
    }
    res->status = status_http_code(st);
    res->contentType = JSON_TYPE;
    sb_clear(&res->body);                    /* drop any half-written success output */
    sb_append(&res->body, "{\"ok\":false,\"error\":{\"code\":");
    sb_append_json_string(&res->body, status_code_string(st));
    sb_append(&res->body, ",\"message\":");
    sb_append_json_string(&res->body, detail != NULL ? detail : status_message(st));
    sb_append(&res->body, "}}");
    /* If even this tiny message failed to allocate, res->body.failed stays set and
     * send_response() falls back to a hard-coded message. */
}

/* ================================================================== */
/* Query-string decoding                                               */
/* ================================================================== */

/* '0'-'9','a'-'f','A'-'F' -> 0..15, anything else -> -1. */
static int hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/* Decode srcLen bytes of "percent-encoded" text into out.
 *   '+'   becomes a space,   "%41" becomes 'A'.
 * BAD_REQUEST if an escape is incomplete/not hex, decodes to a NUL byte, or the
 * result (plus its '\0') does not fit in outSize. */
static Status url_decode(const char *src, size_t srcLen, char *out, size_t outSize)
{
    size_t i = 0;
    size_t o = 0;

    if (out == NULL || outSize == 0) {
        return STATUS_ERR_BAD_REQUEST;
    }
    out[0] = '\0';

    while (i < srcLen) {
        char decoded;

        if (src[i] == '%') {
            int high;
            int low;

            if (i + 2 >= srcLen) {           /* need two characters after the '%' */
                return STATUS_ERR_BAD_REQUEST;
            }
            high = hex_value(src[i + 1]);
            low = hex_value(src[i + 2]);
            if (high < 0 || low < 0) {
                return STATUS_ERR_BAD_REQUEST;
            }
            decoded = (char)(unsigned char)(high * 16 + low);
            if (decoded == '\0') {
                return STATUS_ERR_BAD_REQUEST;   /* %00 would cut the string short */
            }
            i += 3;
        } else if (src[i] == '+') {
            decoded = ' ';
            i++;
        } else {
            decoded = src[i];
            i++;
        }

        if (o + 1 >= outSize) {              /* keep one byte free for the '\0' */
            out[0] = '\0';
            return STATUS_ERR_BAD_REQUEST;
        }
        out[o++] = decoded;
    }
    out[o] = '\0';
    return STATUS_OK;
}

Status http_query_get(const HttpRequest *req, const char *key, char *out, size_t outSize)
{
    const char *cursor;

    if (req == NULL || key == NULL || out == NULL || outSize == 0) {
        return STATUS_ERR_BAD_REQUEST;
    }
    out[0] = '\0';

    /* The query looks like  a=1&b=two%20words&c   - split it at each '&'. */
    cursor = req->query;
    while (*cursor != '\0') {
        const char *ampersand = strchr(cursor, '&');
        size_t pairLength = (ampersand != NULL) ? (size_t)(ampersand - cursor) : strlen(cursor);
        const char *equals = memchr(cursor, '=', pairLength);
        size_t keyLength = (equals != NULL) ? (size_t)(equals - cursor) : pairLength;
        char decodedKey[64];

        /* A key we cannot decode (or that is too long) simply is not our key. */
        if (url_decode(cursor, keyLength, decodedKey, sizeof decodedKey) == STATUS_OK &&
            strcmp(decodedKey, key) == 0) {
            const char *value = (equals != NULL) ? equals + 1 : cursor + pairLength;
            size_t valueLength = (equals != NULL) ? pairLength - keyLength - 1 : 0;

            return url_decode(value, valueLength, out, outSize);
        }
        cursor += pairLength;
        if (*cursor == '&') {
            cursor++;
        }
    }
    return STATUS_ERR_NOT_FOUND;
}

/* ================================================================== */
/* Reading and parsing one request                                     */
/* ================================================================== */

/* Case-insensitive test: does line start with "name:" ?  'name' must be lower-case. */
static int header_name_is(const char *line, size_t length, const char *name)
{
    size_t n = strlen(name);
    size_t i;

    if (length <= n || line[n] != ':') {
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (tolower((unsigned char)line[i]) != (unsigned char)name[i]) {
            return 0;
        }
    }
    return 1;
}

/* Parse the text after "Content-Length:". Digits only (spaces around are fine).
 * Huge numbers are clamped to CONTENT_LENGTH_CAP, which is still "too large". */
static int parse_content_length(const char *value, size_t length, size_t *out)
{
    size_t start = 0;
    size_t end = length;
    size_t result = 0;
    size_t i;

    while (start < end && (value[start] == ' ' || value[start] == '\t')) {
        start++;
    }
    while (end > start && (value[end - 1] == ' ' || value[end - 1] == '\t')) {
        end--;
    }
    if (start == end) {
        return 0;
    }
    for (i = start; i < end; i++) {
        if (value[i] < '0' || value[i] > '9') {
            return 0;
        }
        result = result * 10 + (size_t)(value[i] - '0');
        if (result > CONTENT_LENGTH_CAP) {
            result = CONTENT_LENGTH_CAP;     /* clamp so the next multiply cannot overflow */
        }
    }
    *out = result;
    return 1;
}

/* Position of the "\r\n\r\n" that ends the headers, searched in buf[0..total).
 * Returns 1 and stores the index of the first '\r', or 0 if not there (yet). */
static int find_header_end(const char *buf, size_t total, size_t *endIndex)
{
    size_t i;

    if (total < 4) {
        return 0;
    }
    for (i = 0; i + 3 < total; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') {
            *endIndex = i;
            return 1;
        }
    }
    return 0;
}

/* Parse "GET /api/search?patientId=5 HTTP/1.1" into req->method/path/query.
 * Over-long parts are REJECTED (never truncated). */
static Status parse_request_line(const char *line, size_t length, HttpRequest *req,
                                 const char **detail)
{
    const char *space1 = memchr(line, ' ', length);
    const char *space2;
    const char *target;
    const char *version;
    const char *questionMark;
    size_t methodLength;
    size_t targetLength;
    size_t versionLength;
    size_t pathLength;
    size_t queryLength;
    size_t i;

    *detail = "Malformed request line";
    if (space1 == NULL) {
        return STATUS_ERR_BAD_REQUEST;
    }
    space2 = memchr(space1 + 1, ' ', length - (size_t)(space1 + 1 - line));
    if (space2 == NULL) {
        return STATUS_ERR_BAD_REQUEST;
    }

    methodLength = (size_t)(space1 - line);
    if (methodLength == 0 || methodLength >= sizeof req->method) {
        *detail = "Unsupported HTTP method";
        return STATUS_ERR_BAD_REQUEST;
    }
    for (i = 0; i < methodLength; i++) {
        if (line[i] < 'A' || line[i] > 'Z') {
            *detail = "Unsupported HTTP method";
            return STATUS_ERR_BAD_REQUEST;
        }
    }

    target = space1 + 1;
    targetLength = (size_t)(space2 - target);
    version = space2 + 1;
    versionLength = length - (size_t)(version - line);

    if (versionLength < 8 || strncmp(version, "HTTP/1.", 7) != 0) {
        *detail = "Only HTTP/1.x is supported";
        return STATUS_ERR_BAD_REQUEST;
    }
    if (targetLength == 0 || target[0] != '/') {
        return STATUS_ERR_BAD_REQUEST;
    }
    /* Only visible ASCII is allowed in the target. This also keeps our log lines clean. */
    for (i = 0; i < targetLength; i++) {
        unsigned char c = (unsigned char)target[i];
        if (c < 0x21 || c > 0x7E) {
            return STATUS_ERR_BAD_REQUEST;
        }
    }

    questionMark = memchr(target, '?', targetLength);
    pathLength = (questionMark != NULL) ? (size_t)(questionMark - target) : targetLength;
    queryLength = (questionMark != NULL) ? targetLength - pathLength - 1 : 0;
    if (pathLength >= sizeof req->path) {
        *detail = "Request path is too long";
        return STATUS_ERR_BAD_REQUEST;
    }
    if (queryLength >= sizeof req->query) {
        *detail = "Query string is too long";
        return STATUS_ERR_BAD_REQUEST;
    }

    memcpy(req->method, line, methodLength);
    req->method[methodLength] = '\0';
    memcpy(req->path, target, pathLength);
    req->path[pathLength] = '\0';
    if (queryLength > 0) {
        memcpy(req->query, questionMark + 1, queryLength);
    }
    req->query[queryLength] = '\0';
    *detail = NULL;
    return STATUS_OK;
}

/* Read one complete request from the socket.
 *   On success: *bodyOut is a malloc'd, NUL-terminated copy of the body
 *               (the CALLER frees it) and req is filled in.
 *   On failure: returns a Status, *detail explains it, *bodyOut is NULL.
 *   *gotAnyData tells the caller whether the peer sent at least one byte
 *               (idle "probe" connections get no reply at all). */
static Status receive_request(SOCKET sock, HttpRequest *req, char **bodyOut,
                              int *gotAnyData, const char **detail)
{
    char head[MAX_HEADER_BYTES];             /* 16 KB buffer for the request line + headers */
    size_t total = 0;                        /* bytes currently in head */
    size_t headerEnd = 0;                    /* index of the first byte of "\r\n\r\n" */
    size_t contentLength = 0;
    int haveContentLength = 0;
    const char *lineEnd;
    const char *cursor;
    size_t alreadyHave;
    size_t filled;
    char *body;
    Status st;

    *bodyOut = NULL;
    *gotAnyData = 0;
    *detail = NULL;

    /* ---- 1. read until the blank line that ends the headers ---- */
    for (;;) {
        int got;

        if (total >= MAX_HEADER_BYTES) {
            *detail = "Request headers are too large";
            return STATUS_ERR_BAD_REQUEST;
        }
        got = recv(sock, head + total, (int)(MAX_HEADER_BYTES - total), 0);
        if (got <= 0) {                      /* 0 = peer closed, <0 = error or 5 s timeout */
            *detail = (got == 0) ? "Connection closed before the request was complete"
                                 : "Timed out waiting for the request";
            return STATUS_ERR_BAD_REQUEST;
        }
        *gotAnyData = 1;
        total += (size_t)got;
        if (find_header_end(head, total, &headerEnd)) {
            break;
        }
    }
    head[headerEnd] = '\0';                  /* head is now a normal C string of the header block */

    /* ---- 2. request line ---- */
    lineEnd = strstr(head, "\r\n");
    st = parse_request_line(head, (lineEnd != NULL) ? (size_t)(lineEnd - head) : strlen(head),
                            req, detail);
    if (st != STATUS_OK) {
        return st;
    }

    /* ---- 3. header lines: we only care about Content-Length and Transfer-Encoding ---- */
    cursor = (lineEnd != NULL) ? lineEnd + 2 : head + strlen(head);
    while (*cursor != '\0') {
        const char *eol = strstr(cursor, "\r\n");
        size_t lineLength = (eol != NULL) ? (size_t)(eol - cursor) : strlen(cursor);

        if (header_name_is(cursor, lineLength, "transfer-encoding")) {
            *detail = "Chunked transfer encoding is not supported; send Content-Length";
            return STATUS_ERR_BAD_REQUEST;
        }
        if (header_name_is(cursor, lineLength, "content-length")) {
            size_t nameLength = strlen("content-length:");

            if (haveContentLength ||
                !parse_content_length(cursor + nameLength, lineLength - nameLength, &contentLength)) {
                *detail = "Invalid Content-Length header";
                return STATUS_ERR_BAD_REQUEST;
            }
            haveContentLength = 1;
        }
        cursor += lineLength;
        if (eol != NULL) {
            cursor += 2;                     /* skip the "\r\n" */
        }
    }

    /* ---- 4. body ---- */
    if (contentLength > MAX_BODY_BYTES) {
        *detail = "Request body is larger than 65536 bytes";
        return STATUS_ERR_PAYLOAD_TOO_LARGE;
    }
    /* +1 for the terminating '\0'. This also guarantees we never call malloc(0). */
    body = malloc(contentLength + 1);
    if (body == NULL) {
        return STATUS_ERR_NOMEM;
    }

    /* recv() may already have delivered the start of the body together with the headers. */
    alreadyHave = total - (headerEnd + 4);
    if (alreadyHave > contentLength) {
        alreadyHave = contentLength;         /* ignore anything beyond the declared length */
    }
    memcpy(body, head + headerEnd + 4, alreadyHave);
    filled = alreadyHave;

    while (filled < contentLength) {         /* keep reading until the whole body has arrived */
        int got = recv(sock, body + filled, (int)(contentLength - filled), 0);

        if (got <= 0) {
            free(body);
            *detail = "Request body was incomplete";
            return STATUS_ERR_BAD_REQUEST;
        }
        filled += (size_t)got;
    }
    body[contentLength] = '\0';

    *bodyOut = body;                         /* ownership passes to the caller */
    req->body = body;
    req->bodyLength = contentLength;
    return STATUS_OK;
}

/* ================================================================== */
/* Static files                                                        */
/* ================================================================== */

typedef struct {
    const char *extension;
    const char *contentType;
} MimeType;

/* static const lookup table (allowed by the coding standards). */
static const MimeType MIME_TYPES[] = {
    { "html", "text/html; charset=utf-8" },
    { "css",  "text/css; charset=utf-8" },
    { "js",   "text/javascript; charset=utf-8" },
    { "svg",  "image/svg+xml" },
    { "png",  "image/png" },
    { "ico",  "image/x-icon" },
    { "json", "application/json; charset=utf-8" }
};

#define MIME_TYPE_COUNT (sizeof MIME_TYPES / sizeof MIME_TYPES[0])

/* Case-insensitive string equality using our own tolower loop. */
static int equals_ignore_case(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

/* Content type for the file extension of 'path', or NULL if not allowed. */
static const char *content_type_for(const char *path)
{
    const char *lastDot = strrchr(path, '.');
    const char *lastSlash = strrchr(path, '/');
    size_t i;

    if (lastDot == NULL || lastSlash == NULL || lastDot < lastSlash) {
        return NULL;                         /* no extension in the final path segment */
    }
    for (i = 0; i < MIME_TYPE_COUNT; i++) {
        if (equals_ignore_case(lastDot + 1, MIME_TYPES[i].extension)) {
            return MIME_TYPES[i].contentType;
        }
    }
    return NULL;
}

/* Path-traversal protection: allow ONLY letters, digits and . _ - / and never "..".
 * A backslash or a colon (e.g. "C:") is therefore impossible. */
static int static_path_is_safe(const char *path)
{
    const char *p;

    if (path[0] != '/' || strstr(path, "..") != NULL) {
        return 0;
    }
    for (p = path; *p != '\0'; p++) {
        char c = *p;
        int allowed = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                      (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-' || c == '/';
        if (!allowed) {
            return 0;
        }
    }
    return 1;
}

/* Answer a GET request with a file from ./frontend. */
static void serve_static(const HttpRequest *req, HttpResponse *res)
{
    const char *urlPath = (strcmp(req->path, "/") == 0) ? "/index.html" : req->path;
    const char *contentType;
    char filePath[sizeof FRONTEND_DIR + sizeof req->path + 16];
    char chunk[4096];
    FILE *file;
    long size;
    size_t got;
    int written;

    if (!static_path_is_safe(urlPath)) {
        http_send_error(res, STATUS_ERR_NOT_FOUND, "File not found");
        return;
    }
    contentType = content_type_for(urlPath);
    if (contentType == NULL) {
        http_send_error(res, STATUS_ERR_NOT_FOUND, "File not found");
        return;
    }
    written = snprintf(filePath, sizeof filePath, "%s%s", FRONTEND_DIR, urlPath);
    if (written < 0 || (size_t)written >= sizeof filePath) {
        http_send_error(res, STATUS_ERR_NOT_FOUND, "File not found");
        return;
    }

    file = fopen(filePath, "rb");            /* "rb" = binary, so Windows does not alter bytes */
    if (file == NULL) {
        http_send_error(res, STATUS_ERR_NOT_FOUND, "File not found");
        return;
    }
    /* Measure the file first, to refuse absurdly large ones. (This also fails for
     * things that are not regular files, such as Windows device names.) */
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        http_send_error(res, STATUS_ERR_NOT_FOUND, "File not found");
        return;
    }
    if (size > MAX_STATIC_FILE_BYTES) {
        fclose(file);
        http_send_error(res, STATUS_ERR_INTERNAL, "File is too large to serve");
        return;
    }

    res->status = 200;
    res->contentType = contentType;
    sb_clear(&res->body);
    while ((got = fread(chunk, 1, sizeof chunk, file)) > 0) {
        sb_append_n(&res->body, chunk, got); /* binary-safe append (PNG files contain NULs) */
    }
    if (ferror(file)) {
        fclose(file);
        http_send_error(res, STATUS_ERR_INTERNAL, "Could not read the file");
        return;
    }
    fclose(file);
    if (res->body.failed) {
        http_send_error(res, STATUS_ERR_NOMEM, NULL);
    }
}

/* ================================================================== */
/* Sending the response                                                */
/* ================================================================== */

static const char *reason_phrase(int status)
{
    switch (status) {
    case 200: return "OK";
    case 201: return "Created";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 409: return "Conflict";
    case 413: return "Payload Too Large";
    case 500: return "Internal Server Error";
    default:  return "Unknown";
    }
}

/* send() may send fewer bytes than asked, so loop until everything is out.
 * Returns 1 if all bytes were sent. */
static int send_all(SOCKET sock, const char *data, size_t length)
{
    size_t sent = 0;

    while (sent < length) {
        size_t remaining = length - sent;
        int chunk = (remaining > 65536u) ? 65536 : (int)remaining;
        int n = send(sock, data + sent, chunk, 0);

        if (n <= 0) {
            return 0;                        /* error, timeout or peer closed */
        }
        sent += (size_t)n;
    }
    return 1;
}

static void send_response(SOCKET sock, const HttpResponse *res)
{
    /* Used when the response StrBuf itself ran out of memory. */
    static const char fallbackBody[] =
        "{\"ok\":false,\"error\":{\"code\":\"OUT_OF_MEMORY\",\"message\":\"The server ran out of memory\"}}";
    const char *body;
    const char *contentType;
    size_t bodyLength;
    int status;
    char header[512];
    int headerLength;

    if (res->body.failed) {
        body = fallbackBody;
        bodyLength = sizeof fallbackBody - 1;
        status = 500;
        contentType = JSON_TYPE;
    } else {
        /* Use .data/.length (not sb_cstr) so binary files with NUL bytes work. */
        body = (res->body.data != NULL) ? res->body.data : "";
        bodyLength = res->body.length;
        status = res->status;
        contentType = (res->contentType != NULL) ? res->contentType : JSON_TYPE;
    }

    headerLength = snprintf(header, sizeof header,
                            "HTTP/1.1 %d %s\r\n"
                            "Content-Type: %s\r\n"
                            "Content-Length: %lu\r\n"
                            "Cache-Control: no-store\r\n"
                            "Connection: close\r\n"
                            "\r\n",
                            status, reason_phrase(status), contentType, (unsigned long)bodyLength);
    if (headerLength < 0 || (size_t)headerLength >= sizeof header) {
        return;                              /* cannot happen with our fixed strings */
    }
    if (!send_all(sock, header, (size_t)headerLength)) {
        return;
    }
    if (bodyLength > 0) {
        send_all(sock, body, bodyLength);
    }
}

/* Close a client socket politely. We signal "no more data from us", then read
 * (and discard) whatever the client may still be sending. Without this, closing
 * a socket that still has unread data (for example after a 413) makes Windows
 * send a reset, and the client may never see our response. */
static void close_client(SOCKET sock)
{
    char sink[1024];
    int drained = 0;
    int got;

    shutdown(sock, SD_SEND);
    while (drained < 65536) {
        got = recv(sock, sink, (int)sizeof sink, 0);
        if (got <= 0) {
            break;
        }
        drained += got;
    }
    closesocket(sock);
}

/* ================================================================== */
/* One connection                                                      */
/* ================================================================== */

/* Decide where a parsed request goes: API router, static file, or 405. */
static void dispatch_request(AppState *app, HttpRequest *req, HttpResponse *res)
{
    int isApiPath = (strncmp(req->path, "/api/", 5) == 0) || (strcmp(req->path, "/api") == 0);

    if (isApiPath) {
        http_route_request(app, req, res);
    } else if (strcmp(req->method, "GET") == 0) {
        serve_static(req, res);
    } else {
        http_send_error(res, STATUS_ERR_METHOD_NOT_ALLOWED, NULL);
    }
}

static void handle_client(AppState *app, SOCKET client)
{
    HttpRequest req;
    HttpResponse res;
    char *body = NULL;                       /* malloc'd by receive_request, freed below */
    const char *detail = NULL;
    int gotAnyData = 0;
    DWORD timeoutMs = SOCKET_TIMEOUT_MS;     /* Winsock wants milliseconds in a DWORD */
    Status st;

    memset(&req, 0, sizeof req);
    req.body = "";
    req.pathId = -1;
    res.status = 500;
    res.contentType = JSON_TYPE;
    sb_init(&res.body);

    /* Never wait forever on a silent client. */
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeoutMs, (int)sizeof timeoutMs);
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeoutMs, (int)sizeof timeoutMs);

    st = receive_request(client, &req, &body, &gotAnyData, &detail);
    if (st == STATUS_OK) {
        dispatch_request(app, &req, &res);
        printf("%s %s -> %d\n", req.method, req.path, res.status);
        send_response(client, &res);
    } else if (gotAnyData) {
        http_send_error(&res, st, detail);
        printf("(unreadable request) -> %d\n", res.status);
        send_response(client, &res);
    }
    /* else: the client connected and sent nothing; just close quietly. */

    free(body);                              /* free(NULL) is fine if we never got a body */
    sb_free(&res.body);
    close_client(client);
}

/* ================================================================== */
/* The server loop                                                     */
/* ================================================================== */

/* Called by the C runtime when the user presses Ctrl+C. It may only set the
 * flag; the main loop notices it within one second. */
static void on_interrupt(int signalNumber)
{
    (void)signalNumber;
    g_stopRequested = 1;
}

void server_request_stop(void)
{
    g_stopRequested = 1;
}

Status server_run(AppState *app, int port)
{
    Status result = STATUS_OK;
    WSADATA wsaData;
    int wsaStarted = 0;
    SOCKET listener = INVALID_SOCKET;
    struct sockaddr_in address;
    void (*previousHandler)(int);

    if (app == NULL || port < 1 || port > 65535) {
        return STATUS_ERR_BAD_REQUEST;
    }

    g_stopRequested = 0;
    previousHandler = signal(SIGINT, on_interrupt);

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "WSAStartup failed.\n");
        result = STATUS_ERR_INTERNAL;
        goto cleanup;
    }
    wsaStarted = 1;

    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        fprintf(stderr, "Could not create socket (error %d).\n", WSAGetLastError());
        result = STATUS_ERR_INTERNAL;
        goto cleanup;
    }

    /* Bind to 127.0.0.1 ONLY, so other computers on the network cannot connect.
     * htonl/htons convert numbers to "network byte order". */
    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons((u_short)port);

    if (bind(listener, (struct sockaddr *)&address, (int)sizeof address) == SOCKET_ERROR) {
        fprintf(stderr, "Could not bind to port %d (error %d). Is another copy already running?\n",
                port, WSAGetLastError());
        result = STATUS_ERR_INTERNAL;
        goto cleanup;
    }
    if (listen(listener, 5) == SOCKET_ERROR) {
        fprintf(stderr, "listen failed (error %d).\n", WSAGetLastError());
        result = STATUS_ERR_INTERNAL;
        goto cleanup;
    }

    while (!g_stopRequested) {
        fd_set readSet;
        struct timeval timeout;
        int ready;

        /* Wait up to 1 second for a connection, then loop round and re-check the
         * stop flag. (A plain accept() would block forever and ignore Ctrl+C.) */
        FD_ZERO(&readSet);
        FD_SET(listener, &readSet);
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        ready = select(0, &readSet, NULL, NULL, &timeout);   /* first argument is ignored on Windows */
        if (ready == SOCKET_ERROR) {
            if (g_stopRequested || WSAGetLastError() == WSAEINTR) {
                continue;
            }
            fprintf(stderr, "select failed (error %d).\n", WSAGetLastError());
            result = STATUS_ERR_INTERNAL;
            break;
        }
        if (ready == 0) {
            continue;                        /* timeout: nobody connected */
        }

        {
            SOCKET client = accept(listener, NULL, NULL);

            if (client != INVALID_SOCKET) {
                handle_client(app, client);  /* closes the client socket itself */
            }
        }
    }

cleanup:
    if (listener != INVALID_SOCKET) {
        closesocket(listener);
    }
    if (wsaStarted) {
        WSACleanup();
    }
    if (previousHandler != SIG_ERR) {
        signal(SIGINT, previousHandler);     /* put the old Ctrl+C behaviour back */
    }
    return result;
}