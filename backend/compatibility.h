#ifndef COMPATIBILITY_H
#define COMPATIBILITY_H
#include "common.h"
#include "records.h"
/* Index order: A+=0 A-=1 B+=2 B-=3 AB+=4 AB-=5 O+=6 O-=7 */
int         compat_blood_group_index(const char *bloodGroup);   /* 0..7 or -1; case-insensitive, no trimming */
const char *compat_blood_group_name(int index);                 /* "A+".. or NULL */
Status      compat_normalize_blood_group(const char *input, char out[BG_LEN]);
            /* trims spaces, upper-cases; STATUS_ERR_INVALID_BLOOD_GROUP if not one of the 8 */
int         compat_can_donate(const char *donorGroup, const char *patientGroup);  /* 1/0; 0 if either invalid */
int         compat_city_matches(const char *donorCity, const char *cityFilter);
            /* NULL or "" filter => 1; otherwise case-insensitive equality (own tolower loop) */
int         compat_is_edge(const Donor *donor, const Patient *patient, const char *cityFilter);
            /* 1 iff can_donate && donor->available==1 && city matches. THE single edge rule. */
#endif

