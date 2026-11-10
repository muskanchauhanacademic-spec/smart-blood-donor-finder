# Presentation Handbook - Developer 2
## Blood-group compatibility, donor search, and the JSON engine

**Project:** Smart Blood Donor Finder: A Graph Theory Based Blood Donor Search and Matching System Using C
**My role:** Developer 2 (Claude Account 3). I wrote the compatibility rules, the donor search, the search API handler and the JSON engine.

**How to use this file:** Read sections 1-4 once. Memorise section 13. Practise section 10 aloud. Use section 11 to prepare for the viva.

## 0. Title, role and the files I own

| File | Purpose | Approx. lines |
|---|---|---|
| `backend/json_util.c` | `StrBuf` (growing string), JSON string escaping, flat JSON object parser, `json_get_*` readers | 637 |
| `backend/compatibility.c` | 8x8 blood-group table, normalising, `compat_can_donate`, city matching, `compat_is_edge` | 155 |
| `backend/search.c` | `search_compatible_donors` (two-pass search), `search_result_free` | 97 |
| `backend/api_search.c` | `api_get_search`: handler for `GET /api/search` | 146 |
| `tests/test_json.c` | Standalone tests for the JSON engine | 440 |
| `tests/test_compat.c` | Standalone tests for compatibility (all 64 combinations) | 314 |
| `tests/test_search.c` | Standalone tests for search (own stub of `records_find_patient`) | 310 |
| `tests/test_api_search.c` | **Extra file, not requested in the spec.** Tests the handler with stubs of Developer 1's helpers | 432 |

The headers (`json_util.h`, `compatibility.h`, `search.h`) are frozen. I did not edit them. I wrote no header files.

Nothing in these files has been changed since I delivered them.

---

## 1. The 30-second summary

My part answers one question: **"Which donors may give blood to this patient?"** It does this in three layers:

1. **Compatibility:** a small 8x8 table says which donor blood group may give red blood cells to which patient group.
2. **Search:** go through all donors and return the ones that match the patient's blood group, the optional city, and (by default) are available.
3. **JSON engine:** every answer from the server is JSON text. My `StrBuf` builds that text safely, and my parser reads JSON that the browser sends.

The project needs this because the graph and the matching (Developers 3 and 4) are built on top of the same compatibility rule. Rule of the project: **JavaScript only displays; C decides.** The decision "may A+ give to O-?" is made here, in C.

---

## 2. Where my part sits

```
 Browser (Dev 5: HTML/CSS/JS)
        |  HTTP + JSON
        v
 http_server.c / router.c  (Dev 1)  --- calls --->  api_get_search()          [me]
                                                          |
        +-------------------------------------------------+------------------+
        |                         |                       |                  |
        v                         v                       v                  v
 http_query_get()      records_find_patient()     search_compatible_donors()  patient_write_json()
 http_begin_ok()       (Dev 1)                    [me]                        donor_write_json()
 http_end_ok()                                        |                       (Dev 1)
 http_send_error()                                    v
 (Dev 1)                                    compat_can_donate()  compat_city_matches()   [me]
                                                      |
                                                      v
                                              static COMPAT[8][8] table                [me]

 Every api_*.c file (all developers) also uses  StrBuf / sb_*  and  json_get_*          [me]
```

**Whom I call**
- `http_query_get`, `http_begin_ok`, `http_end_ok`, `http_send_error` (Dev 1, `http_server.h`)
- `records_find_patient`, `patient_write_json`, `donor_write_json` (Dev 1, `records.h`)
- Inside my own files: `compat_*`, `search_*`, `sb_*`

**Who calls me** (from the frozen spec; **VERIFY** with `grep -n "compat_\|sb_\|json_get_" backend/*.c`)
- `compat_is_edge`: `graph.c` (Dev 3), as the spec requires.
- `compat_normalize_blood_group`: expected in `records.c` (Dev 1) when validating a blood group.
- `sb_*`: every API handler and every `*_write_json` function.
- `json_parse_object` / `json_get_*`: `api_records.c` (Dev 1) and `api_matching.c` (Dev 4) for request bodies.
- `api_get_search`: the router (Dev 1).

---

## 3. What I built, file by file

### 3.1 `compatibility.c` - the rules

| Function | In plain English |
|---|---|
| `compat_blood_group_index(text)` | Turns `"A+"` into 0, `"O-"` into 7, anything else into -1. Ignores upper/lower case. Does **not** trim spaces. |
| `compat_blood_group_name(i)` | The opposite: 0 gives `"A+"`. Returns `NULL` if `i` is outside 0..7. |
| `compat_normalize_blood_group(in, out)` | Trims spaces, accepts any case, writes the canonical spelling (`" ab- "` becomes `"AB-"`). Returns `STATUS_ERR_INVALID_BLOOD_GROUP` if it is not one of the 8. |
| `compat_can_donate(donor, patient)` | 1 if that donor group may give to that patient group, else 0. Returns 0 if either text is invalid. |
| `compat_city_matches(donorCity, filter)` | 1 if the filter is `NULL` or empty ("any city"), or if the cities are equal ignoring case. |
| `compat_is_edge(donor, patient, filter)` | **The edge rule:** compatible blood AND donor `available == 1` AND city matches. |

Private helpers (marked `static`, invisible to other files): `lower_ascii` and `equals_ignore_case`.

### 3.2 `search.c` - the search

| Function | In plain English |
|---|---|
| `search_compatible_donors(store, patientId, cityFilter, availableOnly, out)` | Finds the patient, counts the matching donors, allocates exactly that many `Donor` copies, fills them in store order. |
| `search_result_free(r)` | Frees the copies, sets the pointer to `NULL`, count to 0. Safe to call twice or on a zeroed result. |
| `donor_matches` (static) | The matching rule used by both passes, so they cannot disagree. |

### 3.3 `api_search.c` - the HTTP handler

`api_get_search(app, req, res)` reads `patientId`, optional `city`, optional `availableOnly`; calls the search; writes the JSON answer. One cleanup block at the end (`done:`). A static helper `parse_patient_id` accepts only plain digits in the range 1..2147483647.

### 3.4 `json_util.c` - the JSON engine

| Group | Functions | In plain English |
|---|---|---|
| Growing string | `sb_init`, `sb_clear`, `sb_free`, `sb_append`, `sb_append_n`, `sb_append_char`, `sb_appendf`, `sb_cstr` | A string that grows by itself. Never returns `NULL` from `sb_cstr`. |
| Escaping | `sb_append_json_string` | Writes `"text"` with quotes and escapes, so the JSON stays valid. |
| Parser | `json_parse_object`, `json_object_free` | Reads ONE flat JSON object into key/value pairs. |
| Readers | `json_has_key`, `json_get_string`, `json_get_int`, `json_get_bool` | Look up a key and convert the value. |

Private helpers: `sb_reserve` (makes room), `parse_string`, `parse_bare_value`, `is_json_number`, `token_equals`, `add_pair`, `find_pair`, `skip_ws`.

---

## 4. Algorithms and key ideas

### 4.1 The compatibility table (go deep)

