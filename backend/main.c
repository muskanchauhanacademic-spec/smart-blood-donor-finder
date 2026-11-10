/*
 * main.c - program entry point for Smart Blood Donor Finder.
 *
 * Usage:   sbdf.exe [--port N] [--demo]
 *
 * Order of events:
 *   1. read the command-line options
 *   2. app_init         (create the empty record store)
 *   3. optional demo    (--demo: fill the store with fictional sample data)
 *   4. server_run       (blocks until Ctrl+C)
 *   5. app_free         (release ALL memory)
 *
 * main() owns the AppState, so main() is the one that frees it.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "common.h"
#include "http_server.h"
#include "sample_data.h"

#define DEFAULT_PORT 8080

static void print_usage(const char *programName)
{
    fprintf(stderr, "Usage: %s [--port N] [--demo]\n", programName);
    fprintf(stderr, "  --port N   listen on port N (1-65535, default %d)\n", DEFAULT_PORT);
    fprintf(stderr, "  --demo     start with fictional sample donors and patients\n");
}

/* Convert text such as "8080" to an int in 1..65535. Returns 1 on success. */
static int parse_port(const char *text, int *portOut)
{
    char *end = NULL;
    long value;

    if (text == NULL || *text == '\0') {
        return 0;
    }
    errno = 0;
    value = strtol(text, &end, 10);
    /* *end must be '\0': otherwise there was extra text such as "80abc". */
    if (errno != 0 || *end != '\0' || value < 1 || value > 65535) {
        return 0;
    }
    *portOut = (int)value;
    return 1;
}

int main(int argc, char **argv)
{
    AppState app;
    int port = DEFAULT_PORT;
    int demo = 0;
    int exitCode = 0;
    int i;
    Status st;

    /* 1. Command line. Nothing is allocated yet, so returning early is safe. */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0) {
            if (i + 1 >= argc || !parse_port(argv[i + 1], &port)) {
                fprintf(stderr, "Error: --port needs a number from 1 to 65535.\n");
                print_usage(argv[0]);
                return 1;
            }
            i++;                             /* skip the number we just consumed */
        } else if (strcmp(argv[i], "--demo") == 0) {
            demo = 1;
        } else {
            fprintf(stderr, "Error: unknown option '%s'.\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    /* 2. Create the state. */
    memset(&app, 0, sizeof app);
    st = app_init(&app);
    if (st != STATUS_OK) {
        fprintf(stderr, "Could not start: %s\n", status_message(st));
        return 1;
    }

    /* 3. Optional demo data (Dev 4's module). */
    if (demo) {
        st = sample_data_load(&app.store);
        if (st != STATUS_OK) {
            fprintf(stderr, "Could not load demo data: %s\n", status_message(st));
            exitCode = 1;
        } else {
            printf("Demo data loaded: %d donors, %d patients.\n",
                   app.store.donors.count, app.store.patients.count);
        }
    }

    /* 4. Run the server (only if everything above worked). */
    if (exitCode == 0) {
        printf("Smart Blood Donor Finder is running at http://localhost:%d/\n", port);
        printf("Press Ctrl+C to stop.\n");
        fflush(stdout);                      /* show the text before the server blocks */

        st = server_run(&app, port);
        if (st != STATUS_OK) {
            fprintf(stderr, "Server error: %s\n", status_message(st));
            exitCode = 1;
        }
    }

    /* 5. Always release memory, even after an error. */
    app_free(&app);
    printf("Server stopped. All memory released.\n");
    return exitCode;
}