/*
 * matching_test_stubs.c - THROWAWAY stand-ins so that test_matching.c can be
 * linked with backend/matching.c alone (Developer 4, tests only).
 *
 * Do NOT link this file together with the real backend/json_util.c or
 * backend/records.c: the names would clash. It implements just enough of
 * sb_*, donor_write_json and patient_write_json for matching_write_json.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../backend/json_util.h"
#include "../backend/records.h"

void sb_init(StrBuf *sb)
{
    sb->data = NULL;
    sb->length = 0;
    sb->capacity = 0;
    sb->failed = 0;
}

void sb_free(StrBuf *sb)
{
    free(sb->data);
    sb_init(sb);
}

/* Makes room for 'extra' more bytes plus the closing '\0'. */
static int sb_reserve(StrBuf *sb, size_t extra)
{
    size_t needed;
    size_t newCapacity;
    char *bigger;

    if (sb->failed) {
        return 0;
    }
    if (extra > SIZE_MAX - sb->length - 1) {
        sb->failed = 1;
        return 0;
    }
    needed = sb->length + extra + 1;
    if (needed <= sb->capacity) {
        return 1;
    }
    newCapacity = sb->capacity == 0 ? 64 : sb->capacity;
    while (newCapacity < needed) {
        newCapacity *= 2;
    }
    bigger = (char *)realloc(sb->data, newCapacity);
    if (bigger == NULL) {
        sb->failed = 1;
        return 0;
    }
    sb->data = bigger;
    sb->capacity = newCapacity;
    return 1;
}

void sb_append_n(StrBuf *sb, const char *data, size_t n)
{
    if (!sb_reserve(sb, n)) {
        return;
    }
    memcpy(sb->data + sb->length, data, n);
    sb->length += n;
    sb->data[sb->length] = '\0';
}

void sb_append(StrBuf *sb, const char *text)
{
    sb_append_n(sb, text, strlen(text));
}

void sb_append_char(StrBuf *sb, char c)
{
    sb_append_n(sb, &c, 1);
}

void sb_appendf(StrBuf *sb, const char *fmt, ...)
{
    va_list args;
    va_list copy;
    int needed;

    if (sb->failed) {
        return;
    }
    va_start(args, fmt);
    va_copy(copy, args);
    needed = vsnprintf(NULL, 0, fmt, copy);   /* first call only measures */
    va_end(copy);
    if (needed < 0 || !sb_reserve(sb, (size_t)needed)) {
        sb->failed = 1;
        va_end(args);
        return;
    }
    vsnprintf(sb->data + sb->length, (size_t)needed + 1, fmt, args);
    sb->length += (size_t)needed;
    va_end(args);
}

void sb_append_json_string(StrBuf *sb, const char *text)
{
    const char *p;
    char buffer[8];

    sb_append_char(sb, '"');
    for (p = text; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '"') {
            sb_append(sb, "\\\"");
        } else if (c == '\\') {
            sb_append(sb, "\\\\");
        } else if (c < 0x20) {
            snprintf(buffer, sizeof buffer, "\\u%04x", (unsigned)c);
            sb_append(sb, buffer);
        } else {
            sb_append_char(sb, (char)c);
        }
    }
    sb_append_char(sb, '"');
}

const char *sb_cstr(const StrBuf *sb)
{
    return sb->data == NULL ? "" : sb->data;
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