#ifndef RECORDS_H
#define RECORDS_H
#include "common.h"
#include "json_util.h"
typedef struct Donor {
    int  id;
    char name[NAME_LEN];
    char bloodGroup[BG_LEN];   /* canonical upper-case: A+ A- B+ B- AB+ AB- O+ O- */
    char city[CITY_LEN];
    char phone[PHONE_LEN];
    int  available;            /* 1 or 0 */
} Donor;
typedef struct Patient {
    int  id;
    char name[NAME_LEN];
    char bloodGroup[BG_LEN];
    char city[CITY_LEN];
} Patient;
typedef struct { Donor   *items; int count; int capacity; } DonorArray;
typedef struct { Patient *items; int count; int capacity; } PatientArray;
typedef struct { DonorArray donors; PatientArray patients; } RecordStore;
#define RECORDS_INITIAL_CAPACITY 4

Status records_init(RecordStore *store);   /* malloc initial capacity for both arrays */
void   records_free(RecordStore *store);   /* NULL/zero safe */
/* Raw inputs are validated here (rules in section G) and copied safely. First failing check is
   returned in this order: id, name, bloodGroup, city, phone, available, duplicate id. */
Status records_add_donor(RecordStore *store, int id, const char *name, const char *bloodGroup,
                         const char *city, const char *phone, int available);
Status records_add_patient(RecordStore *store, int id, const char *name, const char *bloodGroup,
                           const char *city);
const Donor   *records_find_donor(const RecordStore *store, int id);    /* NULL if absent. BORROWED */
const Patient *records_find_patient(const RecordStore *store, int id);  /* NULL if absent. BORROWED */
Status records_update_donor(RecordStore *store, int id, const char *name, const char *city,
                            const char *phone);                  /* blood group is NOT editable */
Status records_set_donor_availability(RecordStore *store, int id, int available);
int    records_available_donor_count(const RecordStore *store);
/* Read-only direct access to store->donors.items[i] / count is allowed everywhere.
   Mutation only through the functions above. */
void   donor_write_json(StrBuf *sb, const Donor *d);
void   patient_write_json(StrBuf *sb, const Patient *p);
#endif

