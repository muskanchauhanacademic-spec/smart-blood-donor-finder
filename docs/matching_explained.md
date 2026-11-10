# Matching explained (for beginners)

This note explains the idea behind `backend/matching.c`: how the Smart Blood
Donor Finder decides which donor can be given to which patient so that **as
many patients as possible** are served and **no donor is used twice**.

The program only shows *potential red blood cell compatibility* from fixed
blood group rules. It is **not** medical approval; real donation needs
screening and cross-matching by a qualified blood bank.

---

## 1. Bipartite graphs

A **graph** is a set of **vertices** (dots) joined by **edges** (lines).

A graph is **bipartite** when the vertices can be split into two groups and
every edge goes from one group to the other, never inside a group.

In this project:

* group 1 = **patients**
* group 2 = **donors**
* an edge `patient -- donor` means "this donor can give red blood cells to this
  patient right now".

Why bipartite? A patient is never connected to another patient, and a donor is
never connected to another donor. Only patient-donor lines exist.

The single rule that creates an edge (written once in `compat_is_edge`):

> blood group is compatible **and** the donor is available **and** the donor's
> city matches the city filter (if there is one).

---

## 2. Adjacency matrix

We store the graph as a table with one **row per patient** and one **column per
donor**. A `1` in row *i*, column *j* means an edge between patient *i* and
donor *j*; a `0` means no edge.

Example used in this whole document (3 patients, 3 donors):

```
          D1  D2  D3
    P1  [  1   1   0 ]     P1 can use D1 or D2
    P2  [  1   0   0 ]     P2 can use only D1
    P3  [  0   1   1 ]     P3 can use D2 or D3
```

In C this is `int **adj` and the test `adj[row][col] == 1`. The matrix is built
by `graph.c`; `matching.c` only **reads** it.

---

## 3. Degrees

The **degree** of a vertex is the number of edges touching it.

* patient degree = **sum of its row**   (P1: 2, P2: 1, P3: 2)
* donor degree   = **sum of its column** (D1: 2, D2: 2, D3: 1)

Handy facts:

* The sum of all patient degrees = the sum of all donor degrees = number of
  edges (5 in the example). Every edge is counted once from each side.
* A patient with degree 0 can never be matched.
* A donor with degree 0 (for example an unavailable donor, or an AB+ donor when
  no AB+ patient exists) stays unmatched.
* Degree is **not** the answer to "how many pairs?". Three patients may all
  have degree 1 and still all want the same donor.

---

## 4. Depth-first search (DFS)

DFS explores a graph by going **as deep as possible** along one route, and only
when it hits a dead end does it **back up** and try the next option. It is
naturally written with **recursion**: a function calls itself on the next
vertex, and "backing up" is simply the function returning.

To avoid walking in circles, DFS remembers which vertices it has already
visited (`visited[]`).

---

## 5. Matching and maximum matching

A **matching** is a set of edges where **no vertex appears twice**: each
patient has at most one donor, each donor serves at most one patient.