**Idea.** Instead of many `if` statements, store all 64 answers in a 2-D array. To ask "may donor X give to patient Y?" we look at one cell.

**Index order (frozen in the spec):**

| 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|
| A+ | A- | B+ | B- | AB+ | AB- | O+ | O- |

**The table.** Rows are the **patient (recipient)**, columns are the **donor**. `COMPAT[recipient][donor]`.

| Patient \ Donor | A+ | A- | B+ | B- | AB+ | AB- | O+ | O- | Row sum |
|---|---|---|---|---|---|---|---|---|---|
| **A+** | 1 | 1 | 0 | 0 | 0 | 0 | 1 | 1 | 4 |
| **A-** | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 1 | 2 |
| **B+** | 0 | 0 | 1 | 1 | 0 | 0 | 1 | 1 | 4 |
| **B-** | 0 | 0 | 0 | 1 | 0 | 0 | 0 | 1 | 2 |
| **AB+** | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 8 |
| **AB-** | 0 | 1 | 0 | 1 | 0 | 1 | 0 | 1 | 4 |
| **O+** | 0 | 0 | 0 | 0 | 0 | 0 | 1 | 1 | 2 |
| **O-** | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 1 | 1 |

Row sums: 4 + 2 + 4 + 2 + 8 + 4 + 2 + 1 = **27**. So 27 of 64 cells are 1.

**Read two rows aloud (say this in the presentation):**
- **O- row:** "An O-negative patient can receive only from O-negative. One '1', in the last column."
- **AB+ row:** "An AB-positive patient can receive from every group. All eight cells are '1'."

**Why exactly 27?** Each row counts how many donor groups can give to that patient. A patient with *more* antigens (A, B, Rh+) accepts *more* donor types. AB+ has all three, so it accepts all 8. O- has none, so it accepts only O-. The row sums above are just those counts added up. (Do not try to derive 27 by a formula in the viva. Add the row sums.)

**Real code (`compatibility.c`):**

```c
static const int COMPAT[BG_COUNT][BG_COUNT] = {
    /* A+  : */ { 1, 1, 0, 0, 0, 0, 1, 1 },
    /* A-  : */ { 0, 1, 0, 0, 0, 0, 0, 1 },
    /* B+  : */ { 0, 0, 1, 1, 0, 0, 1, 1 },
    /* B-  : */ { 0, 0, 0, 1, 0, 0, 0, 1 },
    /* AB+ : */ { 1, 1, 1, 1, 1, 1, 1, 1 },
    /* AB- : */ { 0, 1, 0, 1, 0, 1, 0, 1 },
    /* O+  : */ { 0, 0, 0, 0, 0, 0, 1, 1 },
    /* O-  : */ { 0, 0, 0, 0, 0, 0, 0, 1 }
};
```

and the lookup (one line of real logic):

```c
return COMPAT[patientIndex][donorIndex];    /* table is [recipient][donor] */
```

**Pseudocode:**
```
can_donate(donorGroup, patientGroup):
    d = index(donorGroup);  p = index(patientGroup)
    if d < 0 or p < 0: return 0
    return COMPAT[p][d]
```

**Worked example.** Donor `O-`, patient `A+`: d = 7, p = 0. `COMPAT[0][7]` = 1, so yes. Donor `A+`, patient `O-`: d = 0, p = 7. `COMPAT[7][0]` = 0, so no. Swapping the order gives a different answer, which is why the row/column order matters.

**Cost.** Time: constant (looking up one cell; finding the index scans at most 8 names). Memory: 64 ints, fixed.

**Why a table and not if/else?** The table is *data*: you can read it row by row and compare it with a textbook. One wrong digit is easy to spot. A long `if/else` chain would need about 27 conditions and each one is another place for a typo. **Alternative:** a bit mask per group (one 8-bit number per row). It is shorter but harder for a beginner to read. We chose readability.

### 4.2 Case-insensitive comparison (own loop)

**Idea.** Compare two strings, treating `P` and `p` as equal.

**Why not `strcasecmp`?** `strcasecmp` is a POSIX function, not part of standard C11. The team spec banned it (the project builds with `-std=c11 -pedantic`). So I wrote my own loop. **VERIFY:** I did not test what MSYS2 does with `strcasecmp` under `-std=c11`; I only know it is not ISO C and is on the banned list.

**Real code:**

```c
static char lower_ascii(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return (char)(c + ('a' - 'A'));
    }
    return c;
}
```

**Worked example.** `"PUNE"` vs `"pune"`: P to p, U to u, N to n, E to e, both end together, so equal. `"Pune"` vs `"Pun"`: the second string ends first, so *not* equal (a prefix is not a match).

**Cost.** Time: one pass over the shorter string. Memory: none extra. **Limit:** ASCII only. Accented letters are compared byte by byte, which is fine for our sample cities. **Alternative:** `tolower()` from `<ctype.h>` depends on the C locale; ours does not.

### 4.3 `compat_is_edge` - the single edge rule

**Idea.** One function answers "is there an edge between this donor and this patient?" The graph (Dev 3) calls it for every cell of the matrix.

```c
if (!compat_can_donate(donor->bloodGroup, patient->bloodGroup)) { return 0; }
if (donor->available != 1) { return 0; }
return compat_city_matches(donor->city, cityFilter);
```

**Why one shared function?** If the graph had its own copy of the rule and search had another, they could slowly drift apart (one gets fixed, the other not). The screen would then show a donor in the search page who is missing in the matrix. One function means one place to fix.

> **Honest detail (very likely viva question):** `search.c` does **not** call `compat_is_edge`. It calls the two building blocks `compat_can_donate` and `compat_city_matches` directly. The reason: the search has an `availableOnly=false` option that must include *unavailable* donors, but `compat_is_edge` always demands `available == 1`. So with `availableOnly=true` (the default) search and graph agree exactly; with `false` search deliberately shows more. See section 11, tricky question 1. I did not write a test that compares search output with graph edges (the graph module is not mine); `test_compat.c` only checks that `compat_is_edge` equals "can_donate AND available AND city matches" for 384 combinations.

### 4.4 The medical disclaimer

Exact text from the spec (shown in the UI on the search page and in the footer):

> "The system identifies potential red blood cell donor compatibility based on predefined blood group rules. It does not provide medical approval. Actual donation requires professional medical screening and cross-matching by a qualified blood bank."

**Why it exists.** Our table is a simplified textbook rule for red blood cells. Real blood banks also test other things (cross-matching, infections, donor health). A student project must never look like medical approval.

**Where it lives.** The disclaimer is displayed by the frontend (Dev 5). It is **not** in any of my C files (I checked with a search of my four backend files: the text does not appear). **VERIFY** in `frontend/index.html` before presenting.

### 4.5 Two-pass search (go deep)

**Idea.** We do not know in advance how many donors match. Two passes avoid guessing: pass 1 **counts**, pass 2 **allocates exactly that many** and **fills**.

