# Presentation Handbook - Developer 5: The Web Interface

*Topic: "The web interface: how the browser talks to the C backend and shows the graph."*

> **How this handbook was written.** I re-read my own three delivered files (`frontend/index.html`,
> `frontend/style.css`, `frontend/script.js`) before writing. Every function name, constant and code
> excerpt below comes from them (excerpts are trimmed to a few lines, never reworded).
> **I have not changed those files since delivering them.** I never ran them: my delivery report
> said "NOT COMPILED / NOT RUN", and that is still true for me. Anything that depends on running the app is
> marked **VERIFY**.

---

## 0. Title, role and file table

**Role:** Developer 5 (Claude Account 6). I wrote the whole frontend: the pages, the styling and the
JavaScript that talks to the C server.

| File | Purpose | Approx. lines (VERIFY with `wc -l`) |
|---|---|---|
| `frontend/index.html` | The page skeleton: sidebar menu, the 8 sections (views), forms, the edit dialog, the error banner, the toast area, the footer disclaimer | ~230 |
| `frontend/style.css` | Colours (CSS variables), layout, tables, badges, graph styles, responsive rules | ~250 |
| `frontend/script.js` | All behaviour: router, `api()` helper, forms, tables, SVG graph, matching view | ~850 |

I did **not** write `README_frontend.md` (it was optional). I did **not** build a mock server, so there is
no `dev-mock/` folder.

---

## 1. The 30-second summary

My part is the screen of the application. It is a **single-page app**: one HTML page whose sections are
shown or hidden by the part after the `#` in the address (`#graph`, `#matching`, ...). The page asks the C
server for data using `fetch`, then draws tables and an SVG picture of the donor-patient graph.

The golden rule: **JavaScript only displays, C computes.** Compatibility, degrees and the maximum
matching are never calculated in my code. The project needs my part because without it a user would have
to type `curl` commands to see anything.

---

## 2. Where my part sits

```
 BROWSER (my 3 files)                         C BACKEND (sbdf.exe)
 +-------------------------+   HTTP/JSON   +--------------------------------------+
 | index.html  style.css   | ------------> | http_server.c (Winsock, Dev 1)       |
 | script.js               |   fetch()     |   -> router.c (Dev 1)                |
 |   api() ... route()     | <------------ |      -> api_*.c handlers (Dev 1-4)   |
 |   buildGraphSvg() ...   |  {"ok":true,  |           -> records / compat /      |
 +-------------------------+   "data":..}  |              search / graph /        |
                                           |              matching (Dev 1-4)      |
                                           +--------------------------------------+
```

**Who calls me?** Nobody calls my JavaScript from C. The C server only *hands out my files*: per the spec,
`GET /` returns `frontend/index.html`, and other `GET` paths that do not start with `/api/` are served from
`frontend/` (types html, css, js, svg, png, ico, json). That static serving is in `http_server.c` (Dev 1).

**Whom do I call?** 12 different API calls. The right-hand column is the C handler name from the frozen
`api.h`. I know the handler names from the spec, not from reading the C files (VERIFY if you need detail).

| My call (in `script.js`) | Used by | C handler (owner) |
|---|---|---|
| `GET /api/stats` | `loadDashboard` | `api_get_stats` (Dev 4) |
| `GET /api/donors` | dashboard, donor directory, city dropdowns | `api_get_donors` (Dev 1) |
| `GET /api/patients` | dashboard, patient directory, search dropdown | `api_get_patients` (Dev 1) |
| `POST /api/donors` | donor form | `api_post_donor` (Dev 1) |
| `POST /api/patients` | patient form | `api_post_patient` (Dev 1) |
| `PUT /api/donors/{id}` | edit dialog | `api_put_donor` (Dev 1) |
| `PUT /api/donors/{id}/availability` | toggle button | `api_put_donor_availability` (Dev 1) |
| `GET /api/search?patientId=..` | `runSearch` | `api_get_search` (Dev 2) |
| `GET /api/graph[?city=]` | graph tab, matching view | `api_get_graph` (Dev 3) |
| `GET /api/graph/matrix[?city=]` | matrix tab | `api_get_graph_matrix` (Dev 3) |
| `GET /api/graph/degrees[?city=]` | degrees tab | `api_get_graph_degrees` (Dev 3) |
| `POST /api/matching` | `runMatching` | `api_post_matching` (Dev 4) |

Two endpoints exist in the spec that I **do not call**: `GET /api/donors/{id}` and `GET /api/patients/{id}`.
The directory tables already contain every field.

---

## 3. What I built, file by file and function by function

### 3.1 `index.html`
- A **sidebar** (`<header class="sidebar">`) with a brand and a `<nav>` of 8 links:
  `#dashboard`, `#donor-register`, `#patient-register`, `#donors`, `#patients`, `#search`, `#graph`, `#matching`.
- **Eight `<section class="view" id="view-...">` blocks**, all `hidden` at first. Each has a heading
  `id="h-<name>"` so the router can move keyboard focus to it.
- Two registration forms (`#donor-form`, `#patient-form`), the directory toolbars, the search form,
  the graph tabs (`role="tablist"`) and three tab panels, and the matching controls.
- A `<dialog id="edit-dialog">` for editing a donor's name, city and phone.
- Shared widgets: `#loading` (small "Loading..." pill), `#banner` (error banner with Retry and Dismiss),
  `#toasts` (an `aria-live="polite"` area for messages).
- The exact medical disclaimer appears twice: on the search view and in the footer.
- Inputs use a `data-field="id|name|bloodGroup|city|phone|available"` attribute, not `name="..."` (see 3.4 for why).

### 3.2 `style.css`
CSS variables for the palette (`--red: #B3122D`, `--navy: #14213D`, ...), the sidebar layout (grid), cards,
tables, badges, the toggle switch, tabs, the SVG graph styles (`.edge`, `.edge-matched`, `.edge.hl`,
`.node-off`, `.ring`), the dialog, and a media query `@media (max-width: 900px)` that turns the sidebar into a
top bar. It also has `@media (prefers-reduced-motion: reduce)` which switches animations off.

### 3.3 `script.js` - the main functions
Everything lives inside one function `(function () { 'use strict'; ... }());` so there are no global variables.

| Group | Functions | What they do |
|---|---|---|
| DOM helpers | `$`, `setAttrs`, `append`, `el`, `svgEl`, `clear`, `num`, `trunc`, `list` | Build elements safely (see 4.3). `num(v)` shows `-` if `v` is not a number. `list(v)` returns `v` if it is an array, otherwise `[]`. |
| API | `setPending`, `apiError`, `api` | The one place that calls `fetch` (see 4.2). |
| UI feedback | `showBanner`, `hideBanner`, `toast`, `setBusy` | Banner with Retry, auto-vanishing toasts, disable a button while its request runs. |
| Router | `route`, `guarded`, `VIEWS`, `init` | Show one view, run its loader (see 4.1). |
| Shared UI | `badge`, `bgChip`, `emptyState`, `buildTable`, `COL`, `fillBloodGroupSelect`, `populateCitySelect` | One generic table builder is used by every table. |
| Validation | `CHECKS`, `FIELD_BY_CODE`, `collectAndValidate`, `showServerError`, `setFieldError`, `clearFormErrors` | Browser-side checks and mapping of server errors to fields (see 4.4). |
| Forms | `setupRegisterForm`, `setupForms`, `bindSwitch` | Donor and patient registration. |
| Dashboard | `loadDashboard`, `renderStats`, `renderDistribution` | Six cards and the blood-group table. |
| Donors | `loadDonors`, `renderDonorTable`, `donorActions`, `toggleAvailability`, `openEdit`, `setupEditDialog` | Directory, filters, toggle, edit dialog. |
| Patients | `loadPatients`, `renderPatientTable` | Directory with text search. |
| Search | `loadSearch`, `runSearch`, `renderSearchResults` | Compatible-donor search page. |
| Graph | `buildGraphSvg`, `graphLegend`, `appendGraph`, `loadGraphView`, `loadGraphTab`, `setGraphTab`, `renderGraphPanel`, `renderMatrix`, `renderDegrees`, `setupGraphTabs` | The SVG graph, the matrix table, the degrees tables, and the tabs. |
| Matching | `loadMatchingView`, `runMatching`, `renderMatching` | Run the matching and draw the result. |

