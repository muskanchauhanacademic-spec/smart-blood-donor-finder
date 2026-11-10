# Presentation Handbook - Developer 1: Records, the HTTP Server, and How the Program Starts and Stops

**Project:** Smart Blood Donor Finder: A Graph Theory Based Blood Donor Search and Matching System Using C
**My role:** Developer 1 (Claude Account 2). I wrote the "foundation" that every other module stands on.
**Topic:** *Records, the HTTP server, and how the whole program starts and stops.*

> How to use this file: read sections 1-3 first, then section 10 (the script) out loud twice, then
> practise section 11 (viva questions). Anything marked **VERIFY** is something I could not check myself.
> Please check it before you present.

## 0. Files I own

Line counts are from `wc -l` on the files as I delivered them (they include comments and blank lines;
real code is roughly 60% of that, because I commented heavily for beginners).

| File | Purpose | Approx. lines |
|---|---|---|
| `backend/common.c` | Turns a `Status` value into a code string, a message and an HTTP number | 68 |
| `backend/records.c` | Stores donors and patients in memory, validates them, writes them as JSON | 501 |
| `backend/app.c` | Creates and destroys the one `AppState` object | 26 |
| `backend/http_server.c` | The web server (Winsock): sockets, reading requests, sending replies, static files | 873 |
| `backend/router.h` | Small internal header: declares `http_route_request` | 15 |
| `backend/router.c` | Route table: decides which `api_*` handler answers a request | 168 |
| `backend/api_records.c` | The 8 donor/patient HTTP handlers | 365 |
| `backend/main.c` | `main()`: command-line options, startup order, shutdown | 117 |
| `Makefile` | `make`, `make debug`, `make test`, `make clean` | 40 |
| `tests/test_records.c` | Standalone tests for `records.c` and `common.c` (114 checks) | 528 |

Not mine: the header files (`common.h`, `records.h`, `http_server.h`, ...). They were frozen in the
specification and the orchestrator generated the `.h` files from it (see section 7).

## 1. The 30-second summary

My part does three jobs. **First**, it keeps every donor and patient in memory in growable arrays and checks
every record before it is stored. **Second**, it is the web server: it listens on `127.0.0.1:8080`, reads an
HTTP request from the browser, finds the right handler, and sends the JSON answer back. **Third**, it is
`main()`: it starts everything in the right order and, when you press Ctrl+C, shuts it down and frees all
memory. The project needs this because Dev 2, 3, 4 and 5 all plug into it: their code reads my records,
registers behind my router, and answers through my server.

## 2. Where my part sits

```
 Browser (Dev 5: index.html, script.js)
      |   HTTP request over TCP  ("GET /api/stats HTTP/1.1 ...")
      v
+---------------------------------------------------------------+
| http_server.c   server_run -> handle_client -> receive_request |  <-- MINE
|        |                                   |                   |
|        | path starts with /api/            | any other GET     |
|        v                                   v                   |
|   router.c: http_route_request        serve_static (./frontend)|  <-- MINE
+--------|------------------------------------------------------+
         v
   api_* handlers
     api_records.c  (donors/patients)    <-- MINE
     api_search.c   (Dev 2)   api_graph.c (Dev 3)   api_matching.c + /api/stats (Dev 4)
         |
         v
   records_* functions  (records.c)      <-- MINE   -- uses -->  compat_normalize_blood_group (Dev 2)
         |
   RecordStore inside AppState  (created by app_init, freed by app_free; main() owns it)  <-- MINE

 Reply travels back: handler writes JSON into HttpResponse.body (a StrBuf, Dev 2's type)
                     -> send_response() -> browser
```

**Who calls me (exact functions):**

- `main()` in my own `main.c` calls `app_init`, `server_run`, `app_free`.
- The other handlers are *designed* to call `http_begin_ok`, `http_end_ok`, `http_send_error`,
  `http_query_get`, `donor_write_json`, `patient_write_json`, `records_find_donor`, `records_find_patient`
  and read `app->store.donors.items[i]` / `.count` directly (the spec allows read-only direct access).
  **VERIFY:** run `grep -n "http_query_get\|http_begin_ok\|donor_write_json" backend/*.c` to confirm
  exactly who calls what. I have not read their files.

**Whom I call:**

- Dev 2: `compat_normalize_blood_group` (from `records.c`), and the `StrBuf` helpers `sb_init`, `sb_clear`,
  `sb_free`, `sb_append`, `sb_append_n`, `sb_append_char`, `sb_appendf`, `sb_append_json_string`,
  `sb_cstr`, plus `json_parse_object`, `json_object_free`, `json_get_string`, `json_get_int`,
  `json_get_bool` (from `api_records.c`).
- Dev 4: `sample_data_load` (from `main.c`, only with `--demo`).
- Dev 2, 3, 4: `api_get_search`, `api_get_graph`, `api_get_graph_matrix`, `api_get_graph_degrees`,
  `api_post_matching`, `api_get_stats` (the router table points at them).

## 3. What I built, file by file

### 3.1 `common.c` - the status table

Every function in the project returns a `Status` (an enum: `STATUS_OK`, `STATUS_ERR_NOT_FOUND`, ...).
`common.c` has one `static const StatusInfo STATUS_TABLE[]` with four columns: the enum value, the code
text (`"NOT_FOUND"`), a default message and the HTTP number (404).

- `status_code_string(Status)` returns the code text.
- `status_message(Status)` returns the default human-readable text.
- `status_http_code(Status)` returns the HTTP number.
- `status_lookup` (private) *searches* the table with a loop. It does **not** use the enum value as an
  array index. If someone re-orders the enum, nothing breaks. An unknown value returns the last row,
  `INTERNAL_ERROR` / 500, so it can never read outside the table.

### 3.2 `records.c` - the in-memory database

- **Structs** (frozen in `records.h`): `Donor` = `id`, `name[50]`, `bloodGroup[5]`, `city[30]`,
  `phone[15]`, `available`. `Patient` = `id`, `name[50]`, `bloodGroup[5]`, `city[30]`.
  Measured with `sizeof` on my sandbox (gcc, 64-bit): `Donor` = 108 bytes, `Patient` = 92 bytes.
- **Growable arrays:** `DonorArray` and `PatientArray` each hold `items` (pointer to the block),
  `count` (how many are used) and `capacity` (how many fit). `RecordStore` holds one of each.
- **Private helpers** (the `static` functions, only visible inside `records.c`):
  `trim_bounds`, `copy_text`, `validate_text`, `validate_phone`, `normalize_group`, `next_capacity`,
  `donor_index`, `patient_index`.
- **Public functions:** `records_init`, `records_free`, `records_add_donor`, `records_add_patient`,
  `records_find_donor`, `records_find_patient`, `records_update_donor`,
  `records_set_donor_availability`, `records_available_donor_count`, `donor_write_json`,
  `patient_write_json`.

### 3.3 `app.c`

`app_init(app)` calls `records_init(&app->store)`. `app_free(app)` calls `records_free(&app->store)`.
Both return or do nothing safely if `app` is `NULL`. That is the whole file: it exists so that when
the project later needs more state, there is already one place to put it.

### 3.4 `http_server.c` - the web server

Read it from top to bottom; it follows the life of one request.

- **Response helpers** (declared in `http_server.h`): `http_begin_ok`, `http_end_ok`, `http_send_error`.
- **Query decoding:** `hex_value`, `url_decode` (private), `http_query_get` (public).
- **Reading a request:** `header_name_is`, `parse_content_length`, `find_header_end`,
  `parse_request_line`, `receive_request`.
- **Static files:** `equals_ignore_case`, `content_type_for`, `static_path_is_safe`, `serve_static`,
  and the table `MIME_TYPES`.
- **Sending:** `reason_phrase`, `send_all`, `send_response`, `close_client`.
- **One connection:** `dispatch_request`, `handle_client`.
- **The loop:** `on_interrupt`, `server_request_stop`, `server_run`.

### 3.5 `router.c` / `router.h`

One public function: `http_route_request(app, req, res)`. A table `ROUTES[]` has 14 rows of
`{method, pattern, handler}`. Private helpers: `segment_length`, `parse_id_segment`, `match_path`.
(I named it `http_route_request` so it begins with a module prefix from the spec's list; `router_` was
not on that list. See section 7.)

### 3.6 `api_records.c`

Eight handlers: `api_get_donors`, `api_post_donor`, `api_get_donor`, `api_put_donor`,
`api_put_donor_availability`, `api_get_patients`, `api_post_patient`, `api_get_patient`.
Private helpers: `body_is_blank`, `parse_body`, `read_text_field`, `read_id_field`,
`read_availability_field`, `respond_with_donor`, `respond_with_patient`, `respond_body_error`.

