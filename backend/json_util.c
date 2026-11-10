/* json_util.c - growable string buffer (StrBuf) and a tiny parser for ONE flat JSON object.
 *
 * Owner: Developer 2.
 *
 * Notes for the reader:
 *   - StrBuf never loses data on a failed allocation. It sets 'failed' and ignores every later
 *     append, so callers can append freely and check 'failed' once at the end.
 *   - The parser is a hand-written scanner. One pointer 'p' walks forward through the text and
 *     never moves backwards.
 *   - Duplicate keys: ALL pairs are stored and lookups search from the END, so the LAST
 *     duplicate wins. Example: {"a":1,"a":2} -> json_get_int("a") gives 2 (count is 2).
 *   - Strictness choices (documented limitations): raw control characters (< 0x20) inside a
 *     string are rejected, \uXXXX escapes are rejected, and bare values must be true, false,
 *     null or a number that follows the JSON number grammar.
 */
#include "json_util.h"

#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SB_MIN_CAPACITY 64   /* first allocation of a StrBuf */
#define JSON_MIN_PAIRS  4    /* first allocation of the pairs array */

/* ====================================================================== */
/*  StrBuf                                                                */
/* ====================================================================== */

/* Make room for 'extra' more bytes PLUS the terminating NUL.
   Returns 1 on success. Returns 0 and sets sb->failed on failure (old data stays valid). */
static int sb_reserve(StrBuf *sb, size_t extra)
{
    size_t needed;
    size_t newCap;
    char  *tmp;

    /* needed = length + extra + 1, checked first so the sum can never wrap around.
       (length < capacity <= SIZE_MAX always holds, so the subtraction is safe.) */
    if (extra > SIZE_MAX - sb->length - 1) {
        sb->failed = 1;
        return 0;
    }
    needed = sb->length + extra + 1;

    if (needed <= sb->capacity) {
        return 1;                           /* enough room already */
    }

    /* Grow by doubling so that n appends cost O(n) copying overall. */
    newCap = (sb->capacity == 0) ? SB_MIN_CAPACITY : sb->capacity;
    while (newCap < needed) {
        if (newCap > SIZE_MAX / 2) {        /* doubling would overflow: take exactly what we need */
            newCap = needed;
            break;
        }
        newCap *= 2;
    }

    /* Use a temporary pointer: if realloc fails it returns NULL but the OLD block is still
       allocated, and assigning NULL straight to sb->data would leak it. */
    tmp = realloc(sb->data, newCap);
    if (tmp == NULL) {
        sb->failed = 1;
        return 0;
    }
    sb->data = tmp;
    sb->capacity = newCap;
    return 1;
}

void sb_init(StrBuf *sb)
{
    if (sb == NULL) {
        return;
    }
    sb->data = NULL;
    sb->length = 0;
    sb->capacity = 0;
    sb->failed = 0;
}

void sb_clear(StrBuf *sb)
{
    if (sb == NULL) {
        return;
    }
    sb->length = 0;
    sb->failed = 0;                 /* a cleared buffer starts over, memory is kept for reuse */
    if (sb->data != NULL) {
        sb->data[0] = '\0';
    }
}

void sb_free(StrBuf *sb)
{
    if (sb == NULL) {
        return;
    }
    free(sb->data);                 /* free(NULL) is harmless, so zeroed buffers are fine */
    sb->data = NULL;
    sb->length = 0;
    sb->capacity = 0;
    sb->failed = 0;
}

/* Do not pass a pointer that points INSIDE sb->data: the realloc may move the block and
   leave 'data' dangling. Handlers only append literals and other buffers, so this is fine. */
void sb_append_n(StrBuf *sb, const char *data, size_t n)
{
    if (sb == NULL || sb->failed || data == NULL || n == 0) {
        return;
    }
    if (!sb_reserve(sb, n)) {
        return;
    }
    memcpy(sb->data + sb->length, data, n);
    sb->length += n;
    sb->data[sb->length] = '\0';    /* always keep the text NUL-terminated */
}