### 3.4 One detail worth knowing
The inputs have **no `name` attribute**. In HTML, a form control called `id` or `name` can hide the form's own
`id` and `name` properties (`form.id` would suddenly return the input box). I avoided the trap by looking inputs
up with `form.querySelector('[data-field="id"]')` through the small helper `fieldInput(form, name)`.

---

## 4. Algorithms and key ideas

### 4.1 The hash router (one view shown, the rest hidden)

**Idea.** The part of the address after `#` is called the *hash*. Changing it does not reload the page, but it
fires a `hashchange` event. `route()` reads the hash, hides every `.view` section except the matching one,
highlights the menu link, and calls that view's loader function.

**Worked example.** The user clicks "Graph". The address becomes `http://localhost:8080/#graph`.
`route()` takes `"#graph"`, strips the `#`, gets `"graph"`, hides 7 sections, shows `view-graph`,
sets `aria-current="page"` on the Graph link and calls `VIEWS['graph']`, which is `loadGraphView`.
If the hash is unknown (say `#banana`) the code falls back to `"dashboard"` (the address bar still says `#banana`; I do not rewrite it).

**Pseudocode.**
```
name = location.hash without '#'
if name is not a key of VIEWS: name = "dashboard"
navToken = navToken + 1            // marks "a new screen started"
hide error banner
for each section: hidden = (section.id != "view-" + name)
for each nav link: mark the one for 'name' as current page
if moveFocus: scroll to top, focus the heading
VIEWS[name]()                      // run the loader
```

**Real code (trimmed).**
```js
function route(moveFocus) {
    let name = (location.hash || '').replace(/^#/, '');
    if (!Object.prototype.hasOwnProperty.call(VIEWS, name)) { name = 'dashboard'; }
    navToken += 1;
    hideBanner();
    document.querySelectorAll('.view').forEach(function (sec) {
        sec.hidden = sec.id !== 'view-' + name;
    });
```

**Cost.** Time: a loop over 8 sections and 8 links, so tiny. Memory: nothing extra.
The `navToken` number lets `guarded()` ignore an error that arrives after the user has already moved to another view.

**Why this way / alternative.** Hash links need no library and work with the simple C file server (it never
has to understand routes). Alternative: the History API (`/graph` paths), which would need the C server to
return `index.html` for every path.

### 4.2 The `api()` helper (fetch + envelope)

**Idea.** Every C reply has the same shape: `{"ok":true,"data":{...}}` or
`{"ok":false,"error":{"code":"...","message":"..."}}`. One function, `api(path, method, payload)`, sends the
request, opens that envelope, and either **returns `data`** or **throws an `Error`** carrying `code`,
`message` and `status`. Every other function just writes `const data = await api('/api/stats')` inside a
`try/catch`.

**What counts as a failure (all become a thrown error).**

| Situation | Error `code` | Message the user sees |
|---|---|---|
| `fetch` itself fails (server not running, page opened from `file://`) | `NETWORK` | `Cannot reach the C backend. Is sbdf.exe running?` |
| Reply is not valid JSON | `BAD_RESPONSE` | `The backend sent a reply that is not valid JSON (HTTP <status>).` |
| JSON is not an object | `BAD_RESPONSE` | `The backend sent an unexpected reply (HTTP <status>).` |
| `ok` is not `true` | the server's `error.code` (or `HTTP_<status>`) | the server's `error.message` |
| `ok` is true but `data` is missing | `BAD_RESPONSE` | `The backend reply has no data.` |

**Worked example.** `POST /api/donors` with an ID that already exists. C answers HTTP 409 with
`{"ok":false,"error":{"code":"DUPLICATE_ID","message":"..."}}`. `api()` throws an `Error` with
`err.code === "DUPLICATE_ID"` and `err.status === 409`. (The exact message text comes from the C side: VERIFY.)

**The pending counter.** Every call does `setPending(1)` before and `setPending(-1)` in a `finally` block.
`pending` is the number of requests in flight; while it is above 0 the "Loading..." pill is visible. Because
`finally` always runs, an error cannot leave the pill stuck on.

**POST with no body.** If `payload` is `undefined`, `api()` sends no body and no `Content-Type`. That is how
`POST /api/matching` is sent when no city is chosen: `runMatching` passes `city ? { city: city } : undefined`.
The spec says an empty body means "no filter".

**Pseudocode.**
```
api(path, method, payload):
    build request; if payload given: add JSON body + Content-Type
    pending += 1
    try:
        try: reply = await fetch(...)     catch -> throw NETWORK error
        try: json = await reply.json()    catch -> throw BAD_RESPONSE error
        if json is not an object          -> throw BAD_RESPONSE error
        if json.ok is not true            -> throw error(json.error.code, json.error.message)
        if json.data is not an object     -> throw BAD_RESPONSE error
        return json.data
    finally:
        pending -= 1
```

**Real code (trimmed).**
```js
async function api(path, method, payload) {
    const init = { method: method || 'GET', headers: { 'Accept': 'application/json' } };
    if (payload !== undefined) {
        init.headers['Content-Type'] = 'application/json';
        init.body = JSON.stringify(payload);
    }
    setPending(1);
    try {
        let res;
        try {
            res = await fetch(path, init);
        } catch (e) {
            throw apiError('NETWORK', NETWORK_MESSAGE, 0);
        }
```

**Cost.** One network round trip per call; no extra memory. Several views fire calls in parallel with
`Promise.all` (dashboard: 3 calls, search page: 2). Whether the C server answers them in parallel or one
after the other depends on `http_server.c` (VERIFY).

**Why / alternative.** One helper means one place to fix bugs and to show the "Cannot reach the C backend"
message. Alternative: call `fetch` everywhere (copy-paste, easy to forget the error case), or use a library such as axios (forbidden: no external libraries).

### 4.3 Safe DOM building (`el()`, `append()`) and why not `innerHTML`

**Idea.** Names and cities are typed by users. If I put them into the page with `innerHTML`, the browser
would *parse them as HTML*. A donor named `<img src=x onerror=alert(1)>` would run script in everybody's
browser: that attack is called **XSS** (cross-site scripting). With `textContent` or a text node the
browser treats the text as plain characters, so the same name is simply shown as text.

**Worked example.** Donor name `<b>Asha</b>`.
- `div.innerHTML = name` shows **Asha** in bold (HTML was parsed - dangerous).
- `el('td', null, [name])` shows the characters `<b>Asha</b>` literally.