### 3.7 `main.c`

`main` plus `print_usage` and `parse_port`. Options: `--port N` (1-65535, default 8080) and `--demo`.

### 3.8 `Makefile` and `tests/test_records.c`

Covered in section 8 (tests) and 4.9 (build).

## 4. Algorithms and key ideas

### 4.1 The growable array: doubling with `realloc` through a temporary pointer

**Idea in plain English.** We do not know how many donors there will be. An array in C has a fixed size.
So we start with room for 4 records (`RECORDS_INITIAL_CAPACITY`). When the 5th record arrives and the array
is full, we ask the system for a block *twice as big*, copy the old records over (that is what `realloc`
does), and carry on. Think of a bus: when it is full, you swap it for a bus twice the size instead of
adding one seat at a time.

**Worked example (real numbers).** `sizeof(Donor)` = 108 bytes on my build.

| Add number | count before | capacity before | what happens | capacity after | bytes of the block |
|---|---|---|---|---|---|
| 1st-4th | 0-3 | 4 | fits | 4 | 432 (= 4 x 108) |
| 5th | 4 | 4 | full -> realloc | 8 | 864 |
| 9th | 8 | 8 | full -> realloc | 16 | 1,728 |
| 17th | 16 | 16 | full -> realloc | 32 | 3,456 |
| 33rd | 32 | 32 | full -> realloc | 64 | 6,912 |
| 65th | 64 | 64 | full -> realloc | 128 | 13,824 |

So 100 donors cause exactly 5 reallocs and end with capacity 128. The enlarged sample data (200 donors)
needs one more (128 -> 256, i.e. 27,648 bytes). 120 patients end at capacity 128 (128 x 92 = 11,776 bytes).
These capacities follow from the doubling rule; my test `test_add_many` checks that capacity is at least 100
and bigger than the initial 4 after 100 adds (it does not print the exact number).

**Pseudocode.**

```
add(record):
    validate everything first (nothing is changed if anything is wrong)
    if count == capacity:
        newCapacity = capacity * 2          (4 if capacity is 0)
        refuse if the arithmetic would overflow
        bigger = realloc(items, newCapacity * sizeof(record))
        if bigger is NULL: return "out of memory"   // old array is still valid
        items = bigger; capacity = newCapacity
    items[count] = record; count = count + 1
```

**The real code** (`records_add_donor` in `records.c`):

```c
        /* Use a TEMPORARY pointer: if realloc fails it returns NULL but the old
         * block is still valid, and assigning NULL straight to items would leak it. */
        bigger = realloc(store->donors.items, (size_t)newCapacity * sizeof(Donor));
        if (bigger == NULL) {
            return STATUS_ERR_NOMEM;         /* store unchanged */
        }
        store->donors.items = bigger;
        store->donors.capacity = newCapacity;
```

**Why the temporary pointer matters.** Suppose we wrote `items = realloc(items, newSize);`. If `realloc`
fails it returns `NULL`, and the old block is *not* freed. We have just overwritten our only pointer to it
with `NULL`. Now the old block still exists in memory but nobody can find it or free it: a **memory leak**.
Worse, our array now looks empty or invalid, so the next access crashes. With the temporary pointer
`bigger`, a failure leaves `items` untouched, the store is unchanged, and the program returns
`STATUS_ERR_NOMEM` (HTTP 500 `OUT_OF_MEMORY`) and keeps running.

**The overflow guard** (`next_capacity`). Before doubling, I check `current > INT_MAX / 2` and
`newCapacity > SIZE_MAX / elementSize`. This stops `capacity * 2 * sizeof(...)` from wrapping around to a
small number (which would make `realloc` return a block that is too small, and we would write past its end).

**Cost.** One add is *usually* cheap. A realloc copies at most `count` records, but it happens only at
4, 8, 16, ... so the total copying over n adds is about 2n records. We say adding is "amortised O(1)"
(cheap on average). Memory: at most about twice what is needed.

**Why this approach, and an alternative.** It is simple and fits the "no fixed limit on the number of
donors" rule. Alternative: a linked list (no copying, but slower to search and more pointers to manage), or
growing by +4 each time (simpler, but copies far more often: O(n squared) total).

### 4.2 The validation pipeline (check first, change nothing if anything fails)

**Idea.** Every record is checked in `records.c` *before* it is stored. Each text is first **trimmed**
(spaces removed from both ends), then checked, then copied into a local variable. Only if every check
passes is the record copied into the array. The exact order the spec demands, and my code follows, for
`records_add_donor`:

1. id  2. name  3. bloodGroup  4. city  5. phone  6. available  7. duplicate id.
(`records_add_patient` uses the same order without phone/available.) The *first* failing check is returned.

**Worked example.** Input: `id=0`, `name=""`, `bloodGroup="??"`. Everything is wrong, but the answer is
`INVALID_ID`, because id is checked first. Fix the id and resubmit: now you get `INVALID_NAME`. Then
`INVALID_BLOOD_GROUP`. This is tested in my test file ("everything wrong -> INVALID_ID is reported first").

Another example: input `" o+ "` for the blood group. `compat_normalize_blood_group` (Dev 2) trims and
upper-cases it to `"O+"`, and that canonical text is what we store.

**Pseudocode.**

```
fresh = empty record
if id < 1:                         return INVALID_ID
if name not valid (1..49 bytes, no control chars): return INVALID_NAME
if blood group not one of the 8:   return INVALID_BLOOD_GROUP
if city not valid (1..29 bytes):   return INVALID_CITY
if phone not valid:                return INVALID_PHONE
if available is not 0 or 1:        return INVALID_AVAILABILITY
if id already used by a donor:     return DUPLICATE_ID
store it
```

**Real code** (the text validator):

```c
    trim_bounds(input, &start, &length);
    if (length == 0 || length > maxBytes) {
        return 0;
    }
    for (i = 0; i < length; i++) {
        unsigned char c = (unsigned char)start[i];   /* cast: char may be signed */
        if (c < 0x20 || c == 0x7F) {
            return 0;
        }
    }
    return copy_text(out, outSize, start, length);
```

**Cost.** O(length of the text) for each field; the duplicate check is a linear search, O(n) (see 4.3).

**Details worth knowing.**
- *Reject, never truncate.* `copy_text` uses `snprintf` and returns failure if the text does not fit. A
  50-byte name is rejected, not silently cut to 49.
- *Phone rule:* 7 to 14 characters **including** an optional leading `+`, then digits only. I read the spec
  this way because `PHONE_LEN` is 15 (14 characters + the `'\0'`). I filed this as a proposed spec change.
- *Trimming* removes only the space character, not tabs. A tab inside a name is a control character and is
  rejected.
- *Bytes above 0x7F* (UTF-8 letters such as accented names) are allowed.
- *The id upper limit* 2,147,483,647 is automatic because id is an `int`. Only `id < 1` is checked.
- *Duplicates are checked within the same type.* A donor and a patient may both have id 7.

**Why the blood group is not editable.** `records_update_donor(store, id, name, city, phone)` simply has no
blood-group parameter. The spec says so, and it makes sense: the graph and matching results depend on blood
group. Changing it silently would change every compatibility result, and in real life a wrong blood group is
dangerous. To "change" it you would register a new donor.

**Why this approach / alternative.** Validating in one place means every entry point (API, demo data)
gets the same rules. Alternative: validate in JavaScript only. That is not safe, because anyone can send a
request with `curl` and skip the browser (see tricky question 2 in section 11).

### 4.3 Finding records: linear search

**Idea.** `donor_index` walks the array from index 0 to `count - 1` and returns the first index whose `id`
matches (or -1). `records_find_donor` wraps it and returns a pointer to that element.

**Worked example.** Donors have ids `[101, 102, 103]`. Searching 102 checks index 0 (no), index 1 (yes):
return `&items[1]`. Searching 999 checks all three and returns `NULL`.

**Pseudocode.**

```
for i from 0 to count-1:
    if items[i].id == id: return i
return -1
```

**Cost.** O(n) time, O(1) memory. Adding n donors one by one does about n(n-1)/2 comparisons for the
duplicate check: for 200 donors that is about 19,900 comparisons, which is instant.
**Alternative:** a hash table (O(1) average) or a sorted array with binary search (O(log n)). I chose the
linear search because it is the simplest code for a beginner to read and the data is small. This is a
deliberate trade-off, and I would say so in the viva.

### 4.4 Borrowed pointers

`records_find_donor` returns `const Donor *` pointing **inside** the array. It is *borrowed*: you may read
it, you may not free it, and it becomes invalid the moment a later `records_add_*` triggers a `realloc`,
because `realloc` may move the whole array to a new address. Pointers into the old block then point at freed
memory (a "dangling pointer"). Rule: use a found pointer immediately; never keep it across an add.

