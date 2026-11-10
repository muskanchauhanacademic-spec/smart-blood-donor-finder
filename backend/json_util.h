#ifndef JSON_UTIL_H
#define JSON_UTIL_H
#include <stddef.h>
#include "common.h"
/* Growable string. 'failed' becomes 1 if any allocation fails; after that all appends are no-ops.
   Check failed once at the end. Zero-initialised StrBuf is valid. */
typedef struct { char *data; size_t length; size_t capacity; int failed; } StrBuf;
void        sb_init(StrBuf *sb);
void        sb_clear(StrBuf *sb);                 /* length=0, failed=0, keeps memory */
void        sb_free(StrBuf *sb);
void        sb_append(StrBuf *sb, const char *text);
void        sb_append_n(StrBuf *sb, const char *data, size_t n);   /* binary-safe */
void        sb_append_char(StrBuf *sb, char c);
void        sb_appendf(StrBuf *sb, const char *fmt, ...);
void        sb_append_json_string(StrBuf *sb, const char *text);   /* adds quotes + escapes " \ control chars */
const char *sb_cstr(const StrBuf *sb);            /* never NULL: returns "" if empty */

/* Parser for ONE flat JSON object: values may be string, number, true, false, null.
   Nested objects/arrays => STATUS_ERR_BAD_REQUEST. Strings: supports \" \\ \/ \b \f \n \r \t;
   \uXXXX => STATUS_ERR_BAD_REQUEST (documented limitation); raw UTF-8 bytes pass through. */
typedef struct { char *key; char *value; int isString; } JsonPair;   /* value is unescaped text / raw token */
typedef struct { JsonPair *pairs; int count; } JsonObject;           /* pairs grown with realloc */
Status json_parse_object(const char *text, JsonObject *out);  /* OK, BAD_REQUEST, NOMEM; on non-OK 'out' is empty */
void   json_object_free(JsonObject *obj);
int    json_has_key(const JsonObject *obj, const char *key);
Status json_get_string(const JsonObject *obj, const char *key, char *out, size_t outSize);
       /* OK; NOT_FOUND if absent or not a string; BAD_REQUEST if it does not fit outSize */
Status json_get_int(const JsonObject *obj, const char *key, int *out);
       /* OK; NOT_FOUND if absent; BAD_REQUEST if not an integer number token (e.g. "12", 1.5, abc) */
Status json_get_bool(const JsonObject *obj, const char *key, int *out);
       /* accepts true/false/1/0 (not quoted); NOT_FOUND if absent; BAD_REQUEST otherwise */
#endif

