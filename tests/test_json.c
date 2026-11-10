/* test_json.c - standalone tests for backend/json_util.c (Developer 2).
 *
 * Build and run from the project root:
 *   gcc -std=c11 -Wall -Wextra -pedantic -g -Ibackend -o test_json tests/test_json.c backend/json_util.c
 *   ./test_json
 */
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json_util.h"

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

/* ---------------------------------------------------------------- StrBuf */

static void test_strbuf_basic(void)
{
    StrBuf sb;
    size_t cap;
    const char raw[3] = { 'a', '\0', 'b' };      /* has an embedded NUL on purpose */

    sb_init(&sb);
    check(strcmp(sb_cstr(&sb), "") == 0, "empty StrBuf: sb_cstr returns \"\" (never NULL)");
    check(sb.length == 0 && sb.failed == 0, "empty StrBuf: length 0 and not failed");
    check(strcmp(sb_cstr(NULL), "") == 0, "sb_cstr(NULL) returns \"\"");

    sb_append(&sb, "hello");
    sb_append_char(&sb, ' ');
    sb_appendf(&sb, "%d-%s", 42, "x");
    check(strcmp(sb_cstr(&sb), "hello 42-x") == 0, "append + append_char + appendf build the expected text");
    check(sb.length == strlen(sb.data), "length matches strlen of the data");

    sb_append(&sb, NULL);
    sb_append_n(&sb, NULL, 5);
    sb_append_n(&sb, "zzz", 0);
    check(strcmp(sb_cstr(&sb), "hello 42-x") == 0, "NULL text and n==0 are ignored");

    sb_clear(&sb);
    check(sb.length == 0 && strcmp(sb_cstr(&sb), "") == 0 && sb.failed == 0, "sb_clear empties the text");
    sb_append_n(&sb, raw, 3);
    check(sb.length == 3 && memcmp(sb.data, raw, 3) == 0 && sb.data[3] == '\0',
          "sb_append_n is binary-safe (embedded NUL kept) and still NUL-terminated");

    sb_append(&sb, "abc");
    cap = sb.capacity;
    sb_clear(&sb);
    check(sb.capacity == cap && sb.data != NULL, "sb_clear keeps the allocated memory");

    sb_free(&sb);
    check(sb.data == NULL && sb.length == 0 && sb.capacity == 0, "sb_free resets the buffer");
    sb_free(&sb);
    check(sb.data == NULL, "sb_free twice is safe");
    sb_append(&sb, "again");
    check(strcmp(sb_cstr(&sb), "again") == 0, "buffer is usable again after sb_free");
    sb_free(&sb);
}

static void test_strbuf_growth(void)
{
    StrBuf sb;
    int i;
    char longText[301];

    sb_init(&sb);
    for (i = 0; i < 1000; i++) {
        sb_append(&sb, "0123456789");
    }
    check(sb.failed == 0, "1000 appends of 10 bytes did not fail");
    check(sb.length == 10000, "length is 10000 after growing far beyond the initial capacity");
    check(sb.capacity > sb.length, "capacity leaves room for the terminating NUL");
    check(strlen(sb.data) == sb.length && sb.data[sb.length] == '\0', "text is NUL-terminated after growth");
    check(strncmp(sb.data, "0123456789", 10) == 0 && strcmp(sb.data + 9990, "0123456789") == 0,
          "content is intact at the start and at the end");

    sb_clear(&sb);
    memset(longText, 'x', 300);
    longText[300] = '\0';
    sb_appendf(&sb, "[%s]", longText);              /* one single appendf larger than the capacity */
    check(sb.length == 302 && sb.data[0] == '[' && sb.data[301] == ']' && sb.failed == 0,
          "appendf with output longer than the buffer grows it correctly");
    sb_appendf(&sb, "%s", "");
    check(sb.length == 302, "appendf that produces no text changes nothing");
    sb_free(&sb);
}