A **maximum matching** is a matching with the largest possible number of edges.
(It is not the same as a *maximal* matching, which only means "you cannot add
another edge without changing the others".)

Why not just be greedy (give every patient the first free donor)? Look at the
example:

| Patient | Greedy choice | Result |
|---------|---------------|--------|
| P1 | first free donor is D1 | P1-D1 |
| P2 | only D1, but it is taken | **no donor** |
| P3 | first free donor is D2 | P3-D2 |

Greedy finds **2** pairs, but **3** are possible (P1-D2, P2-D1, P3-D3). To get
there P1 must be asked to *move*. That idea is the augmenting path.

---

## 6. Augmenting paths

Words first:

* **matched edge**: an edge currently in the matching;
* **free donor**: a donor with no patient yet;
* **unmatched patient**: a patient with no donor yet.

An **augmenting path** is a path that

1. starts at an unmatched patient,
2. ends at a free donor,
3. alternates: *non-matching edge, matching edge, non-matching edge, ...*

Example found for P2 (while P1-D1 is in the matching):

```
P2 --(not matched)--> D1 --(matched)--> P1 --(not matched)--> D2 (free)
```

**Flip** the path: the non-matching edges become matched and the matched edge
becomes unmatched:

```
before:  P1-D1                 after:  P2-D1, P1-D2
```

The matching grew from 1 edge to 2. Flipping an augmenting path **always**
increases the size by exactly 1, because the path has one more non-matching edge
than matching edges.

**Berge's theorem:** a matching is maximum **if and only if** no augmenting path
exists. So the recipe for a maximum matching is: *keep finding augmenting paths
until none is left.*

---

## 7. Kuhn's algorithm

Kuhn's algorithm finds augmenting paths with DFS, one patient at a time.

```
for each patient u (row 0, 1, 2, ...):
    clear visited[] (one flag per donor)
    if try_augment(u): matchCount++

try_augment(u):
    for each donor v with adj[u][v] == 1 and not visited[v]:
        visited[v] = 1
        if donorMatch[v] == -1  OR  try_augment(donorMatch[v]):
            donorMatch[v] = u          # u takes v
            patientMatch[u] = v
            return 1                   # success
    return 0                           # no augmenting path starts at u
```

Reading `try_augment(u)` in words: *"Which donor could I take? If it is free, I
take it. If somebody else (w) has it, I ask w to move to another donor; if w
can, I take the donor."* The "ask w" part is the recursion.

Arrays used by the code (all 0-based in C, shown 1-based below):

* `patientMatch[u]` = donor column of patient u, or `-1`
* `donorMatch[v]`   = patient row of donor v, or `-1`
* `visited[v]`      = 1 if donor v was already tried **in the current search**

### Why `visited[]` is cleared for every patient

* **Inside one search** `visited[]` stops the recursion from looping forever and
  from exploring the same donor again and again. Each donor is explored at most
  once per search.
* **Between searches** the matching has changed, so a donor that was a dead end
  a moment ago might now be useful. A fresh search needs a fresh `visited[]`.

---

## 8. Trace of the 3x3 example

Matrix (rows P1..P3, columns D1..D3):

```
          D1  D2  D3
    P1  [  1   1   0 ]
    P2  [  1   0   0 ]
    P3  [  0   1   1 ]
```

Start: nobody is matched.

### Step 1: patient P1 (visited cleared: none)

* `try_augment(P1)`: D1 has an edge and is not visited -> mark D1 visited.
* D1 is free -> **P1 takes D1**. Return 1.
* Matching: `P1-D1`. matchCount = 1.

### Step 2: patient P2 (visited cleared again)

* `try_augment(P2)`: D1 has an edge -> mark D1 visited.
* D1 belongs to P1, so call `try_augment(P1)`:
  * D1 is visited -> skip.
  * D2 has an edge, not visited -> mark D2 visited.
  * D2 is free -> **P1 moves to D2**. Return 1.
* Back in P2: D1 is now free -> **P2 takes D1**. Return 1.
* Matching: `P1-D2, P2-D1`. matchCount = 2.
* The augmenting path was `P2 -> D1 -> P1 -> D2`.

### Step 3: patient P3 (visited cleared again)

* `try_augment(P3)`: D1 has no edge -> skip. D2 has an edge -> mark D2 visited.
* D2 belongs to P1, so call `try_augment(P1)`:
  * D1 has an edge, not visited -> mark D1 visited.
  * D1 belongs to P2, so call `try_augment(P2)`:
    * P2's only donor D1 is visited -> nothing left. Return **0**.
  * Back in P1: D2 is visited, D3 has no edge. Return **0**.
* Back in P3: moving P1 failed, so D2 cannot be freed. Try the next donor.
* D3 has an edge, not visited -> mark D3 visited. D3 is free -> **P3 takes D3**.
* Matching: `P1-D2, P2-D1, P3-D3`. matchCount = **3**.

Step 3 shows a *failed* branch: the search asked P1 and P2 to move, they could
not, and the algorithm simply tried the next donor without changing anything.
`visited[]` made sure D1 and D2 were not tried twice.

Final answer: `patientMatch = {D2, D1, D3}` (code: `{1, 0, 2}`),
`donorMatch = {P2, P1, P3}` (code: `{1, 0, 2}`). This is exactly what
`tests/test_matching.c` checks in `test_augmenting_path`.

---

## 9. Cost

Let P = number of patients, D = number of donors.

* One search visits each donor at most once, and every recursive call is caused
  by a newly visited donor, so a search makes at most D + 1 calls. Each call
  scans one row of D cells. One search therefore costs O(D^2) with a matrix.
* There are P searches, so the total is **O(P x D^2)** time.
  (With adjacency *lists* it would be the well-known O(P x E).)
* Extra memory: `patientMatch` (P ints), `donorMatch` (D ints), `visited` (D
  bytes) = O(P + D), besides the matrix itself.
* The recursion is at most about D calls deep. That is fine for classroom data;
  for huge graphs an iterative version would be safer.

---

## 10. How the project uses it

1. The graph is rebuilt from the record store for each request (optionally with
   a city filter on the **donor's** city).
2. `matching_compute` runs Kuhn's algorithm on `adj`.
3. `POST /api/matching` returns the pairs, the unmatched patients and the
   unmatched donors. `GET /api/stats` returns `maxMatching` for the whole
   system (no city filter).
4. A result is only valid until the store changes; the next request recomputes.

The matching is **not unique**: a different row or column order can give
different pairs, but the **number** of pairs is always the maximum.

---

## 11. Ten viva questions

**1. What is a bipartite graph, and why is our donor-patient graph one?**
A graph whose vertices split into two groups with edges only *between* the
groups. Edges only ever join a patient to a donor, never two patients or two
donors.

**2. What is a matching, and what is a maximum matching?**
A matching is a set of edges with no shared vertex (a donor serves one patient,
a patient gets one donor). A maximum matching has the most edges possible.

**3. What is an augmenting path?**
A path from an unmatched patient to a free donor that alternates between
non-matching and matching edges. Flipping it increases the matching by one.

**4. Why is a greedy "first free donor" strategy not enough?**
It never reassigns. In our example P1 grabs D1, so P2 (who only fits D1) is left
out; greedy gives 2 pairs but the maximum is 3. Kuhn lets P1 move to D2.

**5. Why does Kuhn's algorithm give a *maximum* matching?**
By Berge's theorem a matching is maximum exactly when no augmenting path exists.
The algorithm tries to find one from every patient. A known lemma says that if
no augmenting path starts at a patient now, none will start there later, so one
pass over the patients is enough.

**6. Why is `visited[]` cleared for every patient, but not during the recursion?**
Inside one search it prevents endless loops and repeated work. Between searches
the matching has changed, so earlier dead ends may now lead somewhere; a new
search needs a clean slate.

**7. Where is the DFS in your code, and how deep can the recursion get?**
`try_augment` is a recursive function: it goes into the patient who owns a donor,
and from there into the next one, and so on. Each recursive step needs a newly
visited donor, so the depth is at most the number of donors.

**8. What is the time complexity?**
With the adjacency matrix it is O(P x D^2): P searches, each making at most D+1
calls that scan a row of D cells. With adjacency lists it would be O(P x E).

**9. What does `matchCount` mean, and how is it different from the edge count
and from the degrees?**
`matchCount` is the number of patient-donor pairs actually assigned. The edge
count is how many *possible* pairs exist. Degrees count possible partners per
vertex. `matchCount` is always <= the edge count and <= min(P, D).

**10. Is the matching unique? What happens with a city filter or an unavailable
donor?**
The number of pairs is fixed, but the exact pairs depend on the order of the
records. A city filter removes edges whose donor is in another city, and an
unavailable donor has no edges at all (degree 0), so neither can be matched.
A smaller graph can only have an equal or smaller maximum matching.