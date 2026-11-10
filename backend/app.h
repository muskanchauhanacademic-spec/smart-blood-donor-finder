#ifndef APP_H
#define APP_H
#include "common.h"
#include "records.h"
typedef struct AppState { RecordStore store; } AppState;
Status app_init(AppState *app);
void   app_free(AppState *app);
#endif