static void test_strbuf_failure_flag(void)
{
    StrBuf sb;

    sb_init(&sb);
    sb_append(&sb, "abc");
    /* Asking for SIZE_MAX more bytes must be refused by the overflow guard (no real allocation
       is attempted and the bytes at "x" are never read). */
    sb_append_n(&sb, "x", SIZE_MAX);
    check(sb.failed == 1, "impossible size sets the 'failed' flag");
    check(sb.length == 3 && strcmp(sb_cstr(&sb), "abc") == 0, "earlier text is preserved after a failure");
    sb_append(&sb, "def");
    sb_append_char(&sb, '!');
    sb_appendf(&sb, "%d", 7);
    check(sb.length == 3 && strcmp(sb_cstr(&sb), "abc") == 0, "after a failure every append is a no-op");
    sb_clear(&sb);
    check(sb.failed == 0, "sb_clear resets the failed flag");
    sb_append(&sb, "ok");
    check(strcmp(sb_cstr(&sb), "ok") == 0, "appends work again after sb_clear");
    sb_free(&sb);
}

static void expect_escaped(const char *input, const char *expected, const char *description)
{
    StrBuf sb;
    sb_init(&sb);
    sb_append_json_string(&sb, input);
    check(strcmp(sb_cstr(&sb), expected) == 0, description);
    if (strcmp(sb_cstr(&sb), expected) != 0) {
        printf("  expected: %s\n  actual:   %s\n", expected, sb_cstr(&sb));
    }
    sb_free(&sb);
}

static void test_escaping(void)
{
    expect_escaped("hello", "\"hello\"", "plain text is just wrapped in quotes");
    expect_escaped("", "\"\"", "empty string becomes \"\"");
    expect_escaped(NULL, "\"\"", "NULL text becomes \"\"");
    expect_escaped("say \"hi\"", "\"say \\\"hi\\\"\"", "double quotes are escaped");
    expect_escaped("a\\b", "\"a\\\\b\"", "backslash is escaped");
    expect_escaped("l1\nl2\r\t", "\"l1\\nl2\\r\\t\"", "newline, carriage return and tab use short escapes");
    expect_escaped("\b\f", "\"\\b\\f\"", "backspace and form feed use short escapes");
    expect_escaped("a\x01" "b", "\"a\\u0001b\"", "other control characters become \\u00XX");
    expect_escaped("\x1f", "\"\\u001f\"", "0x1f becomes \\u001f");
    expect_escaped("caf\xc3\xa9", "\"caf\xc3\xa9\"", "UTF-8 bytes (>= 0x80) pass through unchanged");
    expect_escaped("/", "\"/\"", "forward slash is not escaped");
    expect_escaped("a\"b\\c\n\r\t\b\f\x01",
                   "\"a\\\"b\\\\c\\n\\r\\t\\b\\f\\u0001\"", "all escapes together");
}

/* ---------------------------------------------------------------- parser */

static void expect_parse_fail(const char *text, const char *description)
{
    JsonObject obj;
    Status st = json_parse_object(text, &obj);
    check(st == STATUS_ERR_BAD_REQUEST && obj.pairs == NULL && obj.count == 0, description);
    json_object_free(&obj);
}