In `api_post_donor` I add the donor and then call `records_find_donor` **again** to build the reply. That is
safe, because nothing was added between the lookup and its use:

```c
    stored = records_find_donor(&app->store, id);
    if (stored == NULL) {
        http_send_error(res, STATUS_ERR_INTERNAL, NULL);   /* cannot happen, but never crash */
        goto cleanup;
    }
    respond_with_donor(res, 201, stored);
```

`records_update_donor` and `records_set_donor_availability` never move the array, so pointers survive them.

### 4.5 Status code mapping

**Idea.** One table is the single source of truth. A handler returns a `Status`; `http_send_error` asks the
table for the HTTP number, the code string and the message and builds the JSON error.

**Worked example.** `STATUS_ERR_DUPLICATE_ID` -> code `"DUPLICATE_ID"`, HTTP 409. Reply:
`{"ok":false,"error":{"code":"DUPLICATE_ID","message":"A record with this id already exists"}}`

**Pseudocode.**

```
http_send_error(res, status, detail):
    res.status = status_http_code(status)
    clear the body
    write {"ok":false,"error":{"code": <status_code_string>, "message": <detail or status_message>}}
```

**Real code:**

```c
    res->status = status_http_code(st);
    res->contentType = JSON_TYPE;
    sb_clear(&res->body);                    /* drop any half-written success output */
    sb_append(&res->body, "{\"ok\":false,\"error\":{\"code\":");
    sb_append_json_string(&res->body, status_code_string(st));
    sb_append(&res->body, ",\"message\":");
    sb_append_json_string(&res->body, detail != NULL ? detail : status_message(st));
```

`sb_clear` first means a half-written success reply is thrown away, so an error is never glued to the end of
a success. `http_end_ok` also checks `body.failed` (out of memory while building) and then replaces the
reply with a 500.

**Cost.** The table has 14 rows; a lookup is a loop of at most 14 steps. **Alternative:** index the array by
the enum value (faster, but breaks silently if the enum is re-ordered).

The full table (spec section I): `OK` 200; `OUT_OF_MEMORY` 500; `INVALID_ID`, `INVALID_NAME`,
`INVALID_BLOOD_GROUP`, `INVALID_CITY`, `INVALID_PHONE`, `INVALID_AVAILABILITY`, `BAD_REQUEST` 400;
`DUPLICATE_ID` 409; `NOT_FOUND` 404; `METHOD_NOT_ALLOWED` 405; `PAYLOAD_TOO_LARGE` 413;
`INTERNAL_ERROR` 500.

### 4.6 The HTTP server (for a complete beginner)

**What is a socket?** A socket is like a telephone line between two programs. Your browser and my server
are two programs. The browser "calls" a number made of an address (127.0.0.1 = "this computer") and a port
(8080 = which "extension" on this computer). My server is the one that waits for the phone to ring.

**What is Winsock?** Windows' built-in library for sockets (`winsock2.h`, linked with `-lws2_32`). It
ships with Windows, so no download is needed. It appears **only** in `http_server.c`.

**The call sequence in `server_run`:**

| Step | Call | In plain English |
|---|---|---|
| 1 | `WSAStartup` | Switch Winsock on (Windows requires this once) |
| 2 | `socket` | Get a new phone |
| 3 | `bind` to `htonl(INADDR_LOOPBACK)` | Give it the number 127.0.0.1:port. Only this computer can call |
| 4 | `listen` | Tell Windows to queue incoming calls (up to 5) |
| 5 | `select` (1 s timeout) | Wait up to a second for a call |
| 6 | `accept` | Pick up: you now have a new socket for this one client |
| 7 | `recv` | Listen to what the client says |
| 8 | `send` | Speak the answer |
| 9 | `shutdown` + `closesocket` | Hang up the client socket |
| 10 | `closesocket(listener)`, `WSACleanup` | Unplug the phone, switch Winsock off |

`htonl` / `htons` convert a number to "network byte order" (the order the internet agreed on), because
different computers store bytes in different orders.

**A real HTTP exchange.** HTTP is plain text. When you press Enter on `http://localhost:8080/api/stats`,
the browser sends text like this (each line ends with the two characters `\r\n`, and a blank line ends the
headers):

```
GET /api/stats HTTP/1.1
Host: localhost:8080
Accept: */*

```

My server answers with a status line, headers, a blank line, then the body. Here is the **real
format** my server produces. The first three headers below were copied from a real run of my server
(`GET /api/donors`, Linux test build, 1 demo donor, 142-byte body). For `/api/stats` I composed the body
from the spec's field names and the team's measured numbers for the ORIGINAL sample data (12 donors, 11
available, 8 patients, 29 edges, maximum matching 7, 1 unmatched patient). The body bytes are produced by
Dev 4's handler. **VERIFY:** run `curl.exe -i http://localhost:8080/api/stats` to see the true bytes.

```
HTTP/1.1 200 OK
Content-Type: application/json; charset=utf-8
Content-Length: 161
Cache-Control: no-store
Connection: close

{"ok":true,"data":{"donorCount":12,"availableDonorCount":11,"unavailableDonorCount":1,"patientCount":8,"edgeCount":29,"maxMatching":7,"unmatchedPatientCount":1}}
```

`Content-Length: 161` is the number of body bytes, so the browser knows where the body ends.
`Connection: close` means "I will hang up after this answer"; `Cache-Control: no-store` stops the browser
showing an old answer. (161 = the length of that exact body, computed by me; it is only correct if Dev 4's
real output has exactly those bytes.)

**Reading a request: `receive_request`.**