**Pseudocode:**
```
search(store, patientId, cityFilter, availableOnly, out):
    out = {donors: NULL, count: 0}
    patient = find_patient(patientId);  if none: return NOT_FOUND
    count = 0
    for each donor in store:  if matches(donor): count++          # pass 1
    if count == 0: return OK                                     # donors stays NULL
    copies = malloc(count * sizeof(Donor))
    for each donor in store:  if matches(donor): copies[k++] = *donor   # pass 2
    out = {copies, k};  return OK

matches(donor) = can_donate(donor.group, patient.group)
                 AND city_matches(donor.city, cityFilter)
                 AND (availableOnly ? donor.available == 1 : true)
```

**Real code (`search.c`, the heart of it):**

```c
    for (i = 0; i < store->donors.count; i++) {              /* pass 1 */
        if (donor_matches(&store->donors.items[i], patient, cityFilter, availableOnly)) {
            count++;
        }
    }
    if (count == 0) {
        return STATUS_OK;                   /* donors stays NULL: we never call malloc(0) */
    }
    if ((size_t)count > SIZE_MAX / sizeof(Donor)) {
        return STATUS_ERR_NOMEM;
    }
    copies = malloc((size_t)count * sizeof(Donor));
```

**Hand trace (6 donors).** Patient 4 is **B+** in **Pune**. B+ can receive from B+, B-, O+, O- (row B+ of the table).

| Donor id | Group | City | Available | Compatible with B+? |
|---|---|---|---|---|
| 101 | O- | Pune | 1 | yes |
| 102 | A- | Nagpur | 1 | no |
| 103 | A+ | Pune | 1 | no |
| 104 | O+ | Mumbai | **0** | yes |
| 105 | B- | Pune | 1 | yes |
| 106 | AB- | Mumbai | 1 | no |

| Query | Pass 1 count | Result ids (store order) |
|---|---|---|
| no city, `availableOnly=true` | 2 (101, 105) | 101, 105 |
| no city, `availableOnly=false` | 3 (101, 104, 105) | 101, 104, 105 |
| `city=Pune`, `availableOnly=true` | 2 | 101, 105 |
| `city=mumbai`, `availableOnly=false` | 1 | 104 |
| `city=mumbai`, `availableOnly=true` | **0** | none (`donors == NULL`, count 0) |

I ran exactly this trace in a small throwaway program on Linux (6 donors, 1 patient, `search.c` + `compatibility.c`) and the results above are what it printed. These six donors are the first six of the test data in `tests/test_search.c`; the real sample data of the project is different.

**Why never `malloc(0)`?** The behaviour of `malloc(0)` depends on the platform (it may return `NULL` or a unique pointer). The spec forbids it, and the code avoids it by returning early. Then `NULL` has one clear meaning: "no results".

**Why COPIES?** The spec says pointers from `records_find_*` become invalid after the next `records_add_*`, because the donor array may be moved by `realloc`. A copy stays valid even if the store grows or changes, and the caller can change its copy without touching the store. (`test_search.c` checks that changing a copy leaves the store record unchanged.) **Cost of copies:** extra memory (about 100 bytes per matching donor; **VERIFY** with `sizeof(Donor)`).

**Cost.** Time: two passes over D donors = about 2 x D checks, so O(D), plus the patient lookup in `records_find_patient` (Dev 1; I assume a linear scan, **VERIFY**). Memory: only the matches (k donors), not all D.

**Alternative:** one pass with `realloc` growth. It is a single loop but needs growth logic and over-allocates. Another alternative: return pointers into the store with no copy; cheaper, but dangerous after the array moves.

**Errors.** Unknown patient gives `STATUS_ERR_NOT_FOUND`. NULL `out` or NULL `store` gives `STATUS_ERR_INTERNAL`. Allocation failure gives `STATUS_ERR_NOMEM`. On every error the result is left empty (`donors = NULL`, `count = 0`).

### 4.6 `api_get_search` - parsing and the JSON answer

**Order of checks (first failure wins):**
1. `patientId` missing, not digits, 0, or above 2147483647 gives **400 `INVALID_ID`**
2. `city` malformed or longer than 29 bytes gives **400 `BAD_REQUEST`** (absent means no filter)
3. `availableOnly` must be exactly `true` or `false`, otherwise **400 `BAD_REQUEST`** (absent means `true`)
4. unknown patient gives **404 `NOT_FOUND`**