static void test_parse_success(void)
{
    JsonObject obj;
    char buf[64];
    char tiny[5];
    char exact[9];
    int id = 0;
    int available = -1;
    Status st;

    st = json_parse_object(
        "  {\n"
        "    \"id\": 101,\n"
        "    \"name\" : \"Asha Rao\" ,\n"
        "    \"bloodGroup\":\"O+\",\n"
        "    \"city\":\"Nagpur\",\n"
        "    \"phone\":\"9000000101\",\n"
        "    \"available\": true\n"
        "  }  \n",
        &obj);
    check(st == STATUS_OK, "sample donor body parses (whitespace, newlines, spaces around colon)");
    check(obj.count == 6, "sample donor body has 6 pairs");
    check(json_has_key(&obj, "name") == 1 && json_has_key(&obj, "missing") == 0, "json_has_key");
    check(json_has_key(&obj, "Name") == 0, "keys are case-sensitive");
    check(json_has_key(NULL, "id") == 0 && json_has_key(&obj, NULL) == 0, "json_has_key tolerates NULL");

    check(json_get_int(&obj, "id", &id) == STATUS_OK && id == 101, "get_int id == 101");
    check(json_get_string(&obj, "name", buf, sizeof buf) == STATUS_OK && strcmp(buf, "Asha Rao") == 0,
          "get_string name == \"Asha Rao\"");
    check(json_get_string(&obj, "bloodGroup", buf, sizeof buf) == STATUS_OK && strcmp(buf, "O+") == 0,
          "get_string bloodGroup == O+");
    check(json_get_bool(&obj, "available", &available) == STATUS_OK && available == 1,
          "get_bool available == true");

    check(json_get_string(&obj, "id", buf, sizeof buf) == STATUS_ERR_NOT_FOUND,
          "get_string on a number value -> NOT_FOUND");
    check(json_get_string(&obj, "nope", buf, sizeof buf) == STATUS_ERR_NOT_FOUND && buf[0] == '\0',
          "get_string on an absent key -> NOT_FOUND and out is \"\"");
    check(json_get_int(&obj, "name", &id) == STATUS_ERR_BAD_REQUEST,
          "get_int on a string value -> BAD_REQUEST");
    check(json_get_string(&obj, "name", tiny, sizeof tiny) == STATUS_ERR_BAD_REQUEST && tiny[0] == '\0',
          "get_string into a too-small buffer -> BAD_REQUEST (never truncates)");
    check(json_get_string(&obj, "name", exact, 8) == STATUS_ERR_BAD_REQUEST,
          "get_string: \"Asha Rao\" is 8 chars and needs 9 bytes, so an 8-byte buffer is refused...");
    check(json_get_string(&obj, "name", exact, sizeof exact) == STATUS_OK && strcmp(exact, "Asha Rao") == 0,
          "...and a 9-byte buffer (8 chars + NUL) fits exactly");
    check(json_get_string(&obj, "name", NULL, 0) == STATUS_ERR_BAD_REQUEST,
          "get_string with NULL/0 buffer -> BAD_REQUEST");
    check(json_get_string(NULL, "name", buf, sizeof buf) == STATUS_ERR_NOT_FOUND,
          "get_string on a NULL object -> NOT_FOUND");

    json_object_free(&obj);
}

static void test_parse_values_and_escapes(void)
{
    JsonObject obj;
    char buf[128];
    int flag = -1;

    check(json_parse_object("{}", &obj) == STATUS_OK && obj.count == 0, "{} parses to an empty object");
    json_object_free(&obj);
    check(json_parse_object(" \n { \t } \r\n", &obj) == STATUS_OK && obj.count == 0,
          "whitespace around an empty object is fine");
    json_object_free(&obj);

    check(json_parse_object("{\"s\":\"l1\\nl2 \\\"q\\\" \\\\ \\/ \\t \\b \\f \\r\"}", &obj) == STATUS_OK,
          "string with all supported escapes parses");
    check(json_get_string(&obj, "s", buf, sizeof buf) == STATUS_OK &&
          strcmp(buf, "l1\nl2 \"q\" \\ / \t \b \f \r") == 0,
          "escapes are translated to the real characters");
    json_object_free(&obj);

    check(json_parse_object("{\"n\":\"caf\xc3\xa9\"}", &obj) == STATUS_OK, "raw UTF-8 bytes parse");
    check(json_get_string(&obj, "n", buf, sizeof buf) == STATUS_OK && strcmp(buf, "caf\xc3\xa9") == 0,
          "raw UTF-8 bytes pass through unchanged");
    json_object_free(&obj);

    check(json_parse_object("{\"\":\"empty key\",\"e\":\"\"}", &obj) == STATUS_OK, "empty key and empty value parse");
    check(json_get_string(&obj, "", buf, sizeof buf) == STATUS_OK && strcmp(buf, "empty key") == 0,
          "empty key can be looked up");
    check(json_get_string(&obj, "e", buf, sizeof buf) == STATUS_OK && buf[0] == '\0', "empty string value");
    json_object_free(&obj);

    check(json_parse_object("{\"a\":null,\"b\":false,\"c\":-1.5e3}", &obj) == STATUS_OK && obj.count == 3,
          "null, false and a decimal/exponent number parse");
    check(json_has_key(&obj, "a") == 1, "null value: key exists");
    check(json_get_string(&obj, "a", buf, sizeof buf) == STATUS_ERR_NOT_FOUND, "null is not a string");
    check(json_get_bool(&obj, "b", &flag) == STATUS_OK && flag == 0, "false value -> 0");
    json_object_free(&obj);

    check(json_parse_object("{\"a\":1,\"a\":2}", &obj) == STATUS_OK && obj.count == 2,
          "duplicate keys are accepted");
    {
        int v = 0;
        check(json_get_int(&obj, "a", &v) == STATUS_OK && v == 2, "duplicate keys: the LAST one wins");
    }
    json_object_free(&obj);
}