void sb_append(StrBuf *sb, const char *text)
{
    if (text == NULL) {
        return;
    }
    sb_append_n(sb, text, strlen(text));
}

void sb_append_char(StrBuf *sb, char c)
{
    sb_append_n(sb, &c, 1);
}

void sb_appendf(StrBuf *sb, const char *fmt, ...)
{
    va_list args;
    va_list argsCopy;
    int needed;

    if (sb == NULL || sb->failed || fmt == NULL) {
        return;
    }

    /* A va_list can only be walked once, so keep a copy for the second vsnprintf call. */
    va_start(args, fmt);
    va_copy(argsCopy, args);

    /* First call with no buffer only measures the output length. */
    needed = vsnprintf(NULL, 0, fmt, args);
    va_end(args);

    if (needed < 0) {                       /* encoding error inside vsnprintf */
        va_end(argsCopy);
        sb->failed = 1;
        return;
    }
    if (!sb_reserve(sb, (size_t)needed)) {
        va_end(argsCopy);
        return;
    }

    /* Second call writes into the reserved space (needed chars + the NUL). */
    vsnprintf(sb->data + sb->length, (size_t)needed + 1, fmt, argsCopy);
    va_end(argsCopy);
    sb->length += (size_t)needed;
}

void sb_append_json_string(StrBuf *sb, const char *text)
{
    const unsigned char *p;     /* unsigned so bytes >= 0x80 are not negative */

    if (sb == NULL) {
        return;
    }
    sb_append_char(sb, '"');
    if (text != NULL) {
        for (p = (const unsigned char *)text; *p != '\0'; p++) {
            switch (*p) {
            case '"':  sb_append(sb, "\\\""); break;
            case '\\': sb_append(sb, "\\\\"); break;
            case '\n': sb_append(sb, "\\n");  break;
            case '\r': sb_append(sb, "\\r");  break;
            case '\t': sb_append(sb, "\\t");  break;
            case '\b': sb_append(sb, "\\b");  break;
            case '\f': sb_append(sb, "\\f");  break;
            default:
                if (*p < 0x20) {
                    /* Any other control character becomes \u00XX. */
                    sb_appendf(sb, "\\u%04x", (unsigned int)*p);
                } else {
                    /* Printable ASCII and UTF-8 bytes (>= 0x80) pass through unchanged. */
                    sb_append_char(sb, (char)*p);
                }
                break;
            }
        }
    }
    sb_append_char(sb, '"');
}

const char *sb_cstr(const StrBuf *sb)
{
    if (sb == NULL || sb->data == NULL) {
        return "";
    }
    return sb->data;
}

/* ====================================================================== */
/*  Parser                                                                */
/* ====================================================================== */

/* Skip JSON whitespace and return the first non-space position. */
static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
        p++;
    }
    return p;
}

/* Parse a JSON string. *pp must point at the opening quote.
   On success: *out = malloc'd unescaped copy, *pp = first char after the closing quote.
   On failure: nothing is allocated and *pp is unchanged. */