1. **Read until the blank line.** The headers end at `\r\n\r\n`. TCP may deliver the request in pieces
   (this is called a *partial recv*), so I loop: `recv` into a 16 KB buffer, then `find_header_end` looks
   for `\r\n\r\n`. If 16,384 bytes arrive with no blank line, I reject with 400 ("Request headers are too
   large").
2. **Parse the request line** `GET /api/search?patientId=5 HTTP/1.1` with `parse_request_line`: method
   (capital letters only, max 7), path (max 255), query (max 511), version must start with `HTTP/1.`.
   Over-long parts are rejected, not cut. Every target byte must be visible ASCII.
3. **Read the headers I care about:** `Content-Length` and `Transfer-Encoding`. All others are ignored
   (including `Host`).
4. **Read the body.** The body is `Content-Length` bytes. Some may have already arrived with the headers.
   The rest needs a loop, because one `recv` can return fewer bytes than asked.

**Worked example of the body loop.** `Content-Length: 50`. First `recv` of the headers also happened to
include 20 body bytes. `alreadyHave = 20`, `filled = 20`. Loop: `recv` returns 18 -> `filled = 38`; `recv`
returns 12 -> `filled = 50`; stop. If `recv` ever returns 0 or an error before 50, the answer is 400
"Request body was incomplete".

**Real code** (the body loop):

```c
    while (filled < contentLength) {         /* keep reading until the whole body has arrived */
        int got = recv(sock, body + filled, (int)(contentLength - filled), 0);

        if (got <= 0) {
            free(body);
            *detail = "Request body was incomplete";
            return STATUS_ERR_BAD_REQUEST;
        }
        filled += (size_t)got;
    }
    body[contentLength] = '\0';
```

**The limits:**
- **65,536-byte body limit.** If `Content-Length` is bigger, I reply **413 PAYLOAD_TOO_LARGE** *without
  reading the body*. Why: a body is held in memory, and a rude client could otherwise make us allocate
  gigabytes. Tested on my Linux build: exactly 65,536 bytes -> 201, 65,537 -> 413.
- **Chunked bodies -> 400.** "Chunked" means the sender does not say the length up front; it sends
  pieces, each with its own size line. Supporting that needs more code (parsing chunk sizes and
  trailers). The spec says no chunked, so any `Transfer-Encoding` header gives 400. Browsers send
  `Content-Length` for a normal `fetch` with a JSON string, so this is fine for our frontend.
- **5-second timeout.** `handle_client` sets `SO_RCVTIMEO` and `SO_SNDTIMEO` to 5000 ms. If a client stops
  talking, `recv` returns an error after 5 s instead of waiting forever. Important: the 5 s applies to **each
  `recv` call**, not to the whole request.
- **A client that connects and sends nothing** (browsers sometimes do this to "warm up") gets no reply and
  is closed quietly (`gotAnyData == 0`).

**One client at a time (honest limitation).** `server_run` calls `handle_client` and waits for it to finish
before calling `accept` again. That is simple and has no threading bugs. The consequence: while the server
is busy with one request, other clients wait. If a client opens a connection and goes silent, everybody
waits up to 5 seconds. For a classroom demo with a few users this is fine; for a real service it is not.

**Ctrl+C and `select`.**

```c
    while (!g_stopRequested) {
        ...
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        ready = select(0, &readSet, NULL, NULL, &timeout);   /* first argument is ignored on Windows */
```

A plain `accept()` blocks forever and would ignore Ctrl+C. So I use `select`, which waits *at most one
second* for a connection and then returns. The loop re-checks the flag
`static volatile sig_atomic_t g_stopRequested`. `signal(SIGINT, on_interrupt)` makes Windows call
`on_interrupt` when you press Ctrl+C. That function only sets the flag to 1 (a signal handler may only do
very simple things). Within one second the loop ends, `cleanup:` closes the listening socket, calls
`WSACleanup`, restores the old Ctrl+C handler and returns `STATUS_OK`. Then `main()` frees memory and
prints the shutdown message. `volatile` tells the compiler "another piece of code may change this
variable, always re-read it". `sig_atomic_t` is a type that is safe to write from a signal handler.

**Static file serving.** For any GET that does not start with `/api/`, `serve_static` serves a file from
`./frontend` (so `/` becomes `/index.html`). Only these types are allowed: `html css js svg png ico json`
(table `MIME_TYPES`); anything else is 404.

**Path-traversal protection.** A "path traversal" attack asks for `/../../secret.txt` to escape the
frontend folder. My check:

```c
    if (path[0] != '/' || strstr(path, "..") != NULL) {
        return 0;
    }
    for (p = path; *p != '\0'; p++) {
        char c = *p;
        int allowed = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                      (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-' || c == '/';
        if (!allowed) {
            return 0;
        }
    }
```

So `..` is never accepted, and a backslash or colon (`C:`) cannot appear. The path is **not**
percent-decoded, so `%2e%2e` contains a `%`, which is not an allowed character: rejected. On my Linux test
build, `/../server.log`, `/%2e%2e/server.log` and `/a/../../x.html` all returned 404.

**Percent-decoding (`http_query_get`).** URLs cannot contain spaces or special characters directly. They
are written as `%XX` (two hex digits) and `+` means a space. `url_decode` reverses that.

Worked example: query `city=New+Delhi&patientId=5`. Split at `&` into `city=New+Delhi` and `patientId=5`.
Key `city` matches. Value `New+Delhi` -> `New Delhi`. Another: `Pu%6Ee` -> `%6E` is hex 6E = `n`, giving
`Pune`. It returns 400 `BAD_REQUEST` for `Pu%6` (incomplete), `Pu%zz` (not hex), `%00` (would end the
string early), or a value that does not fit the caller's buffer; `NOT_FOUND` if the key is absent.
I tested this with 21 cases on my Linux harness (all passed).

**Cost.** Reading a request is O(bytes received). Memory per request: the 16 KB header buffer on the stack,
a `malloc(Content-Length + 1)` body, and the response `StrBuf`.
**Alternatives:** threads (one per client, needs locking for the shared store), or a non-blocking
event loop; both are more complex. A ready-made server library was forbidden by the spec.

### 4.7 The router

**Idea.** A table lists `{method, pattern, handler}`. For each request, compare the path with each pattern
one *segment* (text between slashes) at a time. A `{id}` segment matches "digits only, 1 to 2,147,483,647".

**Worked example.** Request `PUT /api/donors/102/availability`.
Row `/api/donors/{id}`: segments `api`=`api`, `donors`=`donors`, `{id}` vs `102` ok, then the pattern has
ended but the path still has `availability` -> no match. Row `/api/donors/{id}/availability`: all four
segments match, id = 102, and method `PUT` equals `PUT` -> set `req->pathId = 102` and call
`api_put_donor_availability`.

**The three failure answers:**

| Request | What the router finds | Answer |
|---|---|---|
| `GET /api/nothing` | no pattern fits | **404** `NOT_FOUND` "Unknown API path" |
| `DELETE /api/donors/101` | path fits `/api/donors/{id}`, but only GET and PUT exist | **405** `METHOD_NOT_ALLOWED` |
| `GET /api/donors/abc` (also `0`, `-5`, `99999999999`) | shape fits, but `{id}` is not a valid id | **400** `INVALID_ID` |

Two small decisions: `GET /api/donors/` (empty id) gives 404, not 400; and if the path is right but the
method is wrong *and* the id is bad, I answer 405 (the method check comes first).

**Pseudocode.**

```
for each route:
    result = match_path(route.pattern, request.path)    // NO_MATCH / MATCH / BAD_ID
    if NO_MATCH: continue
    pathKnown = true
    if route.method != request.method: continue
    if BAD_ID: answer 400 INVALID_ID; stop
    request.pathId = id; call route.handler; stop
if no route fired: 405 if pathKnown else 404
```

**Real code** (parsing the id without overflow):

```c
        digit = s[i] - '0';
        /* Test BEFORE multiplying: value * 10 + digit must stay <= INT_MAX.
         * (On Windows a long is only 32 bits, so we cannot "check afterwards".) */
        if (value > (INT_MAX - digit) / 10) {
            return 0;
        }
        value = value * 10 + digit;
```

I found and fixed a bug here during my own review: my first version used a `long` and checked *after*
multiplying. On Windows `long` is only 32 bits, so the check itself could overflow. Now it checks before.

**Cost.** 14 routes x a few segment comparisons: negligible. **Alternative:** a big `if/else` chain
(harder to read and to extend), or a tree/trie of path segments (overkill for 14 routes).

### 4.8 Startup and shutdown order (`main.c`, `app.c`)

```
1. read options (--port N, --demo)              nothing allocated yet -> early return is safe
2. app_init(&app)                                records_init: malloc 2 arrays of 4
3. if --demo: sample_data_load(&app.store)       Dev 4's function, adds records via records_add_*
4. print "Smart Blood Donor Finder is running at http://localhost:PORT/" and "Press Ctrl+C to stop."
5. server_run(&app, port)                        blocks until Ctrl+C
6. app_free(&app)                                always runs, even after an error
7. print "Server stopped. All memory released."
```

Rules I followed: `main()` owns the `AppState` (it lives on `main`'s stack) so `main()` frees it. Step 6
runs even if the server or demo load failed. A bad option (for example `--port abc`) prints usage and
returns 1 before anything is allocated.

### 4.9 Build and run

The full build (from the spec, and what `make` runs):

```
gcc -std=c11 -Wall -Wextra -pedantic -g -o sbdf.exe backend/*.c -lws2_32
```

| Piece | Meaning |
|---|---|
| `gcc` | the C compiler (from MSYS2 UCRT64) |
| `-std=c11` | use the 2011 C standard, so we do not use non-standard shortcuts by accident |
| `-Wall -Wextra` | turn on most warnings (unused variables, signed/unsigned mix-ups, ...). Spec: zero warnings |
| `-pedantic` | warn about anything that is not strictly standard C |
| `-g` | add debugging information (so a debugger shows names and line numbers) |
| `-o sbdf.exe` | name of the program to create |
| `backend/*.c` | compile every `.c` file in `backend/` and join them (link) into one program |
| `-lws2_32` | link the Windows sockets library (the "l" is "library", `ws2_32` is Winsock 2) |

Run from the project root: `sbdf.exe [--port N] [--demo]`. It must run from the project root because
static files are read from `./frontend` relative to the **current directory**.

`Makefile` targets: `all` (the command above), `debug` (adds `-O0 -DSBDF_DEBUG`, builds `sbdf_debug.exe`),
`test` (builds and runs `tests/test_records.exe`), `clean` (`rm -f` the built files). Recipe lines in a
Makefile **must** start with a TAB character, not spaces; that is the most common Makefile mistake.

**start.bat and share.bat** were written by the orchestrator, not by me, and I have not read them.
From the project notes: `start.bat` builds, starts the server and opens the browser; `share.bat` does the
same and also opens a temporary Cloudflare tunnel so others can try the app while the PC is on.
**VERIFY:** open both files and check that the build command inside is the one above.

**Why the page must be opened from `http://localhost:8080` and not by double-clicking `index.html`.**
`script.js` calls relative addresses such as `/api/stats`. A relative address is completed with whatever
the page's own address is. If you double-click the file, the page's address is
`file:///C:/.../index.html`, so the browser tries `file:///api/stats`, which does not exist, and no C code
is involved at all. From `http://localhost:8080/` the same relative address becomes
`http://localhost:8080/api/stats`, which reaches my server. My server also serves `index.html` itself,
so the page and the API share one address ("same origin").

**Why the Cloudflare tunnel needs no code change.** The tunnel program runs on the same PC and forwards
requests from a public web address to `localhost:8080`. The page is still loaded from my server and still
calls relative addresses, so everything stays same-origin. My server binds to `127.0.0.1`, which is fine
because the tunnel program connects from the same machine. My server also never looks at the `Host`
header. **Honest notes:** (1) the tunnel is **untested** by me; (2) if a proxy ever re-sent a request with
a chunked body, my server would reject it with 400. A normal browser `fetch` with a JSON string sends
`Content-Length`, so I do not expect this, but I have not verified it through the tunnel.

### 4.10 The Winsock dependency question

Winsock is **not** a third-party library: it is part of Windows and needs no download. But it is also
**not standard C**: the C language has no networking at all, and `winsock2.h` only works on Windows.
The spec forbids external C libraries, databases, Python, Node, etc., so a web server written from
scratch on sockets was the way to meet "a C backend that a browser can talk to".

Alternatives that I considered: (a) POSIX sockets (`sys/socket.h`): the same idea, but does not exist on
plain Windows; (b) a small server library such as Mongoose or civetweb: forbidden by the "no external
libraries" rule; (c) no server at all, a console-only program: would lose the web page; (d) a Python or
Node server in front of C: forbidden. Pasting the usual Winsock calls into one file (`http_server.c`)
keeps the dependency isolated: the rest of the project is portable C.
**The professor's approval of using Winsock was still pending when this was written.** Do not say it was
approved. **VERIFY** the current status before the presentation.

## 5. Memory and pointers in my part

**Every allocation in my files:**

| Where | Call | Owner and who frees it |
|---|---|---|
| `records_init` | `malloc(4 * sizeof(Donor))` and `malloc(4 * sizeof(Patient))` | the `RecordStore`; `records_free` (via `app_free`, called by `main`) |
| `records_add_donor/patient` | `realloc(items, newCapacity * sizeof(...))` through temp pointer `bigger` | same: the store |
| `receive_request` | `malloc(contentLength + 1)` for the body | `handle_client` frees it after the handler returns |
| `HttpResponse.body` | a `StrBuf` (Dev 2's type) that grows with `realloc` inside `sb_*` | `handle_client` calls `sb_free` after sending |
| `api_*` handlers | `JsonObject` filled by `json_parse_object` | the handler itself: `json_object_free` in its single `cleanup:` block |

Everything else is on the stack: the 16 KB `head` buffer, `char name[256]`, and so on. Stack memory
disappears automatically when the function returns.

**If an allocation fails.**
- `records_init`: if the second `malloc` fails, I `free` the first and zero the store, then return
  `STATUS_ERR_NOMEM`. No leak.
- `records_add_*`: a failed `realloc` leaves the store unchanged and returns `STATUS_ERR_NOMEM` (HTTP 500).
  The server keeps running.
- `receive_request`: `malloc` failure returns `STATUS_ERR_NOMEM` and the client gets a 500.
- Response building: if the `StrBuf` fails, `http_end_ok` replaces the reply with a 500 error; if even that
  fails, `send_response` sends a hard-coded `OUT_OF_MEMORY` message from a static string.
- **NOT TESTED:** I never forced an allocation to fail. These paths are reviewed by reading only.

**Avoiding double free and leaks.**
- `records_free` frees both arrays, then sets `items = NULL`, `count = 0`, `capacity = 0`. Freeing `NULL`
  is legal and does nothing, so calling `records_free` twice is safe. My test `test_empty_store` does
  exactly that (twice on one store, once on a never-initialised zeroed store, and once with `NULL`).
- One exit point per handler: every `api_*` handler that parses JSON ends with a single `cleanup:` label
  that calls `json_object_free`. Every error path uses `goto cleanup;`, so no early `return` can skip the
  free.
- `handle_client` has one place that does `free(body); sb_free(&res.body); close_client(client);`.
- On my Linux harness build, I ran the server with AddressSanitizer + UBSan + LeakSanitizer through dozens of
  requests and a Ctrl+C shutdown: no errors and no leak report. (That is a Linux build with a shim, not the
  Windows build; see section 8.)

**A common beginner mistake (and how I avoided it):** writing `p = realloc(p, n);`. See 4.1: if it fails,
the old block leaks and `p` becomes `NULL`. I always use a temporary pointer. A second classic mistake:
keeping a pointer from `records_find_donor` and using it after adding more records (dangling pointer, see
4.4).

## 6. Validation and error handling in my part

| What is rejected | Where | Status | Error code | HTTP |
|---|---|---|---|---|
| id < 1, missing id, quoted id (`"id":"7"`), id too big | `records_*`, `router.c` | `STATUS_ERR_INVALID_ID` | `INVALID_ID` | 400 |
| name empty / > 49 bytes / control character | `validate_text` | `..._INVALID_NAME` | `INVALID_NAME` | 400 |
| blood group not one of the 8, empty, `NULL` | `normalize_group` | `..._INVALID_BLOOD_GROUP` | `INVALID_BLOOD_GROUP` | 400 |
| city empty / > 29 bytes / control character | `validate_text` | `..._INVALID_CITY` | `INVALID_CITY` | 400 |
| phone not 7-14 chars, or not `+` then digits | `validate_phone` | `..._INVALID_PHONE` | `INVALID_PHONE` | 400 |
| available not 0/1 (JSON `true`/`false`; `"yes"` rejected) | `records_*` | `..._INVALID_AVAILABILITY` | `INVALID_AVAILABILITY` | 400 |
| id already used (same record type) | `records_add_*` | `..._DUPLICATE_ID` | `DUPLICATE_ID` | 409 |
| donor/patient id does not exist | `api_get_*`, `records_update/set` | `..._NOT_FOUND` | `NOT_FOUND` | 404 |
| unknown `/api/...` path | `router.c` | `..._NOT_FOUND` | `NOT_FOUND` | 404 |
| known path, wrong method | `router.c` | `..._METHOD_NOT_ALLOWED` | `METHOD_NOT_ALLOWED` | 405 |
| empty, blank or invalid JSON body; nested JSON | `parse_body` | `..._BAD_REQUEST` | `BAD_REQUEST` | 400 |
| malformed request line; headers > 16 KB; chunked; bad Content-Length; truncated body | `receive_request` | `..._BAD_REQUEST` | `BAD_REQUEST` | 400 |
| body > 65,536 bytes | `receive_request` | `..._PAYLOAD_TOO_LARGE` | `PAYLOAD_TOO_LARGE` | 413 |
| out of memory | many | `..._NOMEM` | `OUT_OF_MEMORY` | 500 |
| non-GET to a non-API path (e.g. `POST /index.html`) | `dispatch_request` | `..._METHOD_NOT_ALLOWED` | `METHOD_NOT_ALLOWED` | 405 |
| missing static file or disallowed type | `serve_static` | `..._NOT_FOUND` | `NOT_FOUND` | 404 |

**The "invalid sentinel" technique.** If a JSON field is missing or has the wrong type, the handler does
not have its own error code path. It passes an *invalid* value into `records_*` (id `0`, an empty string,
available `-1`), and the normal validation produces the matching `INVALID_*` error. Example from the
team's own check: `POST /api/donors` with only an id returns 400 `INVALID_NAME` (the name is missing, so it
becomes `""`). Result: one set of rules, no duplicated checks.

**Atomic updates.** `records_update_donor` validates name, city and phone into temporary buffers *before*
touching the donor, so a bad phone cannot leave a donor with a new name and an old phone.

## 7. How I coordinated with the other developers

**How the team worked.** Six Claude accounts: one Orchestrator and five Developers. Developers never
spoke to each other; the human team member carried messages. All coordination went through one binding
written specification with frozen headers, JSON formats, business rules, the status table and ownership
rules. We could not edit headers, only file a "PROPOSED SPEC CHANGE".

**I was the critical path.** Everyone needs `common.h` (status codes), `records.h` (the structs) and
`http_server.h` (request/response types). My code was also the thing the others' handlers plug into.

**Contracts I relied on** (from the spec, not from their code):
- Dev 2's `compat_normalize_blood_group(input, out[BG_LEN])` must trim, upper-case, validate, and
  NUL-terminate within 5 bytes. I call it with text up to 15 characters from the API.
- Dev 2's `json_get_string` must fail for over-long values, and `json_get_int` must fail for quoted
  numbers; my handlers depend on both.
- Dev 2's `StrBuf` functions (`sb_*`).
- Dev 4's `sample_data_load` must only use `records_add_*`, so demo data obeys my validation.
- The frozen function prototypes in `api.h`.

**What others rely on from me:** `http_begin_ok`, `http_end_ok`, `http_send_error`, `http_query_get`,
`donor_write_json`, `patient_write_json`, `records_find_*`, direct read-only access to
`store->donors.items[i]` and `.count`, `status_code_string` / `status_http_code`, and the router calling
their `api_*` functions with `req->pathId` set (or -1).

**Problems that came up, and how they were resolved:**
1. **Headers were never delivered as files.** The developers (including me) delivered `.c` files against
   the frozen Section F text, but nobody wrote the `.h` files. The orchestrator generated all 11 headers
   straight from Section F and syntax-checked them with gcc. For my own tests I had made copies of the
   headers by hand (comments trimmed), which I used only in my sandbox, never delivered.
   *What I would do differently:* include a header-generation step in the spec, or ask for the headers
   explicitly in the task.
2. **VS Code showed false errors** ("Patient and RecordStore undefined") although gcc compiled fine. The
   editor did not know about the `backend/` include folder. Fixed with `.vscode/c_cpp_properties.json`
   (`includePath = backend`). Lesson: gcc is the judge, not the editor squiggles.
3. **The page did not work when double-clicked** (`file://`). See 4.9.
4. **Developer 3's task arrived without the specification** (a paste problem). That was not my task, but it
   shows why one attached file is safer than a pasted message.
5. **Spec gaps I reported** as PROPOSED SPEC CHANGE: (a) say that the phone length 7-14 includes the `+`;
   (b) define the check order for `records_update_donor` and `records_set_donor_availability` (I chose
   id, then field checks, then not found); (c) add `http_route_request` to the allowed prefixes or add
   `router.h` to the frozen headers. **VERIFY:** whether the orchestrator accepted these.

**What I would do differently overall:** ask the orchestrator for a tiny shared integration test (one
script that starts the server and checks every endpoint), and keep a short "decisions log" so the others
know about choices like "unknown path -> 404, empty id -> 404".

## 8. Testing

### 8.1 What I wrote

`tests/test_records.c` (528 lines). It has its own `main`, prints `PASS:` / `FAIL:` for each check and
returns 0 only if all pass. It links only `records.c` and `common.c`; it brings tiny **stubs** (fake
versions) of `compat_normalize_blood_group` and the `sb_*` functions so it does not depend on Dev 2's code.
It covers: an empty store; zeroed and double-freed stores; adding a donor with padded input; 100 donors and
100 patients (forces 5 reallocs each, then re-checks every record); duplicate donor and patient ids; the
same id for a donor and a patient; invalid blood group, availability 2 and -1, ids 0 and -5; names of 49
and 50 bytes; control characters; cities of 29 and 30 bytes; phones of 6, 7, 14, 15 characters, letters,
dashes, a lone `+`; the check order; find present and absent; update (including "a failed update changes
nothing"); availability; exact JSON for donor and patient; the status table.

### 8.2 What I actually ran

These runs were in **my own sandbox (Linux)**, not on the team's Windows machine:

| Run | Result |
|---|---|
| `gcc -std=c11 -Wall -Wextra -pedantic -g -Ibackend` on `tests/test_records.c`, `records.c`, `common.c` | 0 warnings; **114 PASS, 0 FAIL** |
| Same with `-fsanitize=address,undefined` | 114 PASS, no sanitizer output |
| MinGW cross compiler (`x86_64-w64-mingw32-gcc`) `-fsyntax-only` and `-O2 -c` on all 7 of my `.c` files | no diagnostics |
| MinGW link of my objects + throw-away stubs with `-lws2_32` | linked into a Windows `.exe` (not run) |
| Linux end-to-end harness: my real server, router, handlers and records, with a throw-away shim for Winsock and fake Dev 2/3/4 modules, driven by `curl` and raw sockets, under ASan/UBSan/LeakSanitizer | all my endpoints and about 40 edge cases behaved as designed (below) |

Edge cases in the harness: all 8 endpoints with success and error paths; id `abc`, `0`, `-5`,
`99999999999`; 404/405; static files and path traversal; chunked (400); bad Content-Length (400);
65,536 vs 65,537 bytes (201 vs 413); truncated body; body split over two packets; a 60 KB body; idle and
vanishing clients; 21 `http_query_get` cases; Ctrl+C (SIGINT) shutdown.

The **team** reported (not run by me): the full Windows build finished with no warnings or errors, and
`sbdf.exe --demo` started. With the ORIGINAL sample data their curl checks returned: `/api/stats` = 12
donors, 11 available, 8 patients, 29 edges, maximum matching 7, 1 unmatched patient; matrix 8 x 12 with row
and column sums both 29; `POST /api/matching` = 7 pairs; unknown patient -> 404 `NOT_FOUND`; unknown
`/api` path -> 404; POST donor with only an id -> 400 `INVALID_NAME`.

### 8.3 NOT verified

- **NOT RUN by me:** the real full build (`backend/*.c`) and `sbdf.exe` on Windows; `make` on MSYS2 (I only
  did a dry run `make -n` on Linux); `curl.exe` against the real app; the real `json_util.c` and
  `compatibility.c` (my harness used my own fake versions).
- **The 5-second timeouts were never exercised** (my Linux shim did not support the Windows socket option).
- **Real Windows Ctrl+C**: only the Linux SIGINT path was run.
- **Out-of-memory paths**: never forced.
- AddressSanitizer on Windows; browser click-through; `share.bat` and the tunnel; the
  per-file results of every other `tests/test_*.c`.

### 8.4 Five-step LIVE DEMO script

Use **Command Prompt** or **PowerShell** in the project folder. Use `curl.exe` (with `.exe`) because in
PowerShell `curl` is an alias for a different command. JSON quoting is awkward on Windows, so for the POST
create a small file first.

*Which sample data?* If the team's current `sample_data.c` is the enlarged one, expect the "enlarged"
numbers (these are **expected**, not measured). **VERIFY:** which version is in the repo before you present.
Ids 9001 and up are unlikely to clash with demo data (**VERIFY** the highest id in `sample_data.c`).

**Step 1: start the server.** `sbdf.exe --demo`
Expect:
```
Demo data loaded: <N> donors, <M> patients.
Smart Blood Donor Finder is running at http://localhost:8080/
Press Ctrl+C to stop.
```
(`N`, `M` = 12 and 8 with the original data; 200 and 120 expected with the enlarged data.)
*Status: the message format is from my `main.c`; I ran it on Linux with a stub that loads 1 donor. I did
not run the Windows exe.*

**Step 2: read data.** Second window: `curl.exe -i http://localhost:8080/api/stats`
Expect `HTTP/1.1 200 OK`, the five headers shown in 4.6, and JSON. Original data: `donorCount` 12,
`availableDonorCount` 11, `patientCount` 8, `edgeCount` 29, `maxMatching` 7, `unmatchedPatientCount` 1
(**measured by the team**). Enlarged data: 200, 178, 120, 10,339, 120, 0 (**expected**, not yet measured).
Then `curl.exe -i http://localhost:8080/api/donors` shows the `Content-Length` and the donor list.
*My run: `GET /api/donors` on the Linux harness returned 200 with the headers in 4.6.*

**Step 3: add a donor, then add it again.** Create `donor.json` containing:
`{"id":9001,"name":"Demo Donor","bloodGroup":"o+","city":"Pune","phone":"9000009001","available":true}`
Run: `curl.exe -i -X POST http://localhost:8080/api/donors -d @donor.json`
Expect **201** and `"bloodGroup":"O+"` (trimmed and upper-cased by the server). Run the **same command
again**. Expect **409** with
`{"ok":false,"error":{"code":"DUPLICATE_ID","message":"A record with this id already exists"}}`.
*My run (Linux harness, real `records.c`/handlers): 201 then 409, exactly these messages.*

**Step 4: show validation and 404.** Change the blood group in the file to `"X+"` and use id 9002; POST
again. Expect **400** `INVALID_BLOOD_GROUP`. Then `curl.exe -i http://localhost:8080/api/nothing`.
Expect **404** `NOT_FOUND`, message "Unknown API path".
*My run (Linux harness): 400 `INVALID_BLOOD_GROUP` and 404 as above.*

**Step 5: shut down cleanly.** In the server window press **Ctrl+C**. Within about a second expect:
```
Server stopped. All memory released.
```
*My run: SIGINT on the Linux harness printed this line. **Real Windows Ctrl+C: NOT RUN.***
The server window prints one log line per request, such as `POST /api/donors -> 409` (from `handle_client`),
which is handy to point at while you demo.

## 9. Limitations and known risks

1. **One client at a time.** A silent client can block everyone for up to 5 s; a client that sends one byte
   every 4 s could hold the server far longer, because the timeout is per `recv`, not per request.
2. **No authentication or encryption.** Binding to `127.0.0.1` protects us on a normal run, but with
   `share.bat` the app is reachable from the internet through the tunnel, and anyone with the link could add
   or change records. The data is fictional and in memory only, but say so honestly.
3. **No chunked requests, no `Expect: 100-continue`, no HEAD or OPTIONS, no keep-alive** (every reply says
   `Connection: close`). A command-line `curl` sending a body larger than 1 KB may pause about one second.
4. **Linear search** for finds and duplicates: O(n). Fine for hundreds of records, slow for millions.
5. **Memory only.** Everything is lost when the server stops.
6. **Not tested on the real target by me**: see 8.3. The out-of-memory paths and the timeouts were never run.
7. **`http_server.c` is long (873 lines)** for a beginner to review. It is one file because only that file
   may include Winsock.
8. **Wording of messages** in `common.c` is mine; the frontend should rely on the `code`, not the message.
9. **Windows device names** (for example `con.html`) could confuse `fopen`; I mitigate this by measuring
   the file with `fseek`/`ftell` first, but I did **not** test it. **VERIFY** if you want to claim it.
10. **Winsock** is Windows-only, and the professor's approval was pending.

## 10. THE PRESENTATION SCRIPT

### 10.1 Four-minute version (about 615 words, cues excluded)

> Hello. I am Developer 1. My part is the foundation: where the data lives, the web server, and how the
> program starts and stops. Let me tell you what happens when you type a web address and press Enter.
>
> **[SHOW: the ASCII diagram from section 2]**
>
> First, the program has to exist. In `main.c` I read the options, then call `app_init`, which gives us an
> empty store: two arrays with room for four donors and four patients. If you start with `--demo`, my
> teammate's `sample_data_load` fills them. Then I print the address and call `server_run`.
>
> `server_run` uses Winsock, the sockets library that comes with Windows. A socket is like a telephone
> line between two programs. I switch Winsock on, create a socket, and bind it to 127.0.0.1 on port 8080.
> That address means "this computer only", so nobody else on the network can call it. Then I listen.
>
> Now you press Enter. Your browser connects and sends a few lines of plain text: the method, the path,
> and some headers, ending with a blank line. My `receive_request` function reads until it sees that blank
> line. Network data can arrive in pieces, so I use loops, not a single read. If the request has a body, I
> read exactly `Content-Length` bytes. Bodies over 65,536 bytes are refused with a 413. Chunked bodies
> are refused with a 400, because the specification does not allow them. And every read has a five-second
> timeout, so a silent client cannot hang us forever.
>
> **[SHOW: the real HTTP request and response from section 4.6]**
>
> Next, the router. It is a table of fourteen rows: method, path pattern, handler. A pattern like
> `/api/donors/{id}` matches a number in that place. Unknown path? 404. Right path, wrong method? 405.
> A bad id like "abc"? 400 INVALID_ID. If it matches, the router calls the handler, which could be mine
> or one of my teammates'.
>
> **[DEMO: curl a donor POST, then repeat it to show 409]**
>
> My handlers talk to `records.c`. Every donor is checked before it is stored: id, name, blood group,
> city, phone, availability, then duplicates, always in that order. The blood group is cleaned up by Dev
> 2's function. Records live in an array that doubles when it is full: 4, 8, 16, and so on. I grow it
> with `realloc` through a temporary pointer. If I wrote the result straight back and `realloc` failed,
> I would lose the old block: a memory leak and a crash. This way, a failure just returns an error.
>
> The reply is built as JSON. `common.c` has one table that maps each status to a code string and an HTTP
> number, so every error has the same shape. Then `send_response` writes the headers and the body, and the
> socket is closed.
>
> **[DEMO: invalid blood group gives 400; unknown path gives 404]**
>
> The same server also hands out the web page itself. Any request that does not start with `/api/` is
> treated as a file request and answered from the `frontend` folder, with a check that blocks `..` tricks
> so nobody can escape that folder. This is why you must open the page at `localhost:8080`. If you
> double-click `index.html`, the browser uses a `file` address, and the page's calls to `/api/stats` never
> reach my code.
>
> Because everyone depended on my headers and my server, I worked strictly from the shared specification.
> My teammates call my small helper functions to build their replies, and I call their functions for blood
> group checking and demo data.
>
> How do we stop? The server waits with `select` for one second at a time and checks a flag. Ctrl+C sets
> that flag, the loop ends, I close the socket, and `main` frees all memory.
>
> **[DEMO: press Ctrl+C]**
>
> You see: "Server stopped. All memory released."
>
> Honest limits: the server handles one client at a time, searches are linear, and there is no login. I
> would add threads and a hash table with more time. Thank you.

*(Word count: about 615 spoken words, measured by a simple whitespace count with the [SHOW]/[DEMO] cues
removed. VERIFY by timing yourself aloud; it should take about four minutes.)*

### 10.2 Sixty-second version (about 130 words)

> I am Developer 1. I built the foundation: the record store, the web server, and the start-up and
> shutdown. Donors and patients live in arrays that double with `realloc`, using a temporary pointer so a
> failure cannot leak memory. Every record is validated in a fixed order before it is stored. The server
> uses Windows sockets bound to 127.0.0.1. It reads the request until the blank line, then
> `Content-Length` bytes, refuses bodies over 65,536 bytes, and times out after five seconds. A router
> table sends each request to the right handler, with 404, 405 or 400 for the wrong path, method or id.
> Ctrl+C sets a flag, the loop ends, and `main` frees everything and prints "All memory released".
> Limits: one client at a time, linear search, no login.
>
> **[DEMO: POST a donor, POST it again: 409]**

## 11. VIVA Q&A

### 11.1 Questions about my part

**Q1. What do `items`, `count` and `capacity` mean?**
`items` points to the array in memory, `count` is how many records are stored, and `capacity` is how many
fit before we must grow. When `count == capacity` and we add one more, we double the capacity.

**Q2. Why double the array instead of adding one slot?**
Doubling copies the data only at 4, 8, 16, ... so total copying is about 2n records (amortised O(1) per
add). Adding one slot each time would copy everything on every add, which is O(n squared) overall.

**Q3. Why `bigger = realloc(...)` instead of `items = realloc(...)`?**
If `realloc` fails it returns `NULL` and leaves the old block alive. Writing it straight into `items`
would lose the only pointer to that block: a leak, plus an array that looks invalid. With `bigger`, we
only copy it into `items` after we know it worked.

**Q4. What does "borrowed pointer" mean?**
`records_find_donor` returns a pointer into the array, not a copy. We may read it but not free it, and it
becomes invalid after the next add, because `realloc` can move the array. So use it right away.

**Q5. What is the validation order and why does it matter?**
id, name, bloodGroup, city, phone, available, then duplicate id. The spec fixes it so everyone gets the
same error for the same bad input. For example, an input that is wrong everywhere always reports
`INVALID_ID` first.

**Q6. Why can't the blood group be edited?**
`records_update_donor` has no blood-group parameter. The graph and matching depend on compatibility, so
changing it would silently change results, and in real life a wrong blood group is dangerous.

**Q7. Is it safe to call `records_free` twice?**
Yes. It frees both arrays and then sets the pointers to `NULL` and the counts to 0. `free(NULL)` does
nothing. My test calls it twice, on a zeroed store, and with `NULL`.

**Q8. What is a socket, and which Winsock calls do you make?**
A socket is an endpoint of a network connection, like a telephone line. The order is `WSAStartup`,
`socket`, `bind`, `listen`, then in a loop `select`, `accept`, `recv`, `send`, `closesocket`, and finally
`WSACleanup`.

**Q9. Why bind to 127.0.0.1 only?**
So that only programs on the same computer can connect. It keeps a student demo from being open to the
whole network. (With the tunnel, the tunnel program runs on the same machine.)

**Q10. Why do you need loops around `recv`?**
TCP is a stream, not a set of messages. One `recv` may return fewer bytes than the request has. So I loop
until I have the blank line, and then until I have `Content-Length` body bytes.

**Q11. What happens with a body over 65,536 bytes? With a chunked body?**
Over 65,536: I answer 413 `PAYLOAD_TOO_LARGE` without reading the body. Chunked: I answer 400, because
the spec says Content-Length only, and supporting chunking needs extra parsing.

**Q12. How does Ctrl+C shut the server down cleanly?**
A signal handler sets a `volatile sig_atomic_t` flag. The main loop uses `select` with a one-second
timeout, so it checks the flag at least once a second. When it is set, the loop ends, I close the sockets,
call `WSACleanup`, and `main` calls `app_free`.

**Q13. How do you stop someone reading files outside `frontend`?**
I only allow letters, digits and `. _ - /`, and I reject any path containing `..`. The path is not
percent-decoded, so `%2e%2e` is rejected too. Only `html css js svg png ico json` files are served.

**Q14. What is the difference between 404, 405 and 400 INVALID_ID in the router?**
404: no route has that path. 405: the path exists but not for that method. 400 `INVALID_ID`: the path
has the right shape but the `{id}` is not a valid positive integer, such as `abc` or `0`.

**Q15. Why does the server handle one client at a time, and what is the cost?**
It is the simplest design and needs no locking around the shared store. The cost: other clients wait, and
a silent client can block the server up to 5 seconds per read.

**Q16. Why must the page be opened from `http://localhost:8080`?**
The JavaScript uses relative addresses like `/api/stats`. From a `file://` address those would point to
files on disk, not to my server. From `localhost:8080` they reach the C backend.

**Q17. What does each compiler flag do?**
`-std=c11` selects the C11 standard; `-Wall -Wextra -pedantic` turn on warnings (we required zero);
`-g` adds debug information; `-lws2_32` links the Windows sockets library.

### 11.2 Three tricky questions

**T1. "If `realloc` fails the program is out of memory anyway. Why bother with the temporary pointer?"**
*Trap:* agreeing that it does not matter. *Answer:* a web server should not crash or corrupt data
because one request failed. With the temporary pointer the old array is intact, the store is unchanged, the
request gets a 500, and later requests (with less memory pressure) can succeed. Without it we leak the old
block and lose all records.

**T2. "The server only listens on 127.0.0.1, so it is secure, right?"**
*Trap:* saying yes. *Answer:* only while it runs locally. It has no login and no encryption. With
`share.bat` the Cloudflare tunnel forwards the internet to my server, so anyone with the link can use the
API. We accept that because the data is fictional and in memory only, but it would not be acceptable for
real patients. I have not tested the tunnel.

**T3. "Your tests pass, so the server works. Prove it."**
*Trap:* overclaiming. *Answer:* `tests/test_records.c` tests only `records.c` and `common.c` (114 checks,
with stubs for Dev 2's code). The server was checked by the team's `curl` runs on the original sample data
(stats, matrix, matching, 404, 400) and, by me, on a Linux test build with a Winsock shim. I did not run
my own tests against the real Windows executable, the timeouts were never exercised, and I never forced an
allocation to fail.

### 11.3 Questions about OTHER modules (one-line answers, then hand over)

1. **"Why are 27 of the 64 blood-group pairs compatible?"** It is the red-cell rule table in the spec,
   stored as `COMPAT[8][8]` in `compatibility.c`. Hand over to **Dev 2** (compatibility).
2. **"How does the search choose donors?"** It keeps donors who are compatible, match the city filter, and
   (optionally) are available. Hand over to **Dev 2** (`search.c`).
3. **"How is the adjacency matrix built and what are the degrees?"** Rows are patients, columns are
   donors, a 1 means an edge; degrees are row and column sums. Hand over to **Dev 3** (`graph.c`).
4. **"How does matching find 7 pairs and what is an augmenting path?"** Kuhn's algorithm re-routes an
   earlier match to free a donor for someone else. Hand over to **Dev 4** (`matching.c`).
5. **"Where is the JSON written and where do strings get escaped?"** `sb_append_json_string` in
   `json_util.c`. Hand over to **Dev 2** (JSON helpers); for the frontend side, **Dev 5**.

## 12. Glossary

- **Socket:** one end of a network connection between two programs.
- **Port:** a number (here 8080) that says which program on a computer should receive the connection.
- **Winsock:** the Windows library (`winsock2.h`, `-lws2_32`) that provides sockets.
- **HTTP request / response:** the plain-text messages a browser and server exchange (method, path,
  headers, blank line, body).
- **Content-Length:** a header giving the exact number of body bytes.
- **Chunked encoding:** a way to send a body in pieces without stating its length first; we reject it.
- **`realloc`:** a C function that resizes a memory block, possibly moving it to a new address.
- **Dangling pointer:** a pointer to memory that has been freed or moved; using it is a bug.
- **`sig_atomic_t` / `volatile`:** the safe kind of variable a signal handler may set, and a keyword that
  forces the compiler to re-read it each time.
- **Path traversal:** an attack using `..` in a path to read files outside the allowed folder.

## 13. Cheat sheet (memorise these)

1. Start order: `app_init` -> (`sample_data_load` if `--demo`) -> `server_run` -> `app_free`.
2. Shutdown message: `Server stopped. All memory released.`
3. Address and port: `127.0.0.1`, default 8080 (`--port N`, 1-65535).
4. `RECORDS_INITIAL_CAPACITY` = 4; growth 4 -> 8 -> 16 -> ... by doubling with a temporary pointer.
5. Validation order: id, name, bloodGroup, city, phone, available, duplicate id.
6. Limits: name 1-49 bytes, city 1-29 bytes, phone 7-14 chars (optional leading `+`), id 1 to 2,147,483,647.
7. Request limits: headers 16 KB (400), body 65,536 bytes (413), chunked (400), receive timeout 5 s.
8. Status mapping: `status_code_string`, `status_message`, `status_http_code`; DUPLICATE_ID = 409,
   NOT_FOUND = 404, METHOD_NOT_ALLOWED = 405, PAYLOAD_TOO_LARGE = 413.
9. Winsock call order: `WSAStartup`, `socket`, `bind`, `listen`, `select`, `accept`, `recv`, `send`,
   `closesocket`, `WSACleanup`.
10. Build: `gcc -std=c11 -Wall -Wextra -pedantic -g -o sbdf.exe backend/*.c -lws2_32`; the page must be
    opened at `http://localhost:8080`.

## 14. Handover notes (five sentences for a teammate)

1. "Developer 1's code keeps every donor and patient in growable arrays that double with `realloc` through a
   temporary pointer, and checks each record in a fixed order before storing it."
2. "The server in `http_server.c` uses Windows sockets on 127.0.0.1:8080, reads each request until the
   blank line and then `Content-Length` bytes, serves one client at a time, and stops cleanly on Ctrl+C
   because `select` re-checks a flag every second."
3. "The router is a table of 14 rows that answers 404 for an unknown path, 405 for a wrong method and 400
   `INVALID_ID` for a bad `{id}`, and `common.c` maps every status to a code string and an HTTP number."
4. "`main` starts everything in a fixed order, supports `--port` and `--demo`, and always frees all memory
   before printing the shutdown message."
5. "The honest limits are one client at a time, linear search, no login, a Windows-only dependency that the
   professor had not yet approved, and that only `records.c` has unit tests; the rest was checked by the
   team's `curl` runs."

---

## VERIFY BEFORE PRESENTING

1. **Who calls my functions.** Section 2 lists the others' intended use of `http_begin_ok`, `http_query_get`,
   `donor_write_json` etc. I did not read their files. Run
   `grep -n "http_query_get\|http_begin_ok\|http_end_ok\|http_send_error\|donor_write_json" backend/*.c`.
2. **Real `/api/stats` bytes.** The body in section 4.6 is composed from the spec's field names and the
   team's measured numbers. The `Content-Length: 161` is only right if Dev 4's output matches exactly. Run
   `curl.exe -i http://localhost:8080/api/stats`.
3. **My files unchanged since delivery.** I did not edit them after delivering, but the orchestrator may
   have. Compare the repository copies with my delivered versions (`git log` / `git diff`), especially
   `http_server.c`, `records.c`, `router.c`, `Makefile`.
4. **Line counts** in section 0 are from `wc -l` on my delivered copies.
5. **Struct sizes** (Donor 108 bytes, Patient 92 bytes) were measured on my 64-bit Linux gcc. They should
   match on Windows x64, but I did not measure them there.
6. **Which sample data is in the repo** (original 12/8 or enlarged 200/120), the highest donor id (is 9001
   free?), and whether `/api/donors/101` exists. The enlarged numbers are *expected*, not measured.
7. **`start.bat` and `share.bat`.** I have not read them. Check that they run the build command in 4.9.
8. **Tunnel behaviour.** Untested: check that a POST through the tunnel arrives with `Content-Length` (not
   chunked).
9. **Professor's approval of Winsock.** Status unknown to me: ask the team.
10. **Whether the orchestrator accepted my three PROPOSED SPEC CHANGES** (phone length includes `+`; update
    check order; router prefix / `router.h`).
11. **Timing of the script** (about 4 minutes) and its word count: read it aloud with a timer.
12. **All demo outputs on Windows.** My demo outputs come from a Linux build with a Winsock shim and fake
    Dev 2/3/4 modules. Re-run Steps 1-5 of section 8.4 on the real `sbdf.exe` and fix any difference.
13. **Windows Ctrl+C**, the 5-second timeouts, and out-of-memory paths were never run. Do not claim them.
14. **`con.html`-style Windows device names** (limitation 9) were not tested.
15. **Other tests.** Whether the team's other `tests/test_*.c` files pass is unverified; I only know about mine.