static void test_parse_failures(void)
{
    expect_parse_fail("{\"a\":{\"b\":1}}", "nested object -> BAD_REQUEST, out empty");
    expect_parse_fail("{\"a\":[1,2]}", "array value -> BAD_REQUEST");
    expect_parse_fail("{\"a\":\"\\u0041\"}", "\\u escape -> BAD_REQUEST (documented limitation)");
    expect_parse_fail("{\"a\":1", "missing closing brace -> BAD_REQUEST");
    expect_parse_fail("{\"a\":1} xyz", "trailing text after the object -> BAD_REQUEST");
    expect_parse_fail("{\"a\":1}{\"b\":2}", "two objects in a row -> BAD_REQUEST");
    expect_parse_fail("[1]", "top-level array -> BAD_REQUEST");
    expect_parse_fail("\"x\"", "top-level string -> BAD_REQUEST");
    expect_parse_fail("", "empty text -> BAD_REQUEST");
    expect_parse_fail("   ", "only whitespace -> BAD_REQUEST");
    expect_parse_fail("{", "only an opening brace -> BAD_REQUEST");
    expect_parse_fail("{\"a\":1,}", "trailing comma -> BAD_REQUEST");
    expect_parse_fail("{\"a\" 1}", "missing colon -> BAD_REQUEST");
    expect_parse_fail("{\"a\":}", "missing value -> BAD_REQUEST");
    expect_parse_fail("{a:1}", "unquoted key -> BAD_REQUEST");
    expect_parse_fail("{\"a\":\"abc}", "unterminated string -> BAD_REQUEST");
    expect_parse_fail("{\"a\":\"abc\\", "string ending in a lone backslash -> BAD_REQUEST");
    expect_parse_fail("{\"a\":abc}", "bare word value -> BAD_REQUEST");
    expect_parse_fail("{\"a\":tru}", "misspelled true -> BAD_REQUEST");
    expect_parse_fail("{\"a\":012}", "number with a leading zero -> BAD_REQUEST");
    expect_parse_fail("{\"a\":-}", "lone minus sign -> BAD_REQUEST");
    expect_parse_fail("{\"a\":1.}", "number with no digits after the point -> BAD_REQUEST");
    expect_parse_fail("{\"a\":\"x\\qy\"}", "unknown escape \\q -> BAD_REQUEST");
    expect_parse_fail("{\"a\":\"line1\nline2\"}", "raw newline inside a string -> BAD_REQUEST");
    expect_parse_fail("{\"a\":1,\"b\":{\"c\":2}}", "failure after a good pair frees the good pair (see sanitizer run)");
    expect_parse_fail("{\"a\":\"x\",\"b\":\"y\",\"c\":", "failure at the very end after several pairs");

    {
        Status st = json_parse_object("{}", NULL);
        check(st == STATUS_ERR_BAD_REQUEST, "NULL output pointer -> BAD_REQUEST (no crash)");
    }
    {
        JsonObject obj;
        check(json_parse_object(NULL, &obj) == STATUS_ERR_BAD_REQUEST && obj.pairs == NULL && obj.count == 0,
              "NULL text -> BAD_REQUEST, out empty");
    }
}

