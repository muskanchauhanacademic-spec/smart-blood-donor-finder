#ifndef API_H
#define API_H
#include "http_server.h"
/* Dev 1 */ void api_get_donors(AppState*, const HttpRequest*, HttpResponse*);
            void api_post_donor(AppState*, const HttpRequest*, HttpResponse*);
            void api_get_donor(AppState*, const HttpRequest*, HttpResponse*);
            void api_put_donor(AppState*, const HttpRequest*, HttpResponse*);
            void api_put_donor_availability(AppState*, const HttpRequest*, HttpResponse*);
            void api_get_patients(AppState*, const HttpRequest*, HttpResponse*);
            void api_post_patient(AppState*, const HttpRequest*, HttpResponse*);
            void api_get_patient(AppState*, const HttpRequest*, HttpResponse*);
/* Dev 2 */ void api_get_search(AppState*, const HttpRequest*, HttpResponse*);
/* Dev 3 */ void api_get_graph(AppState*, const HttpRequest*, HttpResponse*);
            void api_get_graph_matrix(AppState*, const HttpRequest*, HttpResponse*);
            void api_get_graph_degrees(AppState*, const HttpRequest*, HttpResponse*);
/* Dev 4 */ void api_post_matching(AppState*, const HttpRequest*, HttpResponse*);
            void api_get_stats(AppState*, const HttpRequest*, HttpResponse*);
#endif

