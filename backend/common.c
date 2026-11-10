/*
 * common.c - helpers that turn a Status value into text and an HTTP code.
 *
 * Every module returns a Status. The HTTP layer calls these three functions
 * to build the error JSON and to choose the HTTP status line.
 *
 * Design note: we look the status up by SEARCHING a table instead of using the
 * enum value as an array index. That way the code stays correct even if
 * somebody reorders the enum, and an unknown value cannot read outside the
 * table.
 */
#include "common.h"

typedef struct {
    Status      status;    /* which enum value this row describes */
    const char *code;      /* machine-readable text used in the JSON "code" field */
    const char *message;   /* default human-readable text */
    int         httpCode;  /* HTTP status code (section I of the spec) */
} StatusInfo;

/* static const lookup table (allowed by the coding standards). */
static const StatusInfo STATUS_TABLE[] = {
    { STATUS_OK,                     "OK",                   "OK",                                                           200 },
    { STATUS_ERR_NOMEM,              "OUT_OF_MEMORY",        "The server ran out of memory",                                 500 },
    { STATUS_ERR_INVALID_ID,         "INVALID_ID",           "Id must be a whole number from 1 to 2147483647",               400 },
    { STATUS_ERR_INVALID_NAME,       "INVALID_NAME",         "Name must be 1 to 49 characters with no control characters",   400 },
    { STATUS_ERR_INVALID_BLOOD_GROUP, "INVALID_BLOOD_GROUP", "Blood group must be one of A+, A-, B+, B-, AB+, AB-, O+, O-", 400 },
    { STATUS_ERR_INVALID_CITY,       "INVALID_CITY",         "City must be 1 to 29 characters with no control characters",   400 },
    { STATUS_ERR_INVALID_PHONE,      "INVALID_PHONE",        "Phone must be 7 to 14 characters: an optional leading + then digits only", 400 },
    { STATUS_ERR_INVALID_AVAILABILITY, "INVALID_AVAILABILITY", "Availability must be true or false",                        400 },
    { STATUS_ERR_DUPLICATE_ID,       "DUPLICATE_ID",         "A record with this id already exists",                         409 },
    { STATUS_ERR_NOT_FOUND,          "NOT_FOUND",            "The requested record or path was not found",                   404 },
    { STATUS_ERR_BAD_REQUEST,        "BAD_REQUEST",          "The request is malformed",                                     400 },
    { STATUS_ERR_METHOD_NOT_ALLOWED, "METHOD_NOT_ALLOWED",   "This HTTP method is not allowed for this path",                405 },
    { STATUS_ERR_PAYLOAD_TOO_LARGE,  "PAYLOAD_TOO_LARGE",    "The request body is too large",                                413 },
    /* INTERNAL must stay LAST: status_lookup() returns the last row for unknown values. */
    { STATUS_ERR_INTERNAL,           "INTERNAL_ERROR",       "Internal server error",                                        500 }
};

#define STATUS_TABLE_COUNT (sizeof STATUS_TABLE / sizeof STATUS_TABLE[0])

static const StatusInfo *status_lookup(Status s)
{
    size_t i;

    for (i = 0; i < STATUS_TABLE_COUNT; i++) {
        if (STATUS_TABLE[i].status == s) {
            return &STATUS_TABLE[i];
        }
    }
    /* Unknown value: report it as an internal error rather than crash. */
    return &STATUS_TABLE[STATUS_TABLE_COUNT - 1];
}

const char *status_code_string(Status s)
{
    return status_lookup(s)->code;
}

const char *status_message(Status s)
{
    return status_lookup(s)->message;
}

int status_http_code(Status s)
{
    return status_lookup(s)->httpCode;
}