**Real code.**
```js
function append(node, children) {
    if (children === undefined || children === null) { return node; }
    const list = Array.isArray(children) ? children : [children];
    list.forEach(function (c) {
        if (c === undefined || c === null || c === false) { return; }
        node.appendChild(typeof c === 'object' ? c : document.createTextNode(String(c)));
    });
    return node;
}
```
A string child always goes through `document.createTextNode`. `setAttrs` writes the special key `text` with
`node.textContent`, and everything else with `setAttribute` (attribute values are not parsed as HTML either).
The same helpers build SVG elements (`svgEl`), so tooltips (`<title>`) are text nodes too.

**Cost.** Same as normal DOM building. **Why / alternative.** The rule "no `innerHTML` for records" is easy to
check: search `script.js` for `innerHTML` (I wrote none; VERIFY by searching). Alternative: build HTML strings and escape `<`, `>`, `&`, `"` by hand, which is easy to get wrong.

### 4.4 Form validation and error mapping

**Idea.** The browser repeats the backend rules to save the user a round trip and to give friendly messages.
The **server stays the authority**: anyone can bypass my page with `curl`. When the server rejects
something anyway, I show **its** message at the right field.

**Rules mirrored (from spec section G).**

| Field | Browser check (`CHECKS`) |
|---|---|
| id | digits only, value 1 to 2147483647 |
| name | not empty; at most 49 **bytes** (`TextEncoder` counts UTF-8 bytes); no control characters |
| bloodGroup | one of the 8 dropdown values |
| city | not empty; at most 29 bytes; no control characters |
| phone | 7 to 14 characters, optional leading `+`, rest digits |
| available | a checkbox, so it is always `true` or `false` |

**Worked example (bytes, not letters).** `"Zoe"` is 3 bytes. `"Zo\u00eb"` (with an e-diaeresis) is 3 letters but
4 bytes in UTF-8, because that letter takes 2 bytes. A name of 49 such letters could be over the C limit, so I count bytes.

**Phone.** `"9000000101"` (10 chars) passes. `"12345"` fails (shorter than 7). `"+919876543210"` (13 chars) passes.
*Assumption:* I count the `+` inside the 7-14 length; the spec wording is slightly ambiguous (VERIFY against `records.c`).

**Mapping server errors to fields.**
```js
const FIELD_BY_CODE = {
    INVALID_ID: 'id',
    DUPLICATE_ID: 'id',
    INVALID_NAME: 'name',
    INVALID_BLOOD_GROUP: 'bloodGroup',
    INVALID_CITY: 'city',
    INVALID_PHONE: 'phone',
    INVALID_AVAILABILITY: 'available'
};
```
`showServerError(form, err)` looks up `err.code`. If a field is found it writes `err.message` under that field,
marks it `aria-invalid="true"` and focuses it. Otherwise (network error, `BAD_REQUEST`, ...) it shows the message at the top of the form.

**Types matter.** I send `id` as a JSON **number** (`Number(v.id)`) and `available` as a JSON **boolean**,
because the spec says `json_get_int` rejects a quoted `"12"`.

**Buttons while a request is in flight.** `setBusy(btn, true, 'Saving...')` disables the button and changes its label; a `finally` block always calls `setBusy(btn, false)`.

**Pseudocode (submit).**
```
on submit: stop the normal form send
values = collectAndValidate(form, fields)    // trims, checks, shows errors, focuses first bad field
if values is null: stop
disable button
try:   data = await api(url, 'POST', body);  form.reset();  show success toast
catch: showServerError(form, err)
finally: enable button
```

**Cost.** A few string checks, tiny. **Why / alternative.** Mirrored checks give fast feedback; server checks give safety. Alternative: rely only on the server (slower feedback), or only on the browser (insecure).

### 4.5 SVG graph layout and highlighting (the deep one)

**Idea.** `GET /api/graph` returns `patients[]`, `donors[]` and `edges[]` (pairs of ids). I draw an
**SVG** (a vector picture made of tags like `<circle>`, `<line>`, `<text>`) with patients in a left column and
donors in a right column. Each edge becomes one `<line>` from a patient circle to a donor circle. Think of
two rows of seats facing each other with strings between matching people.

**Constants (real).** `ROW = 34, TOP = 56, W = 860, XL = 320, XR = 540, R = 8`
- `ROW`: vertical space per vertex (px). `TOP`: space for the column titles. `XL`, `XR`: x position of the left
  and right circles. `R`: circle radius.
- Height: `H = TOP + rows * ROW + 16`, where `rows = max(patients, donors, 1)`.