static Status parse_string(const char **pp, char **out)
{
    const char *p = *pp + 1;        /* skip the opening quote */
    const char *scan = p;
    size_t rawLen;
    size_t n = 0;
    char *buf;

    /* Pass 1: find the closing quote and reject characters JSON does not allow. */
    while (*scan != '"') {
        unsigned char c = (unsigned char)*scan;
        if (c == '\0') {
            return STATUS_ERR_BAD_REQUEST;      /* string never closed */
        }
        if (c < 0x20) {
            return STATUS_ERR_BAD_REQUEST;      /* raw control character */
        }
        if (c == '\\') {
            scan++;                             /* the next char is escaped, step over it */
            if (*scan == '\0') {
                return STATUS_ERR_BAD_REQUEST;
            }
        }
        scan++;
    }

    /* Unescaped text is never longer than the raw text, so rawLen + 1 bytes always suffice.
       rawLen + 1 >= 1, so this is never malloc(0). */
    rawLen = (size_t)(scan - p);
    buf = malloc(rawLen + 1);
    if (buf == NULL) {
        return STATUS_ERR_NOMEM;
    }

    /* Pass 2: copy, translating escape sequences. */
    while (p < scan) {
        if (*p != '\\') {
            buf[n++] = *p++;
            continue;
        }
        p++;                                    /* now on the escape letter */
        switch (*p) {
        case '"':  buf[n++] = '"';  break;
        case '\\': buf[n++] = '\\'; break;
        case '/':  buf[n++] = '/';  break;
        case 'b':  buf[n++] = '\b'; break;
        case 'f':  buf[n++] = '\f'; break;
        case 'n':  buf[n++] = '\n'; break;
        case 'r':  buf[n++] = '\r'; break;
        case 't':  buf[n++] = '\t'; break;
        default:                                /* includes \uXXXX: documented limitation */
            free(buf);
            return STATUS_ERR_BAD_REQUEST;
        }
        p++;
    }
    buf[n] = '\0';

    *out = buf;
    *pp = scan + 1;
    return STATUS_OK;
}

/* Does the token (not NUL-terminated, 'len' chars) equal the literal word? */
static int token_equals(const char *token, size_t len, const char *word)
{
    return strlen(word) == len && strncmp(token, word, len) == 0;
}

static int is_digit_char(char c)
{
    return c >= '0' && c <= '9';
}

/* JSON number grammar:  -? (0 | [1-9][0-9]*) (. [0-9]+)? ([eE] [+-]? [0-9]+)?  */
static int is_json_number(const char *s, size_t len)
{
    size_t i = 0;
    size_t digitsStart;

    if (i < len && s[i] == '-') {
        i++;
    }
    if (i >= len) {
        return 0;
    }
    if (s[i] == '0') {
        i++;                                    /* a lone leading zero: "012" is not allowed */
    } else if (s[i] >= '1' && s[i] <= '9') {
        while (i < len && is_digit_char(s[i])) {
            i++;
        }
    } else {
        return 0;
    }
    if (i < len && s[i] == '.') {
        i++;
        digitsStart = i;
        while (i < len && is_digit_char(s[i])) {
            i++;
        }
        if (i == digitsStart) {
            return 0;                           /* "1." has no digits after the point */
        }
    }
    if (i < len && (s[i] == 'e' || s[i] == 'E')) {
        i++;
        if (i < len && (s[i] == '+' || s[i] == '-')) {
            i++;
        }
        digitsStart = i;
        while (i < len && is_digit_char(s[i])) {
            i++;
        }
        if (i == digitsStart) {
            return 0;
        }
    }
    return i == len;
}

/* Parse an unquoted value: true, false, null or a number.
   On success *out = malloc'd copy of the token text and *pp moves past it. */
static Status parse_bare_value(const char **pp, char **out)
{
    const char *start = *pp;
    const char *end = start;
    size_t len;
    char *copy;

    /* A bare token ends at whitespace, ',', '}' or the end of the text. */
    while (*end != '\0' && *end != ',' && *end != '}' &&
           *end != ' ' && *end != '\t' && *end != '\n' && *end != '\r') {
        end++;
    }
    len = (size_t)(end - start);
    if (len == 0) {
        return STATUS_ERR_BAD_REQUEST;          /* e.g. {"a":} */
    }
    if (!token_equals(start, len, "true") && !token_equals(start, len, "false") &&
        !token_equals(start, len, "null") && !is_json_number(start, len)) {
        return STATUS_ERR_BAD_REQUEST;
    }

    copy = malloc(len + 1);                     /* len >= 1, never malloc(0) */
    if (copy == NULL) {
        return STATUS_ERR_NOMEM;
    }
    memcpy(copy, start, len);
    copy[len] = '\0';

    *out = copy;
    *pp = end;
    return STATUS_OK;
}

