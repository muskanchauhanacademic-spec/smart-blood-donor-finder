/*
 * app.c - creation and destruction of the single AppState object.
 *
 * main() owns the AppState (it lives on main's stack). These two functions
 * only set up / tear down what is inside it, which is currently just the
 * RecordStore.
 */
#include "app.h"

Status app_init(AppState *app)
{
    if (app == NULL) {
        return STATUS_ERR_INTERNAL;
    }
    /* records_init zeroes the store first, so a failure leaves it safe to free. */
    return records_init(&app->store);
}

void app_free(AppState *app)
{
    if (app == NULL) {
        return;
    }
    /* records_free is safe to call twice, so calling app_free twice is also safe. */
    records_free(&app->store);
}