static void expect_int(const char *json, const char *key, Status expectedStatus, int expectedValue,
                       const char *description)
{
    JsonObject obj;
    int value = -12345;
    Status st;

    if (json_parse_object(json, &obj) != STATUS_OK) {
        check(0, description);
        printf("  (the test JSON itself failed to parse: %s)\n", json);
        return;
    }
    st = json_get_int(&obj, key, &value);
    check(st == expectedStatus && (expectedStatus != STATUS_OK || value == expectedValue), description);
    json_object_free(&obj);
}

static void test_get_int(void)
{
    expect_int("{\"a\":12}", "a", STATUS_OK, 12, "get_int 12");
    expect_int("{\"a\":-7}", "a", STATUS_OK, -7, "get_int -7");
    expect_int("{\"a\":0}", "a", STATUS_OK, 0, "get_int 0");
    expect_int("{\"a\":-0}", "a", STATUS_OK, 0, "get_int -0 is 0");
    expect_int("{\"a\":2147483647}", "a", STATUS_OK, INT_MAX, "get_int INT_MAX is accepted");
    expect_int("{\"a\":-2147483648}", "a", STATUS_OK, INT_MIN, "get_int INT_MIN is accepted");
    expect_int("{\"a\":2147483648}", "a", STATUS_ERR_BAD_REQUEST, 0, "get_int INT_MAX+1 -> BAD_REQUEST");
    expect_int("{\"a\":-2147483649}", "a", STATUS_ERR_BAD_REQUEST, 0, "get_int INT_MIN-1 -> BAD_REQUEST");
    expect_int("{\"a\":99999999999999999999}", "a", STATUS_ERR_BAD_REQUEST, 0,
               "get_int far beyond int (and beyond 64 bits) -> BAD_REQUEST, no overflow");
    expect_int("{\"a\":1.5}", "a", STATUS_ERR_BAD_REQUEST, 0, "get_int 1.5 -> BAD_REQUEST");
    expect_int("{\"a\":1.0}", "a", STATUS_ERR_BAD_REQUEST, 0, "get_int 1.0 -> BAD_REQUEST (no decimals at all)");
    expect_int("{\"a\":1e3}", "a", STATUS_ERR_BAD_REQUEST, 0, "get_int 1e3 -> BAD_REQUEST (no exponents)");
    expect_int("{\"a\":\"12\"}", "a", STATUS_ERR_BAD_REQUEST, 0, "get_int quoted \"12\" -> BAD_REQUEST");
    expect_int("{\"a\":true}", "a", STATUS_ERR_BAD_REQUEST, 0, "get_int true -> BAD_REQUEST");
    expect_int("{\"a\":null}", "a", STATUS_ERR_BAD_REQUEST, 0, "get_int null -> BAD_REQUEST");
    expect_int("{\"a\":1}", "b", STATUS_ERR_NOT_FOUND, 0, "get_int absent key -> NOT_FOUND");

    {
        int v = 0;
        check(json_get_int(NULL, "a", &v) == STATUS_ERR_NOT_FOUND, "get_int on a NULL object -> NOT_FOUND");
    }
}

static void expect_bool(const char *json, Status expectedStatus, int expectedValue, const char *description)
{
    JsonObject obj;
    int value = -1;
    Status st;

    if (json_parse_object(json, &obj) != STATUS_OK) {
        check(0, description);
        printf("  (the test JSON itself failed to parse: %s)\n", json);
        return;
    }
    st = json_get_bool(&obj, "a", &value);
    check(st == expectedStatus && (expectedStatus != STATUS_OK || value == expectedValue), description);
    json_object_free(&obj);
}