**Success: HTTP 200** (this exact layout is checked character by character in `tests/test_api_search.c`, using test stubs of Dev 1's writers):

```json
{"ok":true,"data":{"patient":{"id":3,"name":"Sunil","bloodGroup":"O-","city":"Nagpur"},
 "filters":{"city":"","availableOnly":true},"count":1,
 "donors":[{"id":101,"name":"Asha Rao","bloodGroup":"O-","city":"Pune","phone":"9000000101","available":true}]}}
```
(shown on several lines here; the real answer is one line)

**Zero matches is NOT an error.** It is still HTTP 200 with `"count":0,"donors":[]`. The question "who can donate?" was answered correctly: nobody. Only a bad request or a missing patient is an error.

**Note:** `filters.city` echoes the city exactly as the client typed it (e.g. `pune`), not the spelling stored in the donor records.

### 4.7 `StrBuf` and doubling growth (go deep)

**Idea.** A string whose size is not known in advance. We keep four fields:

| Field | Meaning |
|---|---|
| `data` | pointer to the characters (always ends with `'\0'` when length > 0) |
| `length` | how many characters are used now |
| `capacity` | how many bytes are allocated |
| `failed` | 1 if any allocation ever failed |

**Worked example.** Capacity starts at 64 (`SB_MIN_CAPACITY`). Suppose `length` is 60 and we append 10 characters. `needed = 60 + 10 + 1 = 71` (the `+1` is the `'\0'`). 71 > 64, so we double: 64 becomes 128. Another 100 characters later (length 170, needed 171 > 128) it doubles to 256. Doubling means n appends cost about n copies in total, not n x n.

**Real code (`sb_reserve`):**

```c
    tmp = realloc(sb->data, newCap);
    if (tmp == NULL) {
        sb->failed = 1;
        return 0;
    }
    sb->data = tmp;
    sb->capacity = newCap;
```

**Why a temporary pointer?** If `realloc` fails it returns `NULL` but the **old block is still allocated**. Writing `sb->data = realloc(sb->data, ...)` would overwrite the only pointer to it with `NULL`, and the old memory would leak. With `tmp` we keep `sb->data` valid and only set `failed`.

**Why a sticky `failed` flag?** Building one JSON answer needs dozens of appends. If each could fail, every line would need an `if`. Instead, once memory runs out `failed` becomes 1 and all later appends do nothing. The handler checks once at the end (Dev 1's `http_end_ok` does this and replaces the body with a 500 error). **Trade-off:** the result may be half-written, so it must never be sent when `failed` is 1.

**Overflow guards.** Before computing `length + extra + 1` the code checks `extra > SIZE_MAX - length - 1`, so the addition can never wrap around.

**Cost.** Append: amortised O(1) per character. Memory: at most about 2x the text length.

**Alternative:** a linked list of chunks or a fixed `char buf[4096]`. The fixed buffer is simplest but the spec forbids silent truncation and fixed limits on result size.

**Sharp edge (documented in a comment):** never append a pointer that points *inside* `sb->data` to the same buffer; `realloc` may move the block and leave that pointer dangling. Handlers only append literals and other buffers.

### 4.8 `sb_append_json_string` - escaping

**Idea.** A JSON string is surrounded by `"` quotes. A name like `Ann "AJ" Rao` would break the JSON if written as-is. So we escape.

| Character | Written as |
|---|---|
| `"` | `\"` |
| `\` | `\\` |
| newline, carriage return, tab | `\n`, `\r`, `\t` |
| backspace, form feed | `\b`, `\f` |
| any other byte below 0x20 | `\u00XX` (e.g. byte 1 gives `\u0001`) |
| bytes 0x80 and above (UTF-8) | **unchanged** |
| `/` | unchanged (not escaped) |

**Example.** Input `say "hi"` gives output `"say \"hi\""`.

Bytes are read as `unsigned char`; otherwise accented letters would look like negative numbers and be mistaken for control characters.

### 4.9 The JSON parser (go deep, beginner level)

**Idea.** A *scanner*: one pointer `p` walks left to right through the text and never goes back. It follows this shape: `{ "key" : value , "key" : value }`.

**Steps (in order, as in `json_parse_object`):**
1. Skip whitespace. Next character must be `{`, else **BAD_REQUEST**.
2. Skip whitespace. If the next is `}`, it is an empty object `{}`: done.
3. Otherwise loop:
   - expect `"`, parse the **key** string;
   - skip whitespace, expect `:`;
   - skip whitespace, parse the **value**: a `"string"`, or a bare token (`true`, `false`, `null` or a number). A `{` or `[` here means nested, which is **rejected**;
   - store the pair; skip whitespace;
   - `,` means go round again (a `,` followed by `}` fails because the next key must start with `"`); `}` means finish; anything else is an error.
4. After the closing `}` only whitespace may remain. Any other text is **trailing text: rejected**.

**Worked trace.** Text: `{"id": 7, "name": "Asha"}`

| Step | `p` is at | Action | Stored so far |
|---|---|---|---|
| 1 | `{` | accept, skip space | - |
| 2 | `"id"` | key = `id` | - |
| 3 | `:` | accept, skip space | - |
| 4 | `7` | bare value, token `7` (number OK) | pair 1: id = 7 (not a string) |
| 5 | `,` | accept, skip space | - |
| 6 | `"name"` | key = `name` | - |
| 7 | `"Asha"` | string value | pair 2: name = Asha (a string) |
| 8 | `}` | accept; only whitespace left | **OK, count = 2** |

**What it supports:** one flat object; values that are string, number, `true`, `false` or `null`; escapes `\" \\ \/ \b \f \n \r \t`; raw UTF-8 bytes.

**What it rejects (BAD_REQUEST):**
- nested objects and arrays (`{"a":{"b":1}}`, `{"a":[1]}`)
- `\uXXXX` escapes (documented limitation)
- text after the closing brace; missing brace; trailing comma; missing colon or value; unquoted keys
- numbers that break JSON rules (`012`, `1.`, `-`)
- raw control characters (e.g. a real newline) inside a string

**How the data is stored.** Each pair holds a `key`, a `value` (both separately `malloc`'d) and `isString`. For a number the `value` is the **raw token** (`"7"`), not an int. Conversion happens later in `json_get_int`.

**Duplicate keys:** all pairs are kept, and lookup searches from the end, so **the last one wins**. `{"a":1,"a":2}` gives `a = 2`.

**Readers:**
- `json_get_string`: `NOT_FOUND` if absent *or not a string*; `BAD_REQUEST` if it does not fit `outSize` (never truncates). Sets `out` to `""` first.
- `json_get_int`: optional `-` then digits only, within `int` range. `1.5`, `1e3`, `"12"` (quoted) and `true` give `BAD_REQUEST`. The overflow check is done at each digit, so even 99999999999999999999 cannot overflow.
- `json_get_bool`: unquoted `true`, `false`, `1`, `0`. A quoted `"true"` is rejected.

**Real code (the heart of the loop, shortened):**

```c
            p = skip_ws(p);
            if (*p == ',') {
                p = skip_ws(p + 1);
                continue;                    /* a trailing comma fails the '"' test above */
            }
            if (*p == '}') {
                p++;
                break;
            }
            st = STATUS_ERR_BAD_REQUEST;
            break;
```

**How a failed parse frees everything.** The parser builds into a *private* `result` object. Every error path goes to the end, where `json_object_free(&result)` frees all pairs already stored, and `*out` was set to empty at the start. On success only, `*out = result`. For a pair that was half-built (key allocated, value failed) the code frees the key by hand before breaking out. `test_json.c` has cases like `{"a":1,"b":{"c":2}}` (fails after a good pair), and the sanitizer and valgrind runs found no leaks.

**Cost.** Time: one pass over the text, O(n); strings are scanned twice (once to find the end, once to copy), still O(n). `find_pair` is a linear search from the end, O(pairs). Memory: one copy of every key and value.

**Why hand-written?** External libraries are forbidden, and this scanner is about 300 lines a beginner can read. **Alternative:** a full JSON parser with nesting (recursive). It is more general but much longer and not needed, because every request body in our API is one flat object.

---

## 5. Memory and pointers in my part

**Allocations I make**

| Where | What | Freed by |
|---|---|---|
| `sb_reserve` | `realloc` for `StrBuf.data` | `sb_free` (the owner of the `StrBuf`: the server loop for `HttpResponse.body`) |
| `parse_string`, `parse_bare_value` | `malloc` for each key and value | `json_object_free` |
| `add_pair` | `realloc` for the `pairs` array | `json_object_free` |
| `search_compatible_donors` | `malloc(count * sizeof(Donor))` | `search_result_free`, called in the handler's `done:` block |

**Who owns what** (from spec section E): `HttpResponse.body` is owned by the server loop and freed after sending. `DonorSearchResult` and `JsonObject` are *local to a handler*: init, use, free before return. `Patient` pointers from `records_find_patient` are **borrowed**: I use them but never free or keep them.

**If an allocation fails**
- `StrBuf`: sets `failed = 1`; old text stays valid; later appends do nothing.
- Parser: returns `STATUS_ERR_NOMEM` after freeing everything built so far.
- Search: returns `STATUS_ERR_NOMEM` with an empty result.
- The handler passes the error on with `http_send_error`.

**Guards against overflow.** `needed = length + extra + 1` is checked before it is added. `count * sizeof(Donor)` is checked (`count > SIZE_MAX / sizeof(Donor)`). The pairs array size is checked the same way. No `malloc(0)` anywhere (strings always allocate length + 1 at least).

**How double free and leaks are avoided**
- After `free`, the pointer is set to `NULL` (`sb_free`, `json_object_free`, `search_result_free`). A second call then frees `NULL`, which is harmless.
- Every `*_free` works on a zeroed object.
- The handler uses **one** `done:` label at the end; every exit goes through it. Before the first `goto`, `result` is set to `{NULL, 0}` so the cleanup is always safe.
- `realloc` goes through a temporary pointer.

**Real code (handler cleanup):**

```c
done:
    /* Single cleanup block: runs on success and on every error path. */
    search_result_free(&result);
```

**Common beginner mistake:** `p = realloc(p, newSize);`. If `realloc` fails, `p` becomes `NULL` and the old block is lost forever (a leak). Always use `tmp = realloc(p, ...); if (tmp == NULL) {...} else p = tmp;`.

---

## 6. Validation and error handling in my part

| Situation | Status returned | HTTP code (spec table) |
|---|---|---|
| `patientId` missing, empty, not digits, 0, negative, above 2147483647 | `STATUS_ERR_INVALID_ID` | 400, code `INVALID_ID` |
| `city` malformed (bad `%` encoding) or over 29 bytes | `STATUS_ERR_BAD_REQUEST` | 400, `BAD_REQUEST` |
| `availableOnly` not exactly `true`/`false` (e.g. `TRUE`, `1`, empty) | `STATUS_ERR_BAD_REQUEST` | 400, `BAD_REQUEST` |
| Patient id valid but no such patient | `STATUS_ERR_NOT_FOUND` | 404, `NOT_FOUND` |
| Out of memory in search | `STATUS_ERR_NOMEM` | 500, `OUT_OF_MEMORY` |
| NULL `app` or `req` passed to the handler | `STATUS_ERR_INTERNAL` | 500, `INTERNAL_ERROR` |
| No compatible donors | *not an error* | **200**, `"donors":[]` |
| Invalid blood group text in `compat_normalize_blood_group` | `STATUS_ERR_INVALID_BLOOD_GROUP` | 400 (when Dev 1's code returns it) |
| JSON: nested, `\u`, trailing text, missing brace, etc. | `STATUS_ERR_BAD_REQUEST` | 400 |
| JSON reader: key absent | `STATUS_ERR_NOT_FOUND` | (the caller decides; Dev 1/4 turn it into the normal `INVALID_*` error) |

Message texts I pass to `http_send_error`, for example `"no patient with that id"` and `"availableOnly must be true or false"`, are my wording. The mapping from status to HTTP code is done by Dev 1's code (`status_http_code`), not by me.

---

## 7. How I coordinated with the other developers

**The contract.** I worked only from the Common Technical Specification: frozen headers, JSON formats, the status table, ownership rules. I never talked to another developer; the human team member carried messages.

**What I relied on from others**
- `records_find_patient` returning `NULL` when absent, and the *borrowed* pointer rule (Dev 1).
- `patient_write_json` and `donor_write_json` writing exactly the JSON of spec section H (Dev 1).
- `http_query_get`: `NOT_FOUND` for an absent key, `BAD_REQUEST` for a malformed or too-long value; plus `http_begin_ok`, `http_end_ok`, `http_send_error` (Dev 1). If `http_query_get` behaves differently, my 400/404 mapping changes. **VERIFY** in `http_server.c`.

**What others rely on from me**
- `sb_*` and `StrBuf`: all handlers and all writers (Devs 1, 3, 4).
- `compat_is_edge`: the graph (Dev 3). `compat_normalize_blood_group`: validation in `records.c` (Dev 1).
- `json_get_*`: request-body handlers (Devs 1 and 4).

**Problems that came up**
1. **The `test_search.c` episode.** At one point `tests/test_search.c` showed "Patient and RecordStore undefined" and struct declaration errors. These were **not a bug in the test**. First, `records.h` had not yet been written to disk (the developers delivered `.c` files and nobody wrote the headers). The orchestrator then generated all 11 headers from the frozen Section F and syntax-checked them with gcc. Later VS Code kept showing the same false errors although `gcc` compiled fine, because the editor did not know about the `backend/` include folder. It was fixed with `.vscode/c_cpp_properties.json` (`includePath = backend`). In my own runs I never saw these errors because I passed `-Ibackend` myself.
2. **Missing spec for one developer.** Developer 3's first copy of the task arrived without the specification (a paste problem). It was re-sent as one attached file. It did not affect my files, but it shows why one single written contract mattered.
3. **Browser opening the file directly** (`file://`) cannot call `/api/...`. The app must be opened at `http://localhost:8080`. This affects the demo.

**What I would do differently**
- Run the tests against the real `records.c` as soon as it exists (I only used stubs).
- Add a test that compares search output (`availableOnly=true`) with graph edges for the same patient.
- Agree one place for the disclaimer text in the spec, so it is also in an API response if needed.
- Ask for headers to be written to disk in the very first round.

---

## 8. Testing

### 8.1 What I wrote

| Test file | What it covers |
|---|---|
| `tests/test_compat.c` | All 64 recipient/donor pairs against the plain-text lists from spec section G (expects 27 compatible); the same pairs in lower case; invalid and NULL groups; normalising (`" a+ "`, `"ab-"`, over-long text, tab); city matching (NULL, empty, case, prefix); `compat_is_edge` against its definition for 384 combinations |
| `tests/test_json.c` | `StrBuf` growth to 10,000 bytes, `sb_appendf` larger than the buffer, the `failed` flag, escaping, parser success on a sample donor body, 25 parser failures, `json_get_int` limits (INT_MAX, INT_MIN, overflow), `json_get_bool`, 100 pairs, free-twice safety |
| `tests/test_search.c` | A hand-built `RecordStore` (4 patients, 8 donors) with a test-only stub of `records_find_patient`: available only, unavailable included, city filter (case-insensitive), no compatible donors, unknown/zero/negative patient id, copies independent of the store, zero donors, free-twice |
| `tests/test_api_search.c` | **Extra, my own addition.** The exact JSON text, every error case of the handler, NULL arguments. Uses test-only stubs of Dev 1's functions |

### 8.2 What I actually ran

On a **Linux** machine (Ubuntu, gcc 13.3), **not** on the team's Windows/MSYS2 setup. Because the other developers' files did not exist yet, I used copies of the frozen headers kept outside `backend/`.

- `gcc -std=c11 -Wall -Wextra -pedantic -fsyntax-only` on my four backend files: **no warnings**. Also compiled with `-O2 -Wshadow -Wconversion`: no warnings.
- The four test programs built with `-fsanitize=address,undefined` and run: **63, 133, 33 and 38 checks, 0 failed**.
- The same four built at `-O2` without sanitizers: all passed.
- The same four run under `valgrind --leak-check=full`: no errors, no leaks.
- One check in `test_api_search.c` failed on the first run; the **handler was right and my expected text was wrong** (a missing `}`). I fixed the test and re-ran everything.
- The 6-donor trace of section 4.5 in a throwaway program.

### 8.3 What is NOT verified

- **NOT RUN by me:** the full `gcc ... backend/*.c -lws2_32` build, `sbdf.exe`, any real HTTP call, anything on Windows.
- **NOT RUN:** the handler with the *real* `http_query_get`, `records_find_patient`, `donor_write_json` (only my stubs).
- **NOT RUN:** out-of-memory behaviour (I did not intercept `malloc`/`realloc`); only the size-overflow guard of `StrBuf` is tested.
- **Team level:** the browser click-through; results of running each `tests/test_*.c` **on the team's machine** (the team's record says only that `test_search.c` passed the gcc syntax check); AddressSanitizer on Windows.
- **Linking note:** `test_search.c` defines its own `records_find_patient`. Compile it with only `search.c` and `compatibility.c` (not with `records.c`), or the linker reports a duplicate definition.

**Commands to rebuild a test (from the project root, once the real headers exist):**
```
gcc -std=c11 -Wall -Wextra -pedantic -g -Ibackend -o test_compat tests/test_compat.c backend/compatibility.c
gcc -std=c11 -Wall -Wextra -pedantic -g -Ibackend -o test_json   tests/test_json.c   backend/json_util.c
gcc -std=c11 -Wall -Wextra -pedantic -g -Ibackend -o test_search tests/test_search.c backend/search.c backend/compatibility.c
```

### 8.4 Five-step LIVE DEMO

**Data assumed:** the ORIGINAL sample data (12 donors, 8 patients). The exact donor and patient lists belong to Dev 4; I do not know them, so I cannot print the expected donor ids. **I have NOT run these commands against the real server.** What I can promise (from my stub-based test) is the *shape* of every answer. Before presenting, run the commands once and write down the real ids.

Start the server in one window: `sbdf.exe --demo` (or `start.bat`). Use `curl.exe` in a second window (on Windows PowerShell plain `curl` is an alias for something else). Keep the quotes: `&` must not be seen by the shell.

| Step | Command | What to expect |
|---|---|---|
| 0 (optional, shows the table) | `gcc -std=c11 -Wall -Wextra -pedantic -Ibackend -o test_compat tests/test_compat.c backend/compatibility.c` then `test_compat` | Last lines: `63 checks, 0 failed` and `ALL TESTS PASSED` (this is what I got on Linux) |
| 1 | `curl.exe "http://localhost:8080/api/search?patientId=1"` | HTTP 200. `"filters":{"city":"","availableOnly":true}`, `"count":N`, a list of donors, **all with `"available":true`**. **VERIFY** N and the ids against the sample data. |
| 2 | `curl.exe "http://localhost:8080/api/search?patientId=1&city=Pune"` | `"filters":{"city":"Pune","availableOnly":true}`. Every donor has `"city":"Pune"`. Count is the same or smaller than step 1. |
| 3 | `curl.exe "http://localhost:8080/api/search?patientId=1&availableOnly=false"` | `"availableOnly":false`. Count is the same or **larger** than step 1: unavailable compatible donors (with `"available":false`) now appear. With the original data only 1 of 12 donors is unavailable (11 are available), so the difference is at most 1. |
| 4 | `curl.exe "http://localhost:8080/api/search?patientId=999"` | HTTP 404 and body shaped like `{"ok":false,"error":{"code":"NOT_FOUND","message":"no patient with that id"}}`. (Message text comes from my handler; the envelope from Dev 1.) |
| 5 | `curl.exe "http://localhost:8080/api/search?patientId=abc"` | HTTP 400, `"code":"INVALID_ID"`. Shows that a bad id and an unknown id are different errors. |

Add `-i` to curl to also show the HTTP status line. For a prettier answer, pipe it to a JSON formatter if one is installed (VERIFY on your PC).

---

## 9. Limitations and known risks

- **Red blood cell rules only.** The table does not cover plasma, platelets, rare antigens or a real cross-match. Hence the disclaimer.
- **JSON parser is deliberately small:** flat objects only; no `\uXXXX`; no nesting; strings with raw control characters are rejected; one object per text. It is not a general JSON library.
- **ASCII-only case comparison:** `"Pune"` equals `"PUNE"`, but accented letters are compared byte by byte.
- **City filter is exact (case-insensitive) match:** `"Pun"` does not match `"Pune"`; spaces are not trimmed in `city` (`"Pune "` will not match).
- **`StrBuf` can be left half-written** after a failure; the caller must check `failed` (Dev 1's `http_end_ok` does).
- **Linear work:** each search touches every donor twice. Fine for 200 donors (expected sample size); it would not be good for millions.
- **Search copies donors:** extra memory for large result lists.
- **Search and graph can differ** when `availableOnly=false` (by design, see 4.3).
- **Patient is looked up twice:** once in the handler (to print it) and once inside the search. Harmless (the store does not change between them) but slightly wasteful.
- **Windows and real-server behaviour not tested by me** (section 8.3).
- **No automated build for the tests** (no Makefile target of mine; the team Makefile is Dev 1's).

---

## 10. THE PRESENTATION SCRIPT

### 10.1 Four-minute script (about 600 words)

**[SHOW: slide with the 8x8 table, O- row and AB+ row highlighted]**

Good morning. I will start with this table, because it is the heart of my part. Eight blood groups give eight rows and eight columns. Each row is a patient, the recipient. Each column is a donor. A one means: this donor group may give red blood cells to this patient group.

Read the O-negative row. It has a single one, in the O-negative column. An O-negative patient can receive only O-negative blood. Now read the AB-positive row. All eight cells are one. An AB-positive patient can receive from every group. Add up all the rows and you get twenty-seven ones out of sixty-four cells. Those twenty-seven ones are the whole medical rule of our system.

In C this table is a two-dimensional array called COMPAT. We index it recipient first, donor second. Groups are numbered from A-positive zero to O-negative seven. Why a table and not a long if-else chain? Because a table is data you can check line by line, and the code that uses it is one line. A long chain gives twenty-seven places to make a typo.

**[SHOW: slide "compat_is_edge"]**

Around the table are small helper functions. One trims spaces and turns "ab minus" into "AB-". Another compares city names ignoring capital letters, with our own loop, because strcasecmp is not standard C11. An empty city filter means any city. And compat_is_edge is the single rule for a graph edge: compatible blood, donor available, city matches. The graph module uses that one function, so the matrix cannot disagree with the rule.

**[SHOW: slide "two-pass search"]**

Next, the search. search_compatible_donors works in two passes. Pass one only counts the matching donors. Pass two asks malloc for exactly that many Donor structs and copies them in. No guessing, no wasted memory. If the count is zero we never call malloc of zero; the pointer stays NULL. We return copies, so the result stays valid even if the donor list grows and moves in memory.

The handler, api_get_search, turns this into JSON. Zero matches is not an error. It is HTTP 200 with an empty list, because "nobody matches" is a correct answer. An unknown patient is 404, and a bad patient id is 400. The search can also show unavailable donors when you ask for availableOnly equals false. By default it shows only available ones.

**[DEMO: curl "http://localhost:8080/api/search?patientId=1", then add &city=Pune, then &availableOnly=false, then patientId=999]**

**[SHOW: slide "StrBuf"]**

Finally, the JSON engine, written by hand with no library. StrBuf is a string that grows by itself. It has data, length, capacity and a failed flag. When it is full we double the capacity with realloc, through a temporary pointer, so a failure never loses the old memory. If memory runs out, failed becomes one and all later appends do nothing. So a handler checks once at the end.

**[SHOW: slide "parser steps"]**

Our parser is a small scanner. Skip spaces, open brace, key, colon, value, then comma or close brace. It reads one flat object with strings, numbers, true, false and null. It rejects nested objects, backslash-u escapes and trailing text. On any error it frees everything it allocated. These limits are documented, and they are enough, because every request in our API is one flat object.

Other modules depend on my code. The graph uses compat_is_edge. Every handler uses StrBuf. The record handlers use my JSON readers. I depend on the records module for finding a patient and for writing donors and patients as JSON, and the written specification was our only contract.

About testing: I ran my tests on Linux with the address sanitizer and valgrind. The full Windows run and the live server are the team's to confirm.

Thank you. I am happy to take questions.

*(Word count of the spoken text: about 600. **VERIFY** with your own stopwatch; it should take roughly four minutes at a calm pace.)*

### 10.2 Sixty-second version (about 150 words)

**[SHOW: 8x8 table]** This table is my part: eight blood groups, rows are patients, columns are donors. A one means "may donate". O-negative patients accept only O-negative; AB-positive accept everyone; 27 of 64 cells are one. In C it is a two-dimensional array, and compat_is_edge, built on it, is the single rule the graph uses. **[SHOW: search slide]** The search counts matches first, then allocates exactly that many Donor copies; zero matches gives an empty list with HTTP 200, an unknown patient gives 404. **[SHOW: StrBuf slide]** The JSON engine is hand-written: a string that doubles with realloc, a sticky failed flag, and a small parser for flat objects. It rejects nested objects, backslash-u escapes and trailing text. Tests ran on Linux with sanitizers; the Windows server run is for the team to confirm. Thank you.

---

## 11. VIVA Q&A

### 11.1 Questions about my part

**Q1. What does your part do in one sentence?**
It decides which blood groups are compatible, finds compatible donors for a patient, and provides the JSON string-building and parsing tools that every API handler uses.

**Q2. How are the blood groups stored and numbered?**
As text, upper-case (`"A+"`..`"O-"`). For the table they are numbered by `compat_blood_group_index`: A+=0, A-=1, B+=2, B-=3, AB+=4, AB-=5, O+=6, O-=7.

**Q3. Explain `COMPAT[recipient][donor]`.**
It is an 8x8 array of 0/1. The first index is the patient's group, the second is the donor's group. Cell `[0][7]` is 1 because an O- donor can give to an A+ patient; `[7][0]` is 0 because it does not work the other way round.

**Q4. Why are exactly 27 cells 1?**
I add the row sums: 4 + 2 + 4 + 2 + 8 + 4 + 2 + 1 = 27. Each row counts the donor groups a patient group can accept; AB+ accepts 8 and O- accepts only 1.

**Q5. Why a lookup table instead of if/else?**
The table is data that can be compared with a textbook row by row, and the code is a single lookup. An if/else chain would need about 27 conditions, each a chance for a typo.

**Q6. What does `compat_normalize_blood_group` do?**
It trims leading and trailing spaces, accepts any case, and copies the canonical spelling from my name table. So `" ab- "` becomes `"AB-"`. Anything else returns `STATUS_ERR_INVALID_BLOOD_GROUP`. It trims only the space character, not tabs.

**Q7. How does `compat_city_matches` treat an empty filter?**
A `NULL` or empty filter means "any city" and returns 1. Otherwise it compares with my own case-insensitive loop, and the whole name must be equal (no partial match).

**Q8. Why did you write your own lower-case loop?**
`strcasecmp` is POSIX, not standard C11, and the spec banned it. My `lower_ascii` is a few lines and does not depend on the locale.

**Q9. Why does the search use two passes?**
We do not know how many donors match. Pass 1 counts, so pass 2 can allocate the exact size and fill it. No `realloc` loop, no wasted space, and the empty case is handled before any `malloc`.

**Q10. Why does an empty result leave `donors` as `NULL` and never call `malloc(0)`?**
`malloc(0)` behaves differently on different platforms and the spec forbids it. With `count == 0` and `donors == NULL`, "no results" has one clear meaning.

**Q11. Why does the search return copies of the donors?**
The store's array can be moved by `realloc` when a donor is added, which makes pointers into it invalid. A copy stays valid and changing it cannot damage the store.

**Q12. What happens for count 0 and for an unknown patient?**
Count 0 is a normal answer: HTTP 200 with `"count":0,"donors":[]`. An unknown patient id is `STATUS_ERR_NOT_FOUND`, HTTP 404. A non-numeric or non-positive id is 400 `INVALID_ID`.

**Q13. What is `availableOnly` and what is its default?**
A query parameter. `true` (the default) returns only donors with `available == 1`; `false` also includes unavailable donors. Any other value is 400 `BAD_REQUEST`.

**Q14. Why does `StrBuf` double its capacity?**
Doubling makes n appends cost about n copies in total, instead of n x n if we grew by a fixed small amount.

**Q15. Why `tmp = realloc(...)` and not `p = realloc(p, ...)`?**
If `realloc` fails it returns `NULL` and the old block is still allocated. Assigning `NULL` to `p` would lose the only pointer and leak it; with `tmp` we keep the old pointer and set `failed`.

**Q16. What is the `failed` flag for?**
After one failed allocation every append becomes a no-op. A handler can append freely and check `failed` once at the end instead of after every append.

**Q17. Which characters does `sb_append_json_string` escape?**
`"` and `\`, the controls `\n \r \t \b \f`, and any other byte below 0x20 as `\u00XX`. UTF-8 bytes (0x80 and up) and `/` are left unchanged.

**Q18. How does your parser work?**
It is a scanner with one moving pointer: skip spaces, expect `{`, then repeat `"key"`, `:`, value, and `,` or `}`; after `}` only spaces may follow. Pairs are stored as copied key/value strings in a growing array.

**Q19. What does it reject and why?**
Nested objects and arrays, `\uXXXX` escapes, text after the closing brace, missing braces, trailing commas, and badly formed numbers. The first two are documented limitations; our API only needs flat objects.

**Q20. How does a failed parse avoid leaking memory?**
It builds into a private object; every error path ends in `json_object_free` on it, and half-built pairs are freed by hand. `*out` stays empty unless the whole parse succeeded.

### 11.2 Three tricky questions (with the trap)

**T1. "Search and the graph use the same edge rule, so they always give the same donors. Correct?"**
*Trap:* Saying "yes" is wrong. **Answer:** `compat_is_edge` is the single rule for the graph. The search calls `compat_can_donate` and `compat_city_matches` directly, because with `availableOnly=false` it must also show unavailable donors, which an edge never allows. With the default `availableOnly=true` the two agree.

**T2. "O- is the universal donor, but its *row* in your table has only one 1. Is the table transposed by mistake?"**
*Trap:* Confusing rows and columns. **Answer:** No. Rows are the **patient**. The O- **row** (patient) has one 1, because an O- patient accepts only O- blood. The O- **column** (donor) has eight 1s, because an O- donor can give to everybody. The first index is the recipient.

**T3. "Your parser accepts the number `1.5` or `1e3`, but `json_get_int` rejects it. Is that a contradiction?"**
*Trap:* Thinking the parser and the reader do the same job. **Answer:** No. The parser only checks that the value is a *valid JSON token* (`1.5` is valid JSON). `json_get_int` is stricter on purpose: an id must be a whole number in `int` range. A quoted `"12"` is also rejected, and a missing key is `NOT_FOUND`, a wrong type `BAD_REQUEST`.

*(Backup tricky question: "Does the system approve a donation?" No. It only shows potential compatibility by predefined rules; the disclaimer says real donation needs medical screening and cross-matching by a blood bank.)*

### 11.3 Questions about other modules (hand over)

| Question | One-line answer | Hand over to |
|---|---|---|
| How is the adjacency matrix built, and what do row and column sums mean? | Rows are patients, columns are donors; a cell is 1 where `compat_is_edge` is true; row sums are patient degrees and column sums are donor degrees, and both total the edge count. | Dev 3 (`graph.c`) |
| How does the maximum matching work? | Kuhn's algorithm with a recursive DFS looks for augmenting paths, so an earlier patient can be moved to another donor to free a donor for someone else. | Dev 4 (`matching.c`) |
| Why do records live only in memory, and why can `records_find_*` pointers go stale? | There is no database by the project rules; the arrays grow with `realloc`, which may move them, so pointers become invalid after the next add. | Dev 1 (`records.c`) |
| Why is Winsock only in `http_server.c`? | That is the only file allowed to talk to the network; the rest of the code stays plain portable C. | Dev 1 (`http_server.c`, `router.c`) |
| Why doesn't JavaScript decide compatibility? | Project rule: C computes, JavaScript only displays, so the rules exist in one place. | Dev 5 (frontend) |

---

## 12. Glossary

1. **Recipient** - the patient who receives blood; the row of the table.
2. **Donor** - the person who gives blood; the column of the table.
3. **Lookup table** - an array that stores precomputed answers so code only reads a cell.
4. **Edge** - a link between one patient and one donor, present only when compatible, available and in the chosen city.
5. **Two-pass technique** - first count, then allocate exactly that much and fill.
6. **`malloc` / `realloc` / `free`** - ask for memory, resize it (it may move), give it back.
7. **Borrowed pointer** - a pointer into data owned by someone else; you may read it but must not free it or keep it long.
8. **`StrBuf`** - my growing string with `data`, `length`, `capacity` and `failed`.
9. **JSON** - a text format for data: `{"key": value}` with strings, numbers, true, false, null.
10. **Escaping** - writing special characters (like a quote) with a backslash so the JSON stays valid.
11. **Scanner (parser)** - code that walks through text step by step and recognises its parts.
12. **Sticky flag** - a flag that, once set, stays set until cleared (`failed`).

---

## 13. Cheat sheet - memorise these 10

1. Index order: **A+ 0, A- 1, B+ 2, B- 3, AB+ 4, AB- 5, O+ 6, O- 7.**
2. `COMPAT[recipient][donor]`: rows are patients, columns are donors.
3. **27 of 64** cells are 1. Row sums: 4, 2, 4, 2, **8**, 4, 2, **1**.
4. O- row has one 1; AB+ row has all eight.
5. `compat_is_edge` = compatible AND `available == 1` AND city matches. Used by the graph. Search uses `compat_can_donate` + `compat_city_matches`.
6. Search = **count, then `malloc` exactly, then fill**; no `malloc(0)`; `donors == NULL` when count is 0; result holds **copies**; free with `search_result_free`.
7. `GET /api/search?patientId=..[&city=..][&availableOnly=true|false]`, default `availableOnly=true`. Count 0 gives **200**; unknown patient **404**; bad id **400 `INVALID_ID`**; bad flag or city **400 `BAD_REQUEST`**.
8. `StrBuf` = `data`, `length`, `capacity`, `failed`; starts at **64**, doubles; `realloc` via a temporary pointer.
9. Parser: flat object only; supports string, number, true, false, null; rejects nested, `\uXXXX`, trailing text; last duplicate key wins.
10. Tested **on Linux only** (gcc, sanitizers, valgrind): 63 + 133 + 33 + 38 checks, 0 failed. Windows and the live server are for the team to confirm.

---

## 14. Handover notes (five sentences a teammate can say)

1. "Developer 2's part is the brain of the blood rules: one 8x8 table says which donor group can give to which patient group, and 27 of the 64 cells are allowed."
2. "The search counts the matching donors first, then allocates exactly that many Donor copies, so nothing is wasted and an empty result is just a NULL pointer with count zero."
3. "The search page returns HTTP 200 even when nobody matches, and gives 404 only when the patient does not exist."
4. "The JSON engine is hand-written: a growing string that doubles with realloc, and a small parser that reads one flat object and refuses anything nested or with trailing text."
5. "These tests passed on Linux with memory checkers, but the full Windows server run and the real-data demo are still to be confirmed by the team."

---

## VERIFY BEFORE PRESENTING

1. **Search vs `compat_is_edge`:** confirm with `grep -n "compat_" backend/*.c` that `graph.c` calls `compat_is_edge` and that `records.c` calls `compat_normalize_blood_group`. I wrote the callers from the spec, not from their code.
2. **Real demo output:** I did not run `curl` against the real server. Run steps 1-5 of section 8.4 once, write down the real patient 1 blood group, donor ids and counts, and put them on the slide.
3. **Original sample data:** that "only 1 of 12 donors is unavailable" is taken from the team's reported results (11 of 12 available). Check `sample_data.c`.
4. **`http_query_get` contract** in `http_server.c`: `NOT_FOUND` for an absent key, `BAD_REQUEST` for malformed or too-long values. The error mapping of my handler depends on it.
5. **Real error message text:** the body of the 404 for `patientId=999` should contain `"code":"NOT_FOUND"` (message wording comes from my handler; envelope from Dev 1's `http_send_error`).
6. **`records_find_patient` is a linear scan** (my cost statement) and `patient_write_json` / `donor_write_json` match spec section H. I used my own stubs of them.
7. **`sizeof(Donor)`:** I said "about 100 bytes"; print `sizeof(Donor)` to quote a number.
8. **`strcasecmp` under `-std=c11` on MSYS2:** I only claimed it is not ISO C and is on the banned list. I did not test what the compiler says.
9. **Disclaimer text in the frontend:** it is not in my C code; confirm it appears on the search page and in the footer of `frontend/index.html`.
10. **Test results on the team's machine:** my 63 / 133 / 33 / 38 results are from Linux with stub headers. Re-run the tests on Windows with the real headers. `test_search.c` must not be linked with `records.c`.
11. **`tests/test_api_search.c`** is an extra file I added; tell the team it exists (it is not in the spec's file list).
12. **Script timing:** the 4-minute script is about 600 words; read it aloud once with a stopwatch.
13. **`.vscode/c_cpp_properties.json`** and the incidents in section 7 are taken from the team notes, not from anything I saw myself.