/* Append one pair to obj. On success obj now OWNS key and value.
   On failure the caller still owns them and must free them. 'capacity' is the size of
   obj->pairs (the JsonObject struct itself has no capacity field). */
static Status add_pair(JsonObject *obj, int *capacity, char *key, char *value, int isString)
{
    if (obj->count == *capacity) {
        int newCap;
        JsonPair *tmp;

        if (*capacity > INT_MAX / 2) {
            return STATUS_ERR_NOMEM;
        }
        newCap = (*capacity == 0) ? JSON_MIN_PAIRS : *capacity * 2;
        if ((size_t)newCap > SIZE_MAX / sizeof *tmp) {      /* guard size multiplication */
            return STATUS_ERR_NOMEM;
        }
        tmp = realloc(obj->pairs, (size_t)newCap * sizeof *tmp);
        if (tmp == NULL) {
            return STATUS_ERR_NOMEM;                        /* old array is still valid */
        }
        obj->pairs = tmp;
        *capacity = newCap;
    }
    obj->pairs[obj->count].key = key;
    obj->pairs[obj->count].value = value;
    obj->pairs[obj->count].isString = isString;
    obj->count++;
    return STATUS_OK;
}

void json_object_free(JsonObject *obj)
{
    int i;

    if (obj == NULL) {
        return;
    }
    for (i = 0; i < obj->count; i++) {
        free(obj->pairs[i].key);
        free(obj->pairs[i].value);
    }
    free(obj->pairs);
    obj->pairs = NULL;              /* makes a second call a harmless no-op */
    obj->count = 0;
}

/* 'out' must be a fresh (or already freed) object: it is overwritten, not freed first. */
Status json_parse_object(const char *text, JsonObject *out)
{
    JsonObject result;              /* built privately, copied to *out only on success */
    int capacity = 0;
    const char *p;
    Status st = STATUS_OK;

    if (out == NULL) {
        return STATUS_ERR_BAD_REQUEST;
    }
    out->pairs = NULL;
    out->count = 0;
    if (text == NULL) {
        return STATUS_ERR_BAD_REQUEST;
    }

    result.pairs = NULL;
    result.count = 0;

    p = skip_ws(text);
    if (*p != '{') {
        return STATUS_ERR_BAD_REQUEST;
    }
    p = skip_ws(p + 1);

    if (*p == '}') {
        p++;                        /* empty object {} */
    } else {
        for (;;) {
            char *key = NULL;
            char *value = NULL;
            int isString = 0;

            /* "key" */
            if (*p != '"') {
                st = STATUS_ERR_BAD_REQUEST;
                break;
            }
            st = parse_string(&p, &key);
            if (st != STATUS_OK) {
                break;
            }

            /* : */
            p = skip_ws(p);
            if (*p != ':') {
                free(key);
                st = STATUS_ERR_BAD_REQUEST;
                break;
            }
            p = skip_ws(p + 1);

            /* value */
            if (*p == '"') {
                isString = 1;
                st = parse_string(&p, &value);
            } else if (*p == '{' || *p == '[') {
                st = STATUS_ERR_BAD_REQUEST;        /* nested objects/arrays are not supported */
            } else {
                st = parse_bare_value(&p, &value);
            }
            if (st != STATUS_OK) {
                free(key);                          /* value was never assigned on failure */
                break;
            }

            st = add_pair(&result, &capacity, key, value, isString);
            if (st != STATUS_OK) {
                free(key);                          /* not owned by 'result' yet */
                free(value);
                break;
            }

            /* , or } */
            p = skip_ws(p);
            if (*p == ',') {
                p = skip_ws(p + 1);
                continue;                           /* a trailing comma fails the '"' test above */
            }
            if (*p == '}') {
                p++;
                break;
            }
            st = STATUS_ERR_BAD_REQUEST;
            break;
        }
    }

    if (st == STATUS_OK) {
        p = skip_ws(p);
        if (*p != '\0') {
            st = STATUS_ERR_BAD_REQUEST;            /* garbage after the closing brace */
        }
    }

    if (st != STATUS_OK) {
        json_object_free(&result);                  /* free everything allocated so far */
        return st;                                  /* *out is already empty */
    }
    *out = result;
    return STATUS_OK;
}