**Position formula.** `y = TOP + index * ROW + ROW / 2` (the middle of that vertex's row slot).

**Worked example (3 patients, 3 donors, made-up ids).**
- `rows = 3`, so `H = 56 + 3*34 + 16 = 174`.
- Patient index 0 (id 1): `y = 56 + 0*34 + 17 = 73`. Index 1: `y = 107`. Index 2: `y = 141`.
- Donor index 1 (id 102): `y = 107`.
- Edge patient 1 to donor 102: `x1 = XL + R = 328`, `y1 = 73`, `x2 = XR - R = 532`, `y2 = 107`.
- Label of the left column is written ending at `x = XL - R - 10 = 302`; the right column starts at `x = XR + R + 10 = 558`.
- With the original sample data (8 patients, 12 donors, 29 edges): `rows = 12`, `H = 56 + 408 + 16 = 480` px, 29 lines.
- With the enlarged data (120 patients, 200 donors): `rows = 200`, `H = 56 + 6800 + 16 = 6872` px (calculated from the formula, not measured).

**Why ids and not array positions?** Edges say `patientId` and `donorId`. I build two lookup objects, `pPos[id]` and `dPos[id]`,
that map an id to its y position, so the line for an edge is found directly.

**Real code (trimmed).**
```js
patients.forEach(function (p, i) { pPos[p.id] = TOP + i * ROW + ROW / 2; pRec[p.id] = p; });
donors.forEach(function (d, i) { dPos[d.id] = TOP + i * ROW + ROW / 2; dRec[d.id] = d; });
```
```js
const isMatched = !!matched[e.patientId + ':' + e.donorId];
const line = svgEl('line', {
    x1: XL + R, y1: pPos[e.patientId], x2: XR - R, y2: dPos[e.donorId],
    class: isMatched ? 'edge edge-matched' : 'edge'
    // (the code continues with a <title> tooltip child)
```

**Vertex labels and shapes.** Each vertex is a `<g>` (group) containing a `<title>` (tooltip), a `<circle>` and a `<text>`.
The text is `#<id> <name truncated to 18 chars> (<blood group>), degree <n>`; for a donor who is not available it
adds `, unavailable`. **Unavailable donors** are drawn with the class `node-off`: a **white (hollow) circle with a dashed outline**,
plus the word "unavailable" in the label. So the meaning is carried by shape and text, not by colour alone.
(The brief called this a "dashed ring". In my code the dashed part is the circle's own outline; the separate **`ring`** circle is used for *matched* vertices, see 4.6.)

**Hover / focus highlight.** Every vertex is focusable (`tabindex="0"`). On `mouseenter` or `focus`, `highlight(key)` runs.
Before drawing I built `edgesByKey`: for each vertex key (`'p1'`, `'d102'`) a list of its line elements. So highlighting
vertex `d102` touches only its own lines, not all of them.

```js
function highlight(key) {
    clearHighlight();
    svg.classList.add('is-highlighting');
    vertexByKey[key].classList.add('hl');
    lit.push(vertexByKey[key]);
    (edgesByKey[key] || []).forEach(function (item) {
        item.line.classList.add('hl');
        edgeLayer.appendChild(item.line);   // re-appending moves it above the other lines
        lit.push(item.line);
```
CSS then fades every edge that is not highlighted: `.graph-svg.is-highlighting .edge:not(.hl) { opacity: 0.08; }`.
`lit` remembers which elements were changed so `clearHighlight()` can undo exactly those.

**Scrolling.** The `<svg>` has a fixed pixel size (`width` and `height` equal to the `viewBox`), and it sits inside
`div.graph-scroll` (`overflow: auto; max-height: 72vh`). Big graphs scroll instead of being squashed.

**Pseudocode.**
```
rows = max(P, D, 1);  H = TOP + rows*ROW + 16
for i, patient: pPos[patient.id] = TOP + i*ROW + ROW/2
for i, donor:   dPos[donor.id]   = TOP + i*ROW + ROW/2
for each edge e in data.edges:
    make <line> from (XL+R, pPos[e.patientId]) to (XR-R, dPos[e.donorId])
    remember it under key 'p'+patientId and key 'd'+donorId
for each vertex: make <g> with circle, text, tooltip; add hover/focus handlers
```

**Cost.** Time and DOM size: O(P + D + E) - one circle/text group per vertex, one `<line>` **and one `<title>`** per edge.
Highlighting a vertex costs O(its degree). Memory: the lookup objects hold P + D + 2E small entries.

**Why / alternative.** Two columns with straight lines is the standard way to draw a *bipartite* graph (two groups, edges only between groups) and is easy to read.
Alternatives: HTML `<canvas>` (much faster for thousands of edges, but no per-element tooltips or keyboard focus), or a force-directed layout (needs a library).

### 4.6 The matching overlay (thick red on grey)

**Idea.** `POST /api/matching` returns `pairs` but **not** all edges. So `runMatching` makes **two** calls:
first the matching, then `GET /api/graph` (same city) for all edges and all vertices. Then it draws the graph
and gives the *matched* edges a different class.

```js
const result = await api('/api/matching', 'POST', city ? { city: city } : undefined);
// The matching reply lists pairs only, so the full edge set comes from /api/graph.
const graph = await api('/api/graph' + (city ? '?city=' + encodeURIComponent(city) : ''));
```

To know quickly whether an edge is matched I build a lookup object from `pairs`:
```js
const matched = {};
const matchedP = {}, matchedD = {};
list(pairs).forEach(function (pr) {
    matched[pr.patientId + ':' + pr.donorId] = true;
    matchedP[pr.patientId] = true;
    matchedD[pr.donorId] = true;
});
```

**Worked example (original data).** C says patient 1 is paired with donor 102 and patient 2 with donor 101.
`matched["1:102"] = true`, `matched["2:101"] = true`. Edge (1,102): `matched["1:102"]` is true, so it gets
class `edge edge-matched` (4 px, deep red). Edge (1,101): no entry, so it stays a thin grey `edge`.
Matched vertices also get an extra circle of class `ring` (radius R + 4, red outline).
Grey edges are appended to the SVG first and matched edges last, so the red lines sit on top.

**This is not computing the matching.** The set only *remembers what C returned*.

**Cost.** Building the lookup: O(pairs). Checking each edge: O(1), so O(E) total. **Alternative:** a nested loop that compares every edge with every pair would be O(E x pairs); with about 10,000 edges and 120 pairs that is over a million comparisons.

### 4.7 Client-side filtering, tally and the city dropdown

These are the only "calculations" in my code. They never decide compatibility.

- **Filtering the directory** (`renderDonorTable`): `donorState.all.filter(...)` keeps rows that match the text box, the blood-group dropdown and the availability dropdown. It runs again on every keystroke, on the list already fetched (no new request).
```js
const rows = donorState.all.filter(function (d) {
    if (bg && d.bloodGroup !== bg) { return false; }
    if (av === 'yes' && !d.available) { return false; }
    if (av === 'no' && d.available) { return false; }
```
  Example: 200 donors, the user picks "O+" and "Available": the loop looks at 200 rows and keeps those with both. Cost O(n) per keystroke.
  *There is no sorting of tables in my code* (the brief mentioned "filtering/sorting"; I only implemented filtering).
- **Blood-group tally** (`renderDistribution`): count how many donors and patients have each of the 8 groups, so a table with small bars can be drawn.
```js
const dc = {}, pc = {};
BLOOD_GROUPS.forEach(function (bg) { dc[bg] = 0; pc[bg] = 0; });
donors.forEach(function (d) { if (d.bloodGroup in dc) { dc[d.bloodGroup] += 1; } });
patients.forEach(function (p) { if (p.bloodGroup in pc) { pc[p.bloodGroup] += 1; } });
```
  Example: donors `O+, O+, A-` give `dc['O+'] = 2`, `dc['A-'] = 1`. Bar width = `count / max * 100` percent, where `max` is the biggest count in the table. This only **counts** records; it never asks "who can donate to whom".
- **City dropdown** (`populateCitySelect`): collect each donor's city once (case-insensitive), sort with `localeCompare`, and rebuild the `<select>`. Example: `Pune, pune, Nagpur` gives `Nagpur, Pune` (first spelling kept).
  *Tiny known edge case:* `seen` is a plain `{}` object, so a city literally named `constructor` or `toString` would be wrongly skipped. Using `new Set()` would fix it; I did not change it.

**Alternative for filtering:** ask the server (`/api/search`-style) for each filter; simpler JS but more requests.

### 4.8 Adjacency matrix and degrees tables

**Idea.** The *adjacency matrix* is a table: one row per patient, one column per donor, a 1 where an edge exists.
`GET /api/graph/matrix` gives `patientIds`, `donorIds`, `matrix`, `rowSums`, `columnSums`. `renderMatrix` just lays them out as an HTML `<table>`:
header row = donor ids (with a visually hidden word "Donor" for screen readers), each row starts with a `<th scope="row">` patient id, a "Row sum" column at the end and a "Column sum" footer row (`<tfoot>`).

**Worked example (the spec's sample).** `matrix = [[1,1,0],[1,0,0],[0,1,1]]`, `rowSums = [2,1,2]`, `columnSums = [2,2,1]`.
Both sums add to 5, which is the edge count. That is the **handshake rule**: every edge is counted once on a patient and once on a donor.
`renderDegrees` shows the two degree tables and the line "both sums should equal the number of connections" using
`patientDegreeSum`, `donorDegreeSum` and `edgeCount` as sent by C (my code never adds them up).

**Cost.** One table cell per matrix entry: P x D cells. With the enlarged data that is 120 x 200 = 24,000 cells (VERIFY how fast it renders).
**Alternative:** show only the 1s as a list (much smaller), but a matrix is what the course teaches.

---

## 5. Memory and pointers in my part

JavaScript has **no `malloc`, `calloc`, `realloc` or `free`** and no pointers in my code. The browser's *garbage collector* frees objects that nothing refers to any more. So there is no double free, no use-after-free and no allocation I could forget to check.

What does hold data in my code (all inside the one big function, so none of it is global):

| Variable | Holds | Replaced when |
|---|---|---|
| `donorState.all`, `patientState.all` | the last fetched lists, so filtering needs no new request | every `loadDonors()` / `loadPatients()` |
| `editingDonor` | the donor object shown in the edit dialog | each `openEdit` |
| `bannerRetry` | the function the Retry button should call | each `showBanner` |
| `pending`, `navToken`, `graphSeq` | counters (requests in flight, screen number, graph request number) | change as the app runs |
| inside `buildGraphSvg`: `edgesByKey`, `vertexByKey`, `lit` | lookup tables for highlighting | the whole closure is dropped when the SVG is replaced |

**Do I "cache" results as truth?** The directory lists are held only to filter them and are refreshed from the API after every change. The matching view does **not** keep its result: `loadMatchingView` clears the results each time you enter the page, and `runMatching` always calls the API again.

**If an allocation fails in C?** The spec says C answers `OUT_OF_MEMORY` with HTTP 500. My code shows the server's message in the banner (or in the form). I cannot test an out-of-memory case from the browser.

**Link with the C ownership rules.** The spec says a graph result is valid only while the store is not modified, and that matrix rows follow array positions *at build time*. That is why my graph code matches vertices by **id**, not by position.
(My matrix code does use position `i` to pair `patientIds[i]` with `matrix[i]` and `rowSums[i]`, which is correct because they come from the same single reply.)

**Common beginner mistakes (JS side).**
1. Writing record text with `innerHTML` (XSS).
2. Forgetting `finally`, so a failed request leaves the button disabled forever.
3. Forgetting to empty a container (`clear(host)`) before redrawing, so rows pile up.
4. Using `form.id` or `form.name` when the form has inputs with those names (3.4).

---

## 6. Validation and error handling in my part

**In the browser** (friendly, fast): see the table in 4.4. A failed check shows a message under the field, sets `aria-invalid="true"`, and focuses the first bad field.

**In the server** (authoritative). Spec section I:

| Server code | HTTP | Where my page shows it |
|---|---|---|
| `INVALID_ID`, `DUPLICATE_ID` | 400, 409 | under the ID field |
| `INVALID_NAME` | 400 | under the name field |
| `INVALID_BLOOD_GROUP` | 400 | under the blood group dropdown |
| `INVALID_CITY` | 400 | under the city field |
| `INVALID_PHONE` | 400 | under the phone field |
| `INVALID_AVAILABILITY` | 400 | under the availability switch |
| `NOT_FOUND` | 404 | form message / toast / banner (depends on the action) |
| `BAD_REQUEST`, `METHOD_NOT_ALLOWED`, `PAYLOAD_TOO_LARGE` | 400, 405, 413 | same |
| `OUT_OF_MEMORY`, `INTERNAL_ERROR` | 500 | same |

My code does **not** branch on the HTTP number. The `ok` flag and `error.code` decide everything; `status` is stored on the error object and used only in fallback messages such as `Request failed (HTTP 502).`

**Where each kind of failure is displayed.**
- *Form submit* (register, edit dialog): field message or form-level message (`role="alert"`).
- *Button actions* (toggle availability): an error toast.
- *Loading or running a view* (dashboard, directory, search, graph, matching): the red banner with **Retry** (it calls the same loader again) and Dismiss.

**Order of server checks (spec):** id, name, bloodGroup, city, phone, available, duplicate id. A duplicate is only reported when everything else is valid - so to demonstrate `DUPLICATE_ID`, fill all other fields correctly.

**Search page.** The spec says a missing patient gives `400 INVALID_ID`. My page prevents that: if no patient is selected it shows "Choose a patient first." and sends nothing.

---

## 7. How I coordinated with the other developers

**The only contract was the Common Technical Specification.** I never spoke to Developers 1-4; the human team member carried messages. For me the relevant parts were sections A, B, G, H and I (JSON formats, business rules, status table). Section F (C headers) was irrelevant to JavaScript.

**What I relied on (field names must match what the others emit).**
- `/api/stats`: `donorCount`, `availableDonorCount`, `unavailableDonorCount`, `patientCount`, `edgeCount`, `maxMatching`, `unmatchedPatientCount` (Dev 4).
- `/api/donors`, `/api/patients`: `donors[]` (`id,name,bloodGroup,city,phone,available`), `patients[]` (`id,name,bloodGroup,city`) (Dev 1). `available` must be a JSON boolean.
- POST replies: `donor.id`, `donor.name` / `patient.id`, `patient.name`; availability reply: `donor.available` (Dev 1).
- `/api/search`: `patient`, `filters.city`, `filters.availableOnly`, `count`, `donors[]` (Dev 2).
- `/api/graph`: `cityFilter`, `numPatients`, `numDonors`, `edgeCount`, `patients[].degree`, `donors[].degree`, `donors[].available`, `edges[].patientId`, `edges[].donorId` (Dev 3).
- `/api/graph/matrix`: `cityFilter`, `patientIds`, `donorIds`, `matrix`, `rowSums`, `columnSums` (Dev 3).
- `/api/graph/degrees`: `cityFilter`, `patients[].degree`, `donors[].degree`, `edgeCount`, `patientDegreeSum`, `donorDegreeSum` (Dev 3).
- `/api/matching`: `cityFilter`, `matchCount`, `numPatients`, `numDonors`, `pairs[].patientId/patientName/donorId/donorName`, `unmatchedPatients[]`, `unmatchedDonors[]` (Dev 4).
- Error envelope: `error.code`, `error.message`, and the code strings in spec section I.
- Static file serving from `frontend/` with correct types for `.html`, `.css`, `.js` (Dev 1).

**What the others rely on from me:** nothing in C. The only "contract" in that direction is that my files are plain static files with relative addresses.

**Evidence that the contract worked.** The team's `curl` checks on the original sample data (reported to me by the team; I have not seen the raw JSON myself) returned stats, a matrix with `rowSums` and `columnSums`, and a matching reply with pairs, consistent with the fields I read. **The full browser click-through results were not reported to me: VERIFY.** The fields of `/api/graph`, `/api/graph/degrees`, `/api/search`, the list endpoints and the POST/PUT replies were not part of the curl list I was given: VERIFY them in the browser.

**Problems that came up.**
1. **`/api/matching` has no edge list.** To draw non-matched grey edges I had to call `GET /api/graph` as well. I told the orchestrator in my report ("PROPOSED SPEC CHANGE: none required. Optionally, adding `edges` to the `/api/matching` reply would save the second request"). No formal change was filed, and none was needed.
2. **`file://` problem (team incident 4).** The page cannot work when `index.html` is double-clicked. The page and its CSS and JS load, but every `fetch('/api/...')` fails, so my "Cannot reach the C backend. Is sbdf.exe running?" message appears (expected browser behaviour: VERIFY the exact message you see). The fix is not in code: open `http://localhost:8080` (the orchestrator's `start.bat` does build + start + open browser).
3. **Enlarged sample data (team incident 5).** The orchestrator enlarged `sample_data.c` to 200 donors / 120 patients after my delivery. I designed the graph for small examples and did not test large ones (see sections 4.5 and 9).
4. **Incidents 1, 2 and 3** (Dev 3's missing spec paste, missing `.h` files, VS Code false errors) did not affect my files, because JavaScript has no headers or include paths. They are good "what we learned" points about the spec: the single written contract made it possible to work in parallel.

**What I would do differently.** (a) Write a small mock of the JSON replies and click through every page *before* delivery. (b) Test the graph page with 100+ vertices from the start. (c) Ask for an `edges` list in the matching reply, or draw the matching on a smaller graph. (d) Add a one-line "how to open the app" notice on the page for the `file://` case.

---

## 8. Testing

**What I wrote:** no automated tests. In my delivery report I listed a **manual checklist of 9 checks** (backend stopped; empty store; duplicate ID; client validation; toggle and edit; search variants; graph hover and matrix sums; matching with/without city; narrow window and keyboard use).

**What I actually ran:** **nothing. NOT RUN.** I had no browser, no Node and no backend. I did not run `node --check` or any linter.

**What the team verified (relevant to me):** the build succeeded and `sbdf.exe --demo` started; the `curl` checks listed in the project facts (stats, matrix, matching, 404s, `INVALID_NAME`) behaved as expected on the **original** sample data.

**NOT verified:** the browser click-through of every page; that the page really looks and behaves as designed; screen-reader behaviour; graph speed with the enlarged data; every JSON field name in section 7 that was not in the curl list.

### 5-step LIVE DEMO script

*I have not run this script. The "expected" values for the original data come from the team's curl results and from reasoning with the edge rule; the enlarged-data values are the orchestrator's calculation (expected, not measured). Use whichever sample data your `sample_data.c` currently holds.*

**Step 1 - Start and dashboard.**
Run `start.bat` (or `sbdf.exe --demo`), then open **http://localhost:8080** (never double-click `index.html`). Click **Dashboard**.

| Card | Original data (12/8) | Enlarged data (200/120) |
|---|---|---|
| Registered donors | 12 (note: "1 currently unavailable") | 200 (expected: "22 currently unavailable") |
| Available donors | 11 | 178 (expected) |
| Registered patients | 8 | 120 (expected) |
| Potential compatible connections | 29 | 10,339 (expected) |
| Maximum matching | 7 | 120 (expected) |
| Unmatched patients | 1 | 0 (expected) |

*(22 unavailable = 200 - 178, by arithmetic.)*

**Step 2 - Register a donor, then the same ID again.**
Click **Register donor**. Enter ID `999`, name `Demo Donor`, blood group **O-**, city (any, e.g. `Amravati`), phone `9000000999`, switch left on "Available". Click **Register donor**. Expect a toast `Donor 999 (Demo Donor) registered.` and an empty form.
Submit the same values again. Expect an error under the **ID field** (HTTP 409, code `DUPLICATE_ID`; the exact message text is written by the C side: VERIFY). Open the browser's Network tab to show the status code.

**Step 3 - Watch the dashboard change.**
Go to **Dashboard**. An available O- donor can donate to all 8 blood groups (the O- column of the compatibility table is all 1s), and the dashboard counts edges with no city filter. By the edge rule, expected:
- Original data: donors 13, available 12, connections 29 + 8 = **37**, maximum matching **8**, unmatched patients **0** (the one unmatched patient can take the new donor directly).
- Enlarged data: donors 201, available 179, connections 10,339 + 120 = **10,459**, matching stays 120, unmatched 0.
Then go to **Donors**, find 999 and click **Mark unavailable**. Expect the toast `Donor 999 is now unavailable.` and the badge "Unavailable". Back on the dashboard the numbers should return to the Step 1 values for connections and matching (donor count stays one higher).

**Step 4 - Search.**
Click **Find donors**, choose a patient, leave "Available donors only" ticked, click **Find donors**. Expect a table of donors with blood group, city, phone and an "Available" badge, and the disclaimer text visible on the page. Then untick the box: unavailable donors (badge "Unavailable") may appear. To show the empty state, use a filter that leaves nobody (for example a rare blood group patient plus a small city) and expect "No compatible donors found". (Exact patients to use: VERIFY.)

**Step 5 - Graph and matching.**
Click **Graph**. On the original data you should see 8 left vertices, 12 right vertices and 29 lines; hover a vertex to highlight its lines; the hollow dashed circle is the unavailable donor. Open the **Adjacency matrix** tab (row sums and column sums should each add to 29) and the **Degrees** tab.
On the enlarged data **choose a city first** (e.g. Nagpur) - the unfiltered graph has about 10,339 lines (see section 9).
Click **Matching**, optionally choose a city, click **Run maximum matching**. Expect `Pairs matched`: 7 (original, no city) or, on the enlarged data, 35 for Nagpur (expected, not measured); matched lines thick red, ringed vertices, then the pairs table and the two "unmatched" tables. By arithmetic, original data should show 5 unmatched donors (12 - 7) and 1 unmatched patient.

---

## 9. Limitations and known risks

1. **Never run in a browser by me.** Typos, wrong field names or CSS problems may exist. This is the biggest risk.
2. **Performance with the enlarged data (not measured: VERIFY).**
   - Unfiltered graph: expected about 10,339 edges = 10,339 `<line>` elements **plus** 10,339 `<title>` tooltips, plus about 320 vertex groups, so well over 20,000 DOM nodes. The SVG is about 6,872 px tall. It may load slowly and hovering may feel laggy, because the highlight rule changes how every line is drawn.
   - A city filter reduces edges. The orchestrator's pair counts per city (Nagpur 35, Pune 37, Nashik 39, Mumbai 31, Amravati 36) are *matching pairs*, not edges. The brief's estimate of "about 2,000 edges per city" is roughly 10,339 / 5 and is **not measured**. Note that a city filter still draws all patients and *all donors* as vertices (all patients stay vertices per spec; I draw the donors list as returned).
   - The matching view does the same drawing, and the matrix tab builds P x D cells (24,000 with the enlarged data).
   - No pagination, no virtual scrolling, no debounce on the search boxes.
3. **Keyboard use of big graphs.** Every vertex is a tab stop, so about 320 Tab presses to cross the enlarged graph. Vertices use `role="img"` while being focusable, which is not a perfect semantic fit.
4. **Highlight details.** Highlighted lines are re-appended (moved to the top of the SVG), so after hovering, line order changes; `mouseleave` clears the highlight even if the vertex still has keyboard focus.
5. **No table sorting and no pagination.** Only filtering.
6. **Fixed graph size.** The SVG is 860 px wide; labels are cut to 18 characters (full text in the tooltip and `aria-label`).
7. **Browser support.** Needs a modern browser (`fetch`, `async/await`, `TextEncoder`, `<dialog>`). If `showModal` is missing the Edit button shows an error toast instead.
8. **Two calls for the matching view.** If someone changes the data between the two calls, the picture could be slightly inconsistent.
9. **Assumption about phone length** (the `+` counts) and the tiny `constructor` city edge case (4.7).
10. **Security.** Record text is rendered safely, but I did **not** perform a security audit. The server binds to 127.0.0.1 (spec), and there is no login. Data lives in memory only, so restarting `sbdf.exe` loses everything that was registered.
11. **Not tested:** the `share.bat` tunnel scenario, screen readers, Safari/Firefox differences.

---

## 10. THE PRESENTATION SCRIPT

### 4-minute version (about 600 words)

[SHOW: the dashboard at http://localhost:8080]

Hello everyone. I built the web interface of Smart Blood Donor Finder. It is the part you can see and click.

Let me start with the golden rule of my part: **JavaScript displays, C computes.** Compatibility, degrees and the maximum matching are all calculated by our C backend. My three files - index.html, style.css and script.js - only ask for results and draw them. The only things my JavaScript does itself are small display jobs: filtering a table that is already on screen, counting records for the blood-group summary, and filling dropdowns.

[SHOW: the address bar]

This is a single page. The menu links look like hash-hyphen-graph or hash-matching. When the part after the hash changes, a small function called route() shows one section and hides the other seven. No page reload. Also look at the address: it must be localhost 8080. My code calls relative addresses such as slash api slash stats, so the page has to come from our C server. If you double-click index.html, the page opens but every request fails, and you see "Cannot reach the C backend. Is sbdf.exe running?"

[SHOW: api() in script.js]

All requests go through one helper, api(). It uses fetch with async and await. Our C server always answers in the same envelope: ok true with data, or ok false with an error code and message. api() opens the envelope and returns the data, or throws an error carrying the server's code and message. So the rest of my code never deals with raw HTTP.

Now let me follow **one click from start to finish.** [DEMO: open Matching, pick a city, click Run maximum matching]

I click "Run maximum matching". The button disables itself, so nobody can click twice, and a loading pill appears. runMatching() calls api() with POST slash api slash matching, and the city in the body. The browser sends an HTTP request to our C server. Developer 1's server, built on Winsock, hands it to the router, which calls api_post_matching, written by Developer 4. That C handler builds the graph, runs the augmenting-path algorithm, and writes the JSON: matchCount, the pairs, and the unmatched lists.

[SHOW: the JSON in the browser's Network tab, optional]

The JSON travels back. api() unwraps it. The reply has pairs but not every edge, so I make a second call, GET slash api slash graph, to get all vertices and all edges.

[SHOW: the graph]

Now I draw an SVG. Patients are in the left column, donors in the right. The y position is TOP plus index times ROW plus ROW over 2. Every edge from the server becomes one line. Grey lines are possible connections; the thick red lines are the pairs C chose, and matched vertices get a ring. A hollow dashed circle is a donor who is unavailable, and the label says so too, so colour is never the only signal.

[SHOW: hover a vertex]

Hover or tab to a vertex and its edges light up. Below the graph are the pairs table and the unmatched patients and donors.

Two safety points. First, names are typed by users, so I never use innerHTML. I use textContent, so a donor named with a script tag is shown as text and cannot run code. That attack is called XSS. Second, the forms check the input in the browser, but the server decides. [DEMO: register an existing ID] If I register an ID that exists, C replies DUPLICATE_ID with status 409, and my code shows the message under the ID field.

Finally, honesty. I have not yet run a full browser click-through, and with the enlarged data the unfiltered graph draws thousands of lines and may be slow. A city filter helps. That is my next test.

Thank you. I am happy to take questions.

### 60-second version (about 170 words)

[SHOW: dashboard]
I built the web interface: three files, index.html, style.css and script.js. It is a single page; the hash in the address, like #graph, decides which section route() shows.

The rule is: JavaScript displays, C computes. One helper, api(), calls the C server with fetch, opens the ok/data or ok/error envelope, and throws an error with the server's code and message. That is why the page must be opened from localhost 8080; double-clicking the file gives "Cannot reach the C backend".

[DEMO: Matching, click Run]
When I click Run, api() sends a POST, C builds the graph and runs the matching, and I draw the answer as an SVG: patients left, donors right, matched edges thick red.

Record text goes in with textContent, never innerHTML, to stop XSS. Forms check first, but the server is the authority.

Limit: I have not yet clicked through every page, and the biggest graphs may be slow.

---

## 11. VIVA Q&A

### A. Questions about my part

**Q1. What is a single-page application?**
It is one HTML page that never reloads; JavaScript shows and hides sections. Mine has 8 sections (views) selected by the hash in the address.

**Q2. How does the router work?**
`route()` reads `location.hash`, hides every `.view` section except `view-<name>`, marks the menu link with `aria-current="page"` and calls that view's loader from the `VIEWS` object. It runs once at start and on every `hashchange`.

**Q3. Why must the page be opened from http://localhost:8080?**
The page calls relative addresses such as `/api/stats`, which only make sense relative to a server. Opened from `file://`, the request goes to a file path, fails, and `api()` shows "Cannot reach the C backend. Is sbdf.exe running?".

**Q4. What does `api()` do?**
It sends the request with `fetch`, parses the JSON envelope and returns `data` when `ok` is true. Otherwise it throws an `Error` with `code`, `message` and `status`, so callers only need `try/catch`.

**Q5. What are the special cases in `api()`?**
Network failure becomes the `NETWORK` error with the "Cannot reach..." message. A reply that is not JSON, not an object, or lacks `data` becomes `BAD_RESPONSE`. Each has its own readable message.

**Q6. What is the `pending` counter for?**
It counts requests in flight. It goes up before each `fetch` and down in a `finally` block, and the "Loading..." pill shows while it is above 0. `finally` guarantees it cannot get stuck.

**Q7. How is `POST /api/matching` sent with no body?**
`api()` only adds a body and `Content-Type` when `payload !== undefined`. `runMatching` passes `undefined` when no city is selected, and the spec treats an empty body as "no filter".

**Q8. Why `textContent` and not `innerHTML`?**
User-typed names could contain HTML or script; `innerHTML` would run it (XSS). `textContent` and text nodes show it as plain characters. My `el()` and `append()` helpers turn every string child into a text node.

**Q9. What does JavaScript compute, and what not?**
It only filters lists already fetched, counts records per blood group for the dashboard table, and builds dropdowns. Compatibility, degrees and the matching come only from C.

**Q10. Why validate in the browser if the server validates?**
The browser check gives instant, friendly messages and saves a request. The server check is the real protection, because anyone can bypass the page with `curl`.

**Q11. How do server errors reach the right field?**
`FIELD_BY_CODE` maps codes such as `INVALID_NAME` or `DUPLICATE_ID` to field names. `showServerError` writes the server's message under that field and focuses it; unmapped errors go to a message at the top of the form.

**Q12. How is a vertex's y position computed?**
`y = TOP + index * ROW + ROW / 2`, with `TOP = 56` and `ROW = 34`. Index 0 gives 73, index 1 gives 107. Patients use `XL = 320`, donors `XR = 540`.

**Q13. How does the hover highlight work?**
When the graph is built I store, for each vertex key like `p1` or `d102`, the list of its line elements (`edgesByKey`). `highlight(key)` adds class `hl` to only those lines and their end vertices, and CSS fades all other edges.

**Q14. How do you show an unavailable donor without relying on colour?**
The circle is hollow with a dashed outline (class `node-off`) and the label ends with ", unavailable". In tables, the badge says "Unavailable" in words, with an empty dot instead of a filled one.

**Q15. How do you draw the matching?**
After `POST /api/matching` I call `GET /api/graph`, build a lookup from the returned `pairs` (`"patientId:donorId"`), and give matching edges the class `edge-matched` (thick red) and their vertices a ring. The choice of pairs was made by C.

**Q16. What does the adjacency matrix tab show?**
Rows are patients, columns are donors, a 1 means an edge, plus a row sum at the end of each row and a column-sum footer. The numbers come from `/api/graph/matrix`; row sums are patient degrees, column sums are donor degrees.

### B. Three tricky questions

**T1. "The matching page draws thick red lines. Isn't your JavaScript running the matching?"**
*Trap:* confusing *drawing* with *computing*. *Answer:* No. The pairs come from `POST /api/matching`; my code only stores them in a lookup object to know which lines to make thick. If the C function gave a different answer, the picture would change; my JavaScript has no matching algorithm.

**T2. "Your dashboard counts blood groups in JavaScript. Doesn't that break your rule?"**
*Trap:* thinking any arithmetic in JS breaks the rule. *Answer:* The rule is about compatibility, degrees and matching, i.e. the graph logic. Counting how many records have each blood group is just summarising lists the API returned, and it never asks who can donate to whom. The six number cards (edges, matching, unmatched) come straight from `/api/stats`, calculated in C.

**T3. "Your graph can draw 10,000 edges. How fast is it?"**
*Trap:* inventing a number. *Answer:* I have not measured it. By construction each edge is a `<line>` plus a `<title>`, so about 20,000 elements for the unfiltered enlarged data, and I expect it to be slow; a city filter reduces the edges. A faster design would use `<canvas>`, and measuring is on my to-do list.

*(Bonus trap, if asked "Is your page secure against XSS?":* record text is inserted with `textContent` and text nodes, so I expect it to be safe in that path, but I have not done a formal security audit, and `textContent` does nothing for server-side validation.)*

### C. Five questions about OTHER modules

1. **How does the system decide that a donor can give blood to a patient?** A fixed 8x8 table `COMPAT` (27 of 64 combinations are 1) looked up by blood-group index in `compat_can_donate`. *Hand over to: Developer 2 (compatibility).*
2. **What is an edge in the graph?** `compat_is_edge`: blood group compatible AND donor available AND donor city matches the optional filter. *Hand over to: Developer 2 (rule) and Developer 3 (graph_build uses it).*
3. **How does the maximum matching work?** Kuhn's algorithm with a recursive DFS that finds augmenting paths, like moving patient 1 to donor 102 so patient 2 can take donor 101. *Hand over to: Developer 4 (`matching_compute`).*
4. **Why does Winsock appear only in `http_server.c`?** To keep all platform-specific networking in one file so every other module is plain standard C. *Hand over to: Developer 1.*
5. **Who frees the graph and matching result, and why must `records_find_*` pointers not be kept?** Both are local to a request handler (init, use, free); `records_find_*` pointers become invalid after the next add because `realloc` may move the array. *Hand over to: Developer 3 (graph) and Developer 1 (records).*

---

## 12. Glossary

1. **Single-page app (SPA):** one HTML page whose content changes with JavaScript instead of loading new pages.
2. **Hash (fragment):** the part of the address after `#`; changing it fires `hashchange` without reloading.
3. **DOM:** the browser's tree of page elements that JavaScript can create, change and remove.
4. **`fetch`:** the browser function that sends an HTTP request and returns a promise for the reply.
5. **`async` / `await`:** syntax that lets code wait for a request to finish while still reading like normal top-to-bottom code.
6. **Envelope:** the fixed wrapper `{"ok":..., "data":...}` or `{"ok":false,"error":{...}}` around every API reply.
7. **XSS (cross-site scripting):** an attack where user-supplied text is treated as HTML or script and runs in other people's browsers.
8. **SVG:** a text-based vector image format; I create `<circle>`, `<line>` and `<text>` elements with JavaScript to draw the graph.
9. **Adjacency matrix:** a table with one row per patient and one column per donor, with 1 where an edge exists.
10. **Degree:** the number of edges at a vertex; the row sum is a patient's degree, the column sum is a donor's degree.

---

## 13. Cheat sheet (memorise these 10)

1. **Eight views:** `#dashboard #donor-register #patient-register #donors #patients #search #graph #matching`; `route()` shows one.
2. **Open `http://localhost:8080`**, never `file://`; the code uses relative URLs like `/api/stats`.
3. **`api(path, method, payload)`** returns `envelope.data` or throws `Error{code, message, status}`; network failure message: *"Cannot reach the C backend. Is sbdf.exe running?"*
4. **Golden rule:** JavaScript displays, C computes. JS only filters fetched lists, counts records for the blood-group table, and builds dropdowns.
5. **Never `innerHTML`** for records: `el()` / `append()` make text nodes, `textContent` for the rest.
6. **12 API calls** (stats, donors GET, patients GET, donors POST, patients POST, donor PUT, availability PUT, search, graph, matrix, degrees, matching).
7. **Layout constants:** `ROW = 34, TOP = 56, W = 860, XL = 320, XR = 540, R = 8`; `y = TOP + i*ROW + ROW/2`; one `<line>` per edge.
8. **Matching view = 2 calls:** `POST /api/matching` then `GET /api/graph`; matched edges thick red, matched vertices ringed, unavailable donors hollow and dashed.
9. **Client rules:** id 1..2147483647; name at most 49 bytes; city at most 29 bytes; phone 7..14 chars with optional `+`; **server is authoritative** (`DUPLICATE_ID` is 409, other `INVALID_*` are 400).
10. **Numbers:** original data expected/verified-by-curl: 12 donors, 11 available, 8 patients, 29 edges, matching 7, 1 unmatched. Enlarged data (expected, not measured): 200, 178, 120, 10,339, 120, 0. 27 of 64 blood-group pairs are compatible.

---

## 14. Handover notes (five sentences a teammate can say)

1. "The frontend is one page with eight sections that are switched by the hash in the address, and it must be opened from http://localhost:8080 because it calls relative API addresses."
2. "Every request goes through one helper, `api()`, which unwraps the `{ok, data}` envelope and turns errors into exceptions with the server's code and message."
3. "JavaScript never calculates compatibility, degrees or matchings; it only displays what the C backend returns, apart from filtering lists and counting records."
4. "The graph is an SVG with patients on the left, donors on the right, one line per edge, and the matching view draws C's chosen pairs in thick red on top of the grey edges."
5. "Record text is always inserted with textContent to prevent XSS, the browser validates forms for convenience, and the server's error code is mapped to the right field because the server is the authority."

---

## VERIFY BEFORE PRESENTING

1. **Nothing in my files has been run.** Open every page in a real browser and check the console for errors (menu, both forms, directories, edit dialog, search, three graph tabs, matching).
2. Approximate line counts in section 0 (`wc -l frontend/*`).
3. That `script.js` still contains no `innerHTML` (search the file).
4. Exact C error message texts (e.g. for `DUPLICATE_ID`) and that the field names in section 7 match what `api_*.c` emit, especially `/api/graph`, `/api/graph/degrees`, `/api/search`, list endpoints, and POST/PUT replies (only stats, matrix and matching were reported as matching the curl output).
4b. Handler names in the section 2 table (taken from `api.h` in the spec).
5. The browser message and behaviour when `index.html` is opened via `file://` (expected: page loads, every request fails, banner shows "Cannot reach the C backend...").
6. Phone length rule: whether the `+` counts inside the 7-14 characters (`records.c`).
7. Whether the C server answers parallel requests concurrently or one at a time (affects the dashboard's three simultaneous calls).
8. Which sample data the running app uses (original 12/8 or enlarged 200/120), and therefore which column of the Step 1 table to expect.
9. All **expected** values: 22 unavailable (200 - 178), the Step 3 predictions (original 37 connections / matching 8 / 0 unmatched; enlarged 10,459), 5 unmatched donors on the original data after matching, the per-city pair counts, and that donor ID 999 is unused.
10. Graph performance with the enlarged data (unfiltered about 10,339 lines; city-filtered about 2,000 is an unmeasured estimate), page height of about 6,872 px, matrix rendering time for 120 x 200 cells.
11. Which patients and city give an empty search result in your demo data.
12. Browser compatibility (`<dialog>`, `TextEncoder`) on the presentation machine, and keyboard behaviour of the tabs and graph vertices.
13. That nobody changed `index.html`, `style.css` or `script.js` after my delivery (if they did, this handbook may be out of date).