static void test_get_bool(void)
{
    expect_bool("{\"a\":true}", STATUS_OK, 1, "get_bool true -> 1");
    expect_bool("{\"a\":false}", STATUS_OK, 0, "get_bool false -> 0");
    expect_bool("{\"a\":1}", STATUS_OK, 1, "get_bool 1 -> 1");
    expect_bool("{\"a\":0}", STATUS_OK, 0, "get_bool 0 -> 0");
    expect_bool("{\"a\":\"true\"}", STATUS_ERR_BAD_REQUEST, 0, "get_bool quoted \"true\" -> BAD_REQUEST");
    expect_bool("{\"a\":\"1\"}", STATUS_ERR_BAD_REQUEST, 0, "get_bool quoted \"1\" -> BAD_REQUEST");
    expect_bool("{\"a\":2}", STATUS_ERR_BAD_REQUEST, 0, "get_bool 2 -> BAD_REQUEST");
    expect_bool("{\"a\":-1}", STATUS_ERR_BAD_REQUEST, 0, "get_bool -1 -> BAD_REQUEST");
    expect_bool("{\"a\":null}", STATUS_ERR_BAD_REQUEST, 0, "get_bool null -> BAD_REQUEST");
    expect_bool("{\"b\":true}", STATUS_ERR_NOT_FOUND, 0, "get_bool absent key -> NOT_FOUND");
}

static void test_many_pairs(void)
{
    char text[4096];
    size_t used = 0;
    int i;
    int ok = 1;
    JsonObject obj;
    int v = -1;

    used += (size_t)snprintf(text + used, sizeof text - used, "{");
    for (i = 0; i < 100; i++) {
        int written = snprintf(text + used, sizeof text - used, "%s\"k%d\":%d", i == 0 ? "" : ",", i, i);
        if (written < 0 || (size_t)written >= sizeof text - used) {
            ok = 0;
            break;
        }
        used += (size_t)written;
    }
    snprintf(text + used, sizeof text - used, "}");
    check(ok, "built a 100-pair JSON text");

    check(json_parse_object(text, &obj) == STATUS_OK && obj.count == 100,
          "100 pairs parse (the pairs array grows several times)");
    check(json_get_int(&obj, "k0", &v) == STATUS_OK && v == 0, "first pair readable");
    check(json_get_int(&obj, "k57", &v) == STATUS_OK && v == 57, "middle pair readable");
    check(json_get_int(&obj, "k99", &v) == STATUS_OK && v == 99, "last pair readable");
    json_object_free(&obj);
}

static void test_free_safety(void)
{
    JsonObject empty = { NULL, 0 };
    JsonObject obj;

    json_object_free(&empty);
    json_object_free(&empty);
    check(empty.pairs == NULL && empty.count == 0, "freeing a zeroed JsonObject (twice) is safe");

    json_object_free(NULL);
    check(1, "json_object_free(NULL) does not crash");

    check(json_parse_object("{\"a\":\"x\",\"b\":2}", &obj) == STATUS_OK, "parse before double free");
    json_object_free(&obj);
    check(obj.pairs == NULL && obj.count == 0, "after free the object is empty");
    json_object_free(&obj);
    check(obj.pairs == NULL && obj.count == 0, "second json_object_free is a no-op");
}

int main(void)
{
    test_strbuf_basic();
    test_strbuf_growth();
    test_strbuf_failure_flag();
    test_escaping();
    test_parse_success();
    test_parse_values_and_escapes();
    test_parse_failures();
    test_get_int();
    test_get_bool();
    test_many_pairs();
    test_free_safety();

    printf("\n%d checks, %d failed\n", g_checks, g_failures);
    printf(g_failures == 0 ? "ALL TESTS PASSED\n" : "SOME TESTS FAILED\n");
    return g_failures == 0 ? 0 : 1;
}