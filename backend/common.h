#ifndef COMMON_H
#define COMMON_H
#include <stddef.h>
#define NAME_LEN  50
#define BG_LEN    5
#define CITY_LEN  30
#define PHONE_LEN 15
#define BG_COUNT  8
typedef enum {
    STATUS_OK = 0,
    STATUS_ERR_NOMEM,
    STATUS_ERR_INVALID_ID,
    STATUS_ERR_INVALID_NAME,
    STATUS_ERR_INVALID_BLOOD_GROUP,
    STATUS_ERR_INVALID_CITY,
    STATUS_ERR_INVALID_PHONE,
    STATUS_ERR_INVALID_AVAILABILITY,
    STATUS_ERR_DUPLICATE_ID,
    STATUS_ERR_NOT_FOUND,
    STATUS_ERR_BAD_REQUEST,
    STATUS_ERR_METHOD_NOT_ALLOWED,
    STATUS_ERR_PAYLOAD_TOO_LARGE,
    STATUS_ERR_INTERNAL
} Status;
const char *status_code_string(Status s);  /* see table in section I */
const char *status_message(Status s);      /* default human-readable text */
int         status_http_code(Status s);    /* 200 for OK, else see section I */
#endif