/* Find a key, searching from the end so the last duplicate wins. */
static const JsonPair *find_pair(const JsonObject *obj, const char *key)
{
    int i;

    if (obj == NULL || key == NULL || obj->pairs == NULL) {
        return NULL;
    }
    for (i = obj->count - 1; i >= 0; i--) {
        if (strcmp(obj->pairs[i].key, key) == 0) {
            return &obj->pairs[i];
        }
    }
    return NULL;
}

int json_has_key(const JsonObject *obj, const char *key)
{
    return find_pair(obj, key) != NULL;
}

Status json_get_string(const JsonObject *obj, const char *key, char *out, size_t outSize)
{
    const JsonPair *pair;
    size_t len;

    if (out != NULL && outSize > 0) {
        out[0] = '\0';              /* callers get "" even when we fail */
    }
    pair = find_pair(obj, key);
    if (pair == NULL || !pair->isString) {
        return STATUS_ERR_NOT_FOUND;
    }
    if (out == NULL || outSize == 0) {
        return STATUS_ERR_BAD_REQUEST;
    }
    len = strlen(pair->value);
    if (len >= outSize) {           /* need len chars + the NUL; never truncate silently */
        return STATUS_ERR_BAD_REQUEST;
    }
    memcpy(out, pair->value, len + 1);
    return STATUS_OK;
}

Status json_get_int(const JsonObject *obj, const char *key, int *out)
{
    const JsonPair *pair = find_pair(obj, key);
    const char *s;
    int negative = 0;
    unsigned long long limit;
    unsigned long long acc = 0;

    if (pair == NULL) {
        return STATUS_ERR_NOT_FOUND;
    }
    if (out == NULL || pair->isString) {
        return STATUS_ERR_BAD_REQUEST;              /* "12" in quotes is not a number token */
    }

    s = pair->value;
    if (*s == '-') {
        negative = 1;
        s++;
    }
    if (*s == '\0') {
        return STATUS_ERR_BAD_REQUEST;              /* just "-" */
    }

    /* int range is -2147483648 .. 2147483647 (when int is 32 bits). */
    limit = negative ? (unsigned long long)INT_MAX + 1ULL : (unsigned long long)INT_MAX;
    for (; *s != '\0'; s++) {
        if (!is_digit_char(*s)) {
            return STATUS_ERR_BAD_REQUEST;          /* '.', 'e', letters, true, null ... */
        }
        acc = acc * 10ULL + (unsigned long long)(*s - '0');
        if (acc > limit) {                          /* checked every step, so it cannot overflow */
            return STATUS_ERR_BAD_REQUEST;
        }
    }

    *out = negative ? (int)(-(long long)acc) : (int)acc;
    return STATUS_OK;
}

Status json_get_bool(const JsonObject *obj, const char *key, int *out)
{
    const JsonPair *pair = find_pair(obj, key);

    if (pair == NULL) {
        return STATUS_ERR_NOT_FOUND;
    }
    if (out == NULL || pair->isString) {
        return STATUS_ERR_BAD_REQUEST;              /* "true" in quotes is rejected */
    }
    if (strcmp(pair->value, "true") == 0 || strcmp(pair->value, "1") == 0) {
        *out = 1;
        return STATUS_OK;
    }
    if (strcmp(pair->value, "false") == 0 || strcmp(pair->value, "0") == 0) {
        *out = 0;
        return STATUS_OK;
    }
    return STATUS_ERR_BAD_REQUEST;
}