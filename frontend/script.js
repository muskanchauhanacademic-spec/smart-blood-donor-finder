/* frontend/script.js - Smart Blood Donor Finder frontend (Developer 5).
   Vanilla JS. All data comes from the C backend. Nothing here computes compatibility,
   degrees or matchings: this file only fetches and displays what the API returns.
   Record text is always written with textContent / text nodes, never innerHTML. */
(function () {
    'use strict';

    // UI constant used only to build dropdowns (same eight values as spec section G).
    const BLOOD_GROUPS = ['A+', 'A-', 'B+', 'B-', 'AB+', 'AB-', 'O+', 'O-'];
    const MAX_ID = 2147483647;
    const SVG_NS = 'http://www.w3.org/2000/svg';
    const NETWORK_MESSAGE = 'Cannot reach the C backend. Is sbdf.exe running?';

    // ------------------------------------------------------------------ DOM helpers

    function $(id) { return document.getElementById(id); }

    function setAttrs(node, attrs) {
        if (!attrs) { return; }
        Object.keys(attrs).forEach(function (k) {
            const v = attrs[k];
            if (v === undefined || v === null || v === false) { return; }
            if (k === 'text') { node.textContent = String(v); }
            else { node.setAttribute(k, v === true ? '' : String(v)); }
        });
    }

    // Children may be nodes, strings or numbers; strings become text nodes (never parsed as HTML).
    function append(node, children) {
        if (children === undefined || children === null) { return node; }
        const list = Array.isArray(children) ? children : [children];
        list.forEach(function (c) {
            if (c === undefined || c === null || c === false) { return; }
            node.appendChild(typeof c === 'object' ? c : document.createTextNode(String(c)));
        });
        return node;
    }

    function el(tag, attrs, children) {
        const node = document.createElement(tag);
        setAttrs(node, attrs);
        return append(node, children);
    }

    function svgEl(tag, attrs, children) {
        const node = document.createElementNS(SVG_NS, tag);
        setAttrs(node, attrs);
        return append(node, children);
    }

    function clear(node) {
        while (node.firstChild) { node.removeChild(node.firstChild); }
    }

    function num(v) { return typeof v === 'number' ? String(v) : '-'; }

    function trunc(s, n) {
        const t = String(s);
        return t.length > n ? t.slice(0, n - 1) + '\u2026' : t;
    }

    function list(v) { return Array.isArray(v) ? v : []; }

    // ------------------------------------------------------------------ API helper

    let pending = 0;

    function setPending(delta) {
        pending += delta;
        $('loading').hidden = pending <= 0;
        $('main').setAttribute('aria-busy', pending > 0 ? 'true' : 'false');
    }

    function apiError(code, message, status) {
        const err = new Error(message);
        err.code = code;
        err.status = status;
        return err;
    }

    // Wraps fetch + the {"ok":...} envelope. Returns envelope.data or throws Error{code,message}.
    // payload === undefined means "send no body" (used for POST /api/matching without a filter).
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
            let json = null;
            try {
                json = await res.json();
            } catch (e) {
                throw apiError('BAD_RESPONSE', 'The backend sent a reply that is not valid JSON (HTTP ' + res.status + ').', res.status);
            }
            if (!json || typeof json !== 'object') {
                throw apiError('BAD_RESPONSE', 'The backend sent an unexpected reply (HTTP ' + res.status + ').', res.status);
            }
            if (json.ok !== true) {
                const e = json.error || {};
                throw apiError(e.code || ('HTTP_' + res.status), e.message || ('Request failed (HTTP ' + res.status + ').'), res.status);
            }
            if (!json.data || typeof json.data !== 'object') {
                throw apiError('BAD_RESPONSE', 'The backend reply has no data.', res.status);
            }
            return json.data;
        } finally {
            setPending(-1);
        }
    }

    // ------------------------------------------------------------------ banner, toast, busy buttons

    let bannerRetry = null;

    function showBanner(message, retry) {
        $('banner-text').textContent = message;
        bannerRetry = retry || null;
        $('banner-retry').hidden = !retry;
        $('banner').hidden = false;
    }

    function hideBanner() {
        $('banner').hidden = true;
        bannerRetry = null;
    }

    function toast(message, kind) {
        const t = el('div', { class: 'toast ' + (kind === 'error' ? 'toast-error' : 'toast-success'), text: message });
        $('toasts').appendChild(t);
        setTimeout(function () {
            if (t.parentNode) { t.parentNode.removeChild(t); }
        }, kind === 'error' ? 7000 : 4000);
    }

    // Disables a button while a request is in flight and swaps its label.
    function setBusy(btn, busy, busyText) {
        if (busy) {
            if (btn.disabled) { return; }
            btn.dataset.label = btn.textContent;
            btn.textContent = busyText || 'Working\u2026';
            btn.disabled = true;
            btn.setAttribute('aria-busy', 'true');
        } else {
            if (btn.dataset.label !== undefined) { btn.textContent = btn.dataset.label; }
            btn.disabled = false;
            btn.removeAttribute('aria-busy');
        }
    }

    // ------------------------------------------------------------------ routing

    let navToken = 0;

    // Runs a loader; on failure shows the banner with a Retry (unless the user already left the view).
    async function guarded(work, retry) {
        const token = navToken;
        try {
            await work();
            if (token === navToken) { hideBanner(); }
        } catch (err) {
            if (token === navToken) { showBanner(err.message, retry); }
        }
    }

    function route(moveFocus) {
        let name = (location.hash || '').replace(/^#/, '');
        if (!Object.prototype.hasOwnProperty.call(VIEWS, name)) { name = 'dashboard'; }
        navToken += 1;
        hideBanner();
        document.querySelectorAll('.view').forEach(function (sec) {
            sec.hidden = sec.id !== 'view-' + name;
        });
        document.querySelectorAll('.nav-list a').forEach(function (a) {
            if (a.dataset.view === name) {
                a.setAttribute('aria-current', 'page');
                document.title = a.textContent + ' - Smart Blood Donor Finder';
            } else {
                a.removeAttribute('aria-current');
            }
        });
        if (moveFocus) {
            window.scrollTo(0, 0);
            $('h-' + name).focus();
        }
        VIEWS[name]();
    }

    // ------------------------------------------------------------------ shared UI pieces

    function badge(available) {
        return el('span', { class: 'badge ' + (available ? 'badge-yes' : 'badge-no'), text: available ? 'Available' : 'Unavailable' });
    }

    function bgChip(bg) { return el('span', { class: 'bg-chip', text: bg }); }

    function emptyState(title, detail, linkText, href) {
        const box = el('div', { class: 'empty' }, [el('p', { class: 'empty-title', text: title })]);
        if (detail) { box.appendChild(el('p', { text: detail })); }
        if (linkText) { box.appendChild(el('a', { href: href, text: linkText })); }
        return box;
    }

    // columns: [{header, cell(row) -> node|string, rowHeader?, numeric?}]
    function buildTable(caption, columns, rows) {
        const table = el('table', { class: 'data' });
        table.appendChild(el('caption', { class: 'sr-only', text: caption }));
        const headRow = el('tr');
        columns.forEach(function (c) {
            headRow.appendChild(el('th', { scope: 'col', class: c.numeric ? 'num' : null, text: c.header }));
        });
        table.appendChild(el('thead', null, [headRow]));
        const body = el('tbody');
        rows.forEach(function (r) {
            const tr = el('tr');
            columns.forEach(function (c) {
                tr.appendChild(el(c.rowHeader ? 'th' : 'td',
                    { scope: c.rowHeader ? 'row' : null, class: c.numeric ? 'num' : null },
                    [c.cell(r)]));
            });
            body.appendChild(tr);
        });
        table.appendChild(body);
        return el('div', { class: 'table-wrap', tabindex: '0', role: 'region', 'aria-label': caption }, [table]);
    }

    const COL = {
        id: function (h) { return { header: h || 'ID', rowHeader: true, cell: function (r) { return num(r.id); } }; },
        name: function (h) { return { header: h || 'Name', cell: function (r) { return r.name; } }; },
        bg: function () { return { header: 'Blood group', cell: function (r) { return bgChip(r.bloodGroup); } }; },
        city: function () { return { header: 'City', cell: function (r) { return r.city; } }; },
        phone: function () { return { header: 'Phone', cell: function (r) { return r.phone; } }; },
        status: function () { return { header: 'Availability', cell: function (r) { return badge(!!r.available); } }; }
    };

    function fillBloodGroupSelect(select, placeholder) {
        select.appendChild(el('option', { value: '', text: placeholder }));
        BLOOD_GROUPS.forEach(function (bg) { select.appendChild(el('option', { value: bg, text: bg })); });
    }

    // Rebuilds a city <select> from the donors list (distinct, case-insensitive, sorted) and keeps the old choice.
    function populateCitySelect(select, donors) {
        const previous = select.value;
        const seen = {};
        const cities = [];
        donors.forEach(function (d) {
            const key = String(d.city).toLowerCase();
            if (!seen[key]) { seen[key] = true; cities.push(d.city); }
        });
        cities.sort(function (a, b) { return a.localeCompare(b); });
        clear(select);
        select.appendChild(el('option', { value: '', text: 'All cities' }));
        cities.forEach(function (c) { select.appendChild(el('option', { value: c, text: c })); });
        if (cities.indexOf(previous) >= 0) { select.value = previous; }
    }

    // ------------------------------------------------------------------ client validation (mirrors spec section G)

    const CONTROL_CHARS = /[\u0000-\u001f\u007f]/;

    function byteLength(s) { return new TextEncoder().encode(s).length; }

    const CHECKS = {
        id: function (v) {
            const msg = 'ID must be a whole number from 1 to ' + MAX_ID + '.';
            if (!/^[0-9]+$/.test(v)) { return msg; }
            const n = Number(v);
            return (n < 1 || n > MAX_ID) ? msg : '';
        },
        name: function (v) {
            if (v.length === 0) { return 'Enter a name.'; }
            if (byteLength(v) > 49) { return 'Name is too long (49 bytes at most; accented letters use more than one).'; }
            if (CONTROL_CHARS.test(v)) { return 'Name must not contain control characters.'; }
            return '';
        },
        bloodGroup: function (v) {
            return BLOOD_GROUPS.indexOf(v) >= 0 ? '' : 'Choose a blood group.';
        },
        city: function (v) {
            if (v.length === 0) { return 'Enter a city.'; }
            if (byteLength(v) > 29) { return 'City is too long (29 bytes at most; accented letters use more than one).'; }
            if (CONTROL_CHARS.test(v)) { return 'City must not contain control characters.'; }
            return '';
        },
        phone: function (v) {
            if (v.length < 7 || v.length > 14 || !/^\+?[0-9]+$/.test(v)) {
                return 'Phone must be 7 to 14 characters: digits with an optional leading +.';
            }
            return '';
        }
    };

    // Server error code -> form field (spec section I). Other codes appear as a form-level message.
    const FIELD_BY_CODE = {
        INVALID_ID: 'id',
        DUPLICATE_ID: 'id',
        INVALID_NAME: 'name',
        INVALID_BLOOD_GROUP: 'bloodGroup',
        INVALID_CITY: 'city',
        INVALID_PHONE: 'phone',
        INVALID_AVAILABILITY: 'available'
    };

    function fieldInput(form, name) { return form.querySelector('[data-field="' + name + '"]'); }

    function setFieldError(input, message) {
        const p = $(input.id + '-error');
        if (p) { p.textContent = message; p.hidden = false; }
        input.setAttribute('aria-invalid', 'true');
    }

    function setFormStatus(form, message) {
        const p = form.querySelector('.form-status');
        p.textContent = message;
        p.hidden = !message;
    }

    function clearFormErrors(form) {
        form.querySelectorAll('.field-error').forEach(function (p) { p.textContent = ''; p.hidden = true; });
        form.querySelectorAll('[data-field]').forEach(function (i) { i.removeAttribute('aria-invalid'); });
        setFormStatus(form, '');
    }

    // Returns trimmed values, or null after showing errors and focusing the first bad field.
    function collectAndValidate(form, names) {
        clearFormErrors(form);
        const values = {};
        let firstBad = null;
        names.forEach(function (n) {
            const input = fieldInput(form, n);
            const v = input.value.trim();
            values[n] = v;
            const msg = CHECKS[n](v);
            if (msg) {
                setFieldError(input, msg);
                if (!firstBad) { firstBad = input; }
            }
        });
        if (firstBad) { firstBad.focus(); return null; }
        return values;
    }

    // The server is authoritative: show its message at the mapped field, or at form level.
    function showServerError(form, err) {
        const field = FIELD_BY_CODE[err.code];
        const input = field ? fieldInput(form, field) : null;
        if (input) {
            setFieldError(input, err.message);
            input.focus();
        } else {
            setFormStatus(form, err.message);
        }
    }

    // ------------------------------------------------------------------ B and C: registration forms

    function bindSwitch(input, textEl) {
        function update() { textEl.textContent = input.checked ? 'Available' : 'Unavailable'; }
        input.addEventListener('change', update);
        return update;
    }

    function setupRegisterForm(formId, fields, url, buildBody, successText) {
        const form = $(formId);
        const submit = form.querySelector('button[type="submit"]');
        form.addEventListener('submit', async function (ev) {
            ev.preventDefault();
            const values = collectAndValidate(form, fields);
            if (!values) { return; }
            setBusy(submit, true, 'Saving\u2026');
            try {
                const data = await api(url, 'POST', buildBody(values, form));
                form.reset();
                clearFormErrors(form);
                toast(successText(data));
            } catch (err) {
                showServerError(form, err);
            } finally {
                setBusy(submit, false);
            }
        });
        form.addEventListener('reset', function () { clearFormErrors(form); });
        return form;
    }

    function setupForms() {
        fillBloodGroupSelect($('donor-bg'), 'Select a blood group');
        fillBloodGroupSelect($('patient-bg'), 'Select a blood group');

        const donorForm = setupRegisterForm('donor-form', ['id', 'name', 'bloodGroup', 'city', 'phone'], '/api/donors',
            function (v, form) {
                return {
                    id: Number(v.id), name: v.name, bloodGroup: v.bloodGroup, city: v.city, phone: v.phone,
                    available: fieldInput(form, 'available').checked
                };
            },
            function (data) {
                return data.donor ? 'Donor ' + data.donor.id + ' (' + data.donor.name + ') registered.' : 'Donor registered.';
            });
        const updateSwitch = bindSwitch($('donor-available'), $('donor-available-text'));
        donorForm.addEventListener('reset', function () { setTimeout(updateSwitch, 0); });

        setupRegisterForm('patient-form', ['id', 'name', 'bloodGroup', 'city'], '/api/patients',
            function (v) {
                return { id: Number(v.id), name: v.name, bloodGroup: v.bloodGroup, city: v.city };
            },
            function (data) {
                return data.patient ? 'Patient ' + data.patient.id + ' (' + data.patient.name + ') registered.' : 'Patient registered.';
            });
    }

    // ------------------------------------------------------------------ A: dashboard

    function loadDashboard() {
        return guarded(async function () {
            const results = await Promise.all([api('/api/stats'), api('/api/donors'), api('/api/patients')]);
            renderStats(results[0]);
            renderDistribution(list(results[1].donors), list(results[2].patients));
        }, loadDashboard);
    }

    function renderStats(s) {
        const cards = [
            { label: 'Registered donors', value: s.donorCount, note: num(s.unavailableDonorCount) + ' currently unavailable' },
            { label: 'Available donors', value: s.availableDonorCount, note: 'Ready to donate now' },
            { label: 'Registered patients', value: s.patientCount, note: 'Waiting for a compatible donor' },
            { label: 'Potential compatible connections', value: s.edgeCount, note: 'Patient and available donor pairs with a compatible blood group' },
            { label: 'Maximum matching', value: s.maxMatching, note: 'Patients that can each get a different donor', featured: true },
            { label: 'Unmatched patients', value: s.unmatchedPatientCount, note: 'Left without a donor in the maximum matching' }
        ];
        const host = $('stats');
        clear(host);
        cards.forEach(function (c) {
            host.appendChild(el('li', { class: 'stat' + (c.featured ? ' stat-featured' : '') }, [
                el('p', { class: 'stat-value', text: num(c.value) }),
                el('p', { class: 'stat-label', text: c.label }),
                el('p', { class: 'stat-note', text: c.note })
            ]));
        });
    }

    // Simple tally of the two lists by blood group (counting only, no compatibility logic).
    function renderDistribution(donors, patients) {
        const host = $('dist-wrap');
        clear(host);
        if (donors.length === 0 && patients.length === 0) {
            host.appendChild(emptyState('Nothing to summarise yet', 'Register donors and patients to see the blood group distribution.', 'Register a donor', '#donor-register'));
            return;
        }
        const dc = {}, pc = {};
        BLOOD_GROUPS.forEach(function (bg) { dc[bg] = 0; pc[bg] = 0; });
        donors.forEach(function (d) { if (d.bloodGroup in dc) { dc[d.bloodGroup] += 1; } });
        patients.forEach(function (p) { if (p.bloodGroup in pc) { pc[p.bloodGroup] += 1; } });
        let max = 1;
        BLOOD_GROUPS.forEach(function (bg) { max = Math.max(max, dc[bg], pc[bg]); });

        function barCell(count, cls) {
            const bar = el('span', { class: 'bar ' + cls });
            bar.style.width = (count / max * 100) + '%';
            return el('div', { class: 'bar-cell' }, [el('span', { class: 'bar-num', text: count }), el('span', { class: 'bar-track' }, [bar])]);
        }
        host.appendChild(buildTable('Blood group distribution of donors and patients', [
            { header: 'Blood group', rowHeader: true, cell: function (bg) { return bgChip(bg); } },
            { header: 'Donors', cell: function (bg) { return barCell(dc[bg], 'bar-donor'); } },
            { header: 'Patients', cell: function (bg) { return barCell(pc[bg], 'bar-patient'); } }
        ], BLOOD_GROUPS));
    }

    // ------------------------------------------------------------------ D: donor directory

    const donorState = { all: [] };

    function loadDonors() {
        return guarded(async function () {
            const data = await api('/api/donors');
            donorState.all = list(data.donors);
            renderDonorTable();
        }, loadDonors);
    }

    function renderDonorTable() {
        const q = $('donor-search').value.trim().toLowerCase();
        const bg = $('donor-bg-filter').value;
        const av = $('donor-avail-filter').value;
        const rows = donorState.all.filter(function (d) {
            if (bg && d.bloodGroup !== bg) { return false; }
            if (av === 'yes' && !d.available) { return false; }
            if (av === 'no' && d.available) { return false; }
            if (q) {
                const hay = (d.id + ' ' + d.name + ' ' + d.city + ' ' + d.phone).toLowerCase();
                if (hay.indexOf(q) < 0) { return false; }
            }
            return true;
        });
        const host = $('donor-table');
        clear(host);
        $('donor-count').textContent = 'Showing ' + rows.length + ' of ' + donorState.all.length + ' donors.';
        if (donorState.all.length === 0) {
            host.appendChild(emptyState('No donors registered yet', 'Add the first donor to start building the graph.', 'Register a donor', '#donor-register'));
            return;
        }
        if (rows.length === 0) {
            host.appendChild(emptyState('No donors match these filters', 'Clear the search or change a filter.'));
            return;
        }
        host.appendChild(buildTable('Donor directory', [
            COL.id(), COL.name(), COL.bg(), COL.city(), COL.phone(), COL.status(),
            { header: 'Actions', cell: donorActions }
        ], rows));
    }

    function donorActions(d) {
        const toggle = el('button', {
            type: 'button', class: 'btn btn-small',
            text: d.available ? 'Mark unavailable' : 'Mark available',
            'aria-label': (d.available ? 'Mark unavailable' : 'Mark available') + ', donor ' + d.name
        });
        toggle.addEventListener('click', function () { toggleAvailability(d, toggle); });
        const edit = el('button', { type: 'button', class: 'btn btn-small', text: 'Edit', 'aria-label': 'Edit, donor ' + d.name });
        edit.addEventListener('click', function () { openEdit(d); });
        return el('div', { class: 'row-actions' }, [toggle, edit]);
    }

    async function toggleAvailability(d, btn) {
        setBusy(btn, true, 'Saving\u2026');
        try {
            const data = await api('/api/donors/' + encodeURIComponent(String(d.id)) + '/availability', 'PUT', { available: !d.available });
            const now = data.donor ? !!data.donor.available : !d.available;
            toast('Donor ' + d.id + ' is now ' + (now ? 'available' : 'unavailable') + '.');
            await loadDonors();
        } catch (err) {
            toast(err.message, 'error');
            setBusy(btn, false);
        }
    }

    let editingDonor = null;

    function openEdit(d) {
        const dlg = $('edit-dialog');
        if (typeof dlg.showModal !== 'function') {
            toast('This browser cannot open the edit window.', 'error');
            return;
        }
        editingDonor = d;
        const form = $('edit-form');
        clearFormErrors(form);
        $('edit-title').textContent = 'Edit donor ' + d.id;
        $('edit-note').textContent = 'Blood group ' + d.bloodGroup + ' cannot be changed.';
        $('edit-name').value = d.name;
        $('edit-city').value = d.city;
        $('edit-phone').value = d.phone;
        dlg.showModal();
        $('edit-name').focus();
    }

    function setupEditDialog() {
        const form = $('edit-form');
        const submit = form.querySelector('button[type="submit"]');
        $('edit-cancel').addEventListener('click', function () { $('edit-dialog').close(); });
        form.addEventListener('submit', async function (ev) {
            ev.preventDefault();
            if (!editingDonor) { return; }
            const v = collectAndValidate(form, ['name', 'city', 'phone']);
            if (!v) { return; }
            setBusy(submit, true, 'Saving\u2026');
            try {
                await api('/api/donors/' + encodeURIComponent(String(editingDonor.id)), 'PUT', { name: v.name, city: v.city, phone: v.phone });
                $('edit-dialog').close();
                toast('Donor ' + editingDonor.id + ' updated.');
                editingDonor = null;
                await loadDonors();
            } catch (err) {
                showServerError(form, err);
            } finally {
                setBusy(submit, false);
            }
        });
    }

    // ------------------------------------------------------------------ E: patient directory

    const patientState = { all: [] };

    function loadPatients() {
        return guarded(async function () {
            const data = await api('/api/patients');
            patientState.all = list(data.patients);
            renderPatientTable();
        }, loadPatients);
    }

    function renderPatientTable() {
        const q = $('patient-search').value.trim().toLowerCase();
        const rows = patientState.all.filter(function (p) {
            if (!q) { return true; }
            return (p.id + ' ' + p.name + ' ' + p.city + ' ' + p.bloodGroup).toLowerCase().indexOf(q) >= 0;
        });
        const host = $('patient-table');
        clear(host);
        $('patient-count').textContent = 'Showing ' + rows.length + ' of ' + patientState.all.length + ' patients.';
        if (patientState.all.length === 0) {
            host.appendChild(emptyState('No patients registered yet', 'Add a patient to search for compatible donors.', 'Register a patient', '#patient-register'));
            return;
        }
        if (rows.length === 0) {
            host.appendChild(emptyState('No patients match this search', 'Clear the search box to see everyone.'));
            return;
        }
        host.appendChild(buildTable('Patient directory', [COL.id(), COL.name(), COL.bg(), COL.city()], rows));
    }

    // ------------------------------------------------------------------ F: compatible donor search

    function loadSearch() {
        return guarded(async function () {
            const results = await Promise.all([api('/api/patients'), api('/api/donors')]);
            const patients = list(results[0].patients);
            const sel = $('search-patient');
            const previous = sel.value;
            clear(sel);
            sel.appendChild(el('option', { value: '', text: 'Select a patient' }));
            patients.forEach(function (p) {
                sel.appendChild(el('option', { value: String(p.id), text: p.name + ' (ID ' + p.id + ', ' + p.bloodGroup + ')' }));
            });
            if (previous && sel.querySelector('option[value="' + previous + '"]')) { sel.value = previous; }
            populateCitySelect($('search-city'), list(results[1].donors));
            $('search-no-patients').hidden = patients.length > 0;
            $('search-submit').disabled = patients.length === 0;
            clear($('search-results'));
        }, loadSearch);
    }

    async function runSearch() {
        const sel = $('search-patient');
        const btn = $('search-submit');
        sel.removeAttribute('aria-invalid');
        $('search-patient-error').hidden = true;
        if (!sel.value) {
            setFieldError(sel, 'Choose a patient first.');
            sel.focus();
            return;
        }
        let url = '/api/search?patientId=' + encodeURIComponent(sel.value);
        if ($('search-city').value) { url += '&city=' + encodeURIComponent($('search-city').value); }
        url += '&availableOnly=' + ($('search-available').checked ? 'true' : 'false');
        setBusy(btn, true, 'Searching\u2026');
        try {
            const data = await api(url);
            hideBanner();
            renderSearchResults(data);
        } catch (err) {
            clear($('search-results'));
            showBanner(err.message, runSearch);
        } finally {
            setBusy(btn, false);
        }
    }

    function renderSearchResults(data) {
        const host = $('search-results');
        clear(host);
        const p = data.patient || {};
        const donors = list(data.donors);
        const f = data.filters || {};
        const count = typeof data.count === 'number' ? data.count : donors.length;
        host.appendChild(el('h2', { text: 'Potential donors for ' + (p.name || 'the selected patient') }));
        host.appendChild(el('p', { class: 'result-meta', text:
            'Patient ID ' + num(p.id) + ', blood group ' + (p.bloodGroup || '-') + ', ' + (p.city || '-') + '. ' +
            count + (count === 1 ? ' donor' : ' donors') + ' found (' +
            (f.city ? 'city: ' + f.city : 'all cities') + ', ' +
            (f.availableOnly === false ? 'including unavailable donors' : 'available donors only') + ').' }));
        if (donors.length === 0) {
            host.appendChild(emptyState('No compatible donors found', 'Try including unavailable donors or choosing all cities.'));
            return;
        }
        host.appendChild(buildTable('Compatible donors', [COL.id(), COL.name(), COL.bg(), COL.city(), COL.phone(), COL.status()], donors));
    }

    // ------------------------------------------------------------------ G: graph, matrix, degrees

    // Draws the two-column bipartite graph. pairs (optional) = matching result pairs, drawn thick red.
    function buildGraphSvg(data, pairs) {
        const patients = list(data.patients);
        const donors = list(data.donors);
        const edges = list(data.edges);
        const ROW = 34, TOP = 56, W = 860, XL = 320, XR = 540, R = 8;
        const rows = Math.max(patients.length, donors.length, 1);
        const H = TOP + rows * ROW + 16;

        const matched = {};
        const matchedP = {}, matchedD = {};
        list(pairs).forEach(function (pr) {
            matched[pr.patientId + ':' + pr.donorId] = true;
            matchedP[pr.patientId] = true;
            matchedD[pr.donorId] = true;
        });

        const svg = svgEl('svg', {
            class: 'graph-svg', width: W, height: H, viewBox: '0 0 ' + W + ' ' + H,
            role: 'group', 'aria-label': 'Bipartite graph with ' + patients.length + ' patients and ' + donors.length + ' donors'
        });
        svg.appendChild(svgEl('title', null, ['Patients on the left, donors on the right']));
        svg.appendChild(svgEl('text', { x: XL + R, y: 28, 'text-anchor': 'end', class: 'col-title' }, ['Patients (' + patients.length + ')']));
        svg.appendChild(svgEl('text', { x: XR - R, y: 28, 'text-anchor': 'start', class: 'col-title' }, ['Donors (' + donors.length + ')']));

        const edgeLayer = svgEl('g', { class: 'edges' });
        const vertexLayer = svgEl('g', { class: 'vertices' });
        svg.appendChild(edgeLayer);
        svg.appendChild(vertexLayer);

        const pPos = {}, dPos = {}, pRec = {}, dRec = {};
        patients.forEach(function (p, i) { pPos[p.id] = TOP + i * ROW + ROW / 2; pRec[p.id] = p; });
        donors.forEach(function (d, i) { dPos[d.id] = TOP + i * ROW + ROW / 2; dRec[d.id] = d; });

        const edgesByKey = {};
        const vertexByKey = {};

        function addToKey(key, item) {
            if (!edgesByKey[key]) { edgesByKey[key] = []; }
            edgesByKey[key].push(item);
        }

        // Grey edges first, matched edges last so they sit on top.
        const plain = [], thick = [];
        edges.forEach(function (e) {
            if (!(e.patientId in pPos) || !(e.donorId in dPos)) { return; }
            const isMatched = !!matched[e.patientId + ':' + e.donorId];
            const line = svgEl('line', {
                x1: XL + R, y1: pPos[e.patientId], x2: XR - R, y2: dPos[e.donorId],
                class: isMatched ? 'edge edge-matched' : 'edge'
            }, [svgEl('title', null, [
                (isMatched ? 'Matched: ' : 'Possible: ') + 'patient ' + e.patientId + ' (' + pRec[e.patientId].name + ') and donor ' + e.donorId + ' (' + dRec[e.donorId].name + ')'
            ])]);
            const item = { line: line, pKey: 'p' + e.patientId, dKey: 'd' + e.donorId };
            addToKey(item.pKey, item);
            addToKey(item.dKey, item);
            (isMatched ? thick : plain).push(line);
        });
        plain.concat(thick).forEach(function (line) { edgeLayer.appendChild(line); });

        function makeVertex(kind, rec, y) {
            const isPatient = kind === 'p';
            const x = isPatient ? XL : XR;
            const key = kind + rec.id;
            const degree = num(rec.degree);
            const off = !isPatient && !rec.available;
            const desc = (isPatient ? 'Patient ' : 'Donor ') + rec.id + ': ' + rec.name + ', ' + rec.bloodGroup + ', ' + rec.city +
                (isPatient ? '' : (rec.available ? ', available' : ', unavailable')) + '. Degree ' + degree + '.';
            const isMatchedVertex = isPatient ? matchedP[rec.id] : matchedD[rec.id];
            const g = svgEl('g', { class: 'vertex', tabindex: '0', role: 'img', 'aria-label': desc }, [svgEl('title', null, [desc])]);
            if (isMatchedVertex) { g.appendChild(svgEl('circle', { class: 'ring', cx: x, cy: y, r: R + 4 })); }
            g.appendChild(svgEl('circle', {
                class: 'node ' + (isPatient ? 'node-patient' : (off ? 'node-donor node-off' : 'node-donor')), cx: x, cy: y, r: R
            }));
            g.appendChild(svgEl('text', {
                x: isPatient ? x - R - 10 : x + R + 10, y: y + 4, 'text-anchor': isPatient ? 'end' : 'start'
            }, ['#' + rec.id + ' ' + trunc(rec.name, 18) + ' (' + rec.bloodGroup + '), degree ' + degree + (off ? ', unavailable' : '')]));
            vertexByKey[key] = g;
            g.addEventListener('mouseenter', function () { highlight(key); });
            g.addEventListener('mouseleave', clearHighlight);
            g.addEventListener('focus', function () { highlight(key); });
            g.addEventListener('blur', clearHighlight);
            vertexLayer.appendChild(g);
        }
        patients.forEach(function (p) { makeVertex('p', p, pPos[p.id]); });
        donors.forEach(function (d) { makeVertex('d', d, dPos[d.id]); });

        let lit = [];
        function highlight(key) {
            clearHighlight();
            svg.classList.add('is-highlighting');
            vertexByKey[key].classList.add('hl');
            lit.push(vertexByKey[key]);
            (edgesByKey[key] || []).forEach(function (item) {
                item.line.classList.add('hl');
                edgeLayer.appendChild(item.line);   // re-appending moves it above the other lines
                lit.push(item.line);
                [item.pKey, item.dKey].forEach(function (k) {
                    if (vertexByKey[k]) { vertexByKey[k].classList.add('hl'); lit.push(vertexByKey[k]); }
                });
            });
        }
        function clearHighlight() {
            svg.classList.remove('is-highlighting');
            lit.forEach(function (n) { n.classList.remove('hl'); });
            lit = [];
        }
        return svg;
    }

    function graphLegend(withMatching) {
        function item(swatchClass, text) {
            return el('li', null, [el('span', { class: 'sw ' + swatchClass, 'aria-hidden': 'true' }), text]);
        }
        const items = [item('sw-patient', 'Patient'), item('sw-donor', 'Available donor'), item('sw-off', 'Unavailable donor (hollow, dashed)')];
        items.push(item('sw-edge', withMatching ? 'Other possible connection' : 'Possible connection'));
        if (withMatching) { items.push(item('sw-matched', 'Matched pair (thick red line)')); }
        return el('ul', { class: 'legend', 'aria-label': 'Legend' }, items);
    }

    function filterText(cityFilter) { return cityFilter ? 'donors in ' + cityFilter : 'all cities'; }

    // Appends legend + scrollable SVG, or an empty state when the graph has no vertices on one side.
    function appendGraph(host, data, pairs) {
        if (list(data.patients).length === 0 || list(data.donors).length === 0) {
            host.appendChild(emptyState('Nothing to draw yet', 'The graph needs at least one patient and one donor.', 'Register a donor', '#donor-register'));
            return;
        }
        if (list(data.edges).length === 0) {
            host.appendChild(el('p', { class: 'hint', text: 'No compatible connections for this filter, so only the vertices are shown.' }));
        }
        host.appendChild(graphLegend(!!pairs));
        host.appendChild(el('div', { class: 'graph-scroll', tabindex: '0', role: 'region', 'aria-label': 'Graph drawing (scrollable)' }, [buildGraphSvg(data, pairs)]));
    }

    const GRAPH_TABS = ['graph', 'matrix', 'degrees'];
    let graphTab = 'graph';
    let graphSeq = 0;

    function loadGraphView() {
        return guarded(async function () {
            const d = await api('/api/donors');
            populateCitySelect($('graph-city'), list(d.donors));
            await loadGraphTab();
        }, loadGraphView);
    }

    function refreshGraphTab() { return guarded(loadGraphTab, refreshGraphTab); }

    async function loadGraphTab() {
        const seq = ++graphSeq;
        const city = $('graph-city').value;
        const q = city ? '?city=' + encodeURIComponent(city) : '';
        if (graphTab === 'graph') {
            const data = await api('/api/graph' + q);
            if (seq !== graphSeq) { return; }
            renderGraphPanel(data);
        } else if (graphTab === 'matrix') {
            const data = await api('/api/graph/matrix' + q);
            if (seq !== graphSeq) { return; }
            renderMatrix(data);
        } else {
            const data = await api('/api/graph/degrees' + q);
            if (seq !== graphSeq) { return; }
            renderDegrees(data);
        }
    }

    function setGraphTab(name, focus) {
        graphTab = name;
        GRAPH_TABS.forEach(function (t) {
            const selected = t === name;
            $('tab-' + t).setAttribute('aria-selected', selected ? 'true' : 'false');
            $('tab-' + t).tabIndex = selected ? 0 : -1;
            $('panel-' + t).hidden = !selected;
        });
        if (focus) { $('tab-' + name).focus(); }
        refreshGraphTab();
    }

    function renderGraphPanel(data) {
        const host = $('panel-graph');
        clear(host);
        host.appendChild(el('p', { class: 'result-meta', text:
            num(data.numPatients) + ' patients, ' + num(data.numDonors) + ' donors, ' + num(data.edgeCount) +
            ' possible connections (' + filterText(data.cityFilter) + ').' }));
        appendGraph(host, data, null);
    }

    function srPrefix(prefix, id) {
        return el('span', null, [el('span', { class: 'sr-only', text: prefix + ' ' }), String(id)]);
    }

    function renderMatrix(data) {
        const host = $('panel-matrix');
        clear(host);
        const pids = list(data.patientIds), dids = list(data.donorIds);
        const matrix = list(data.matrix), rowSums = list(data.rowSums), colSums = list(data.columnSums);
        if (pids.length === 0 || dids.length === 0 || matrix.length === 0) {
            host.appendChild(emptyState('The matrix is empty', 'It needs at least one patient and one donor.', 'Register a donor', '#donor-register'));
            return;
        }
        host.appendChild(el('p', { class: 'result-meta', text:
            'Rows are patient IDs and columns are donor IDs (' + filterText(data.cityFilter) + '). A 1 marks a possible connection.' }));

        const headRow = el('tr', null, [el('th', { scope: 'col', text: 'Patient ID \\ Donor ID' })]);
        dids.forEach(function (id) { headRow.appendChild(el('th', { scope: 'col' }, [srPrefix('Donor', id)])); });
        headRow.appendChild(el('th', { scope: 'col', text: 'Row sum' }));

        const body = el('tbody');
        pids.forEach(function (pid, i) {
            const tr = el('tr', null, [el('th', { scope: 'row' }, [srPrefix('Patient', pid)])]);
            const cells = list(matrix[i]);
            dids.forEach(function (did, j) {
                tr.appendChild(el('td', { class: cells[j] === 1 ? 'one' : null, text: cells[j] }));
            });
            tr.appendChild(el('td', { class: 'sum', text: num(rowSums[i]) }));
            body.appendChild(tr);
        });

        const footRow = el('tr', null, [el('th', { scope: 'row', text: 'Column sum' })]);
        dids.forEach(function (did, j) { footRow.appendChild(el('td', { text: num(colSums[j]) })); });
        footRow.appendChild(el('td'));

        const table = el('table', { class: 'data matrix' }, [
            el('caption', { class: 'sr-only', text: 'Adjacency matrix of patients and donors' }),
            el('thead', null, [headRow]), body, el('tfoot', null, [footRow])
        ]);
        host.appendChild(el('div', { class: 'table-wrap', tabindex: '0', role: 'region', 'aria-label': 'Adjacency matrix (scrollable)' }, [table]));
    }

    function renderDegrees(data) {
        const host = $('panel-degrees');
        clear(host);
        const degCol = { header: 'Degree', numeric: true, cell: function (r) { return num(r.degree); } };
        const patients = list(data.patients), donors = list(data.donors);
        host.appendChild(el('p', { class: 'result-meta', text:
            'Possible connections: ' + num(data.edgeCount) + '. Sum of patient degrees: ' + num(data.patientDegreeSum) +
            '. Sum of donor degrees: ' + num(data.donorDegreeSum) + ' (' + filterText(data.cityFilter) + ').' }));
        host.appendChild(el('p', { class: 'hint', text: 'A degree counts the connections a vertex has. Every connection is counted once on each side, so both sums should equal the number of connections.' }));
        const split = el('div', { class: 'split' });
        const left = el('div', null, [el('h3', { text: 'Patients' })]);
        const right = el('div', null, [el('h3', { text: 'Donors' })]);
        left.appendChild(patients.length ? buildTable('Patient degrees', [COL.id('Patient ID'), COL.name(), degCol], patients) : emptyState('No patients yet'));
        right.appendChild(donors.length ? buildTable('Donor degrees', [COL.id('Donor ID'), COL.name(), degCol], donors) : emptyState('No donors yet'));
        split.appendChild(left);
        split.appendChild(right);
        host.appendChild(split);
    }

    function setupGraphTabs() {
        GRAPH_TABS.forEach(function (t, idx) {
            $('tab-' + t).addEventListener('click', function () { setGraphTab(t, false); });
            $('tab-' + t).addEventListener('keydown', function (ev) {
                let next = -1;
                if (ev.key === 'ArrowRight') { next = (idx + 1) % GRAPH_TABS.length; }
                else if (ev.key === 'ArrowLeft') { next = (idx + GRAPH_TABS.length - 1) % GRAPH_TABS.length; }
                else if (ev.key === 'Home') { next = 0; }
                else if (ev.key === 'End') { next = GRAPH_TABS.length - 1; }
                if (next >= 0) { ev.preventDefault(); setGraphTab(GRAPH_TABS[next], true); }
            });
        });
        $('graph-city').addEventListener('change', refreshGraphTab);
        $('graph-refresh').addEventListener('click', refreshGraphTab);
    }

    // ------------------------------------------------------------------ H: maximum matching

    function loadMatchingView() {
        return guarded(async function () {
            const d = await api('/api/donors');
            populateCitySelect($('match-city'), list(d.donors));
            // Earlier results are not kept as truth: the person must run the matching again.
            const host = $('match-results');
            clear(host);
            host.appendChild(emptyState('No matching has been run yet', 'Choose an optional city and select "Run maximum matching".'));
        }, loadMatchingView);
    }

    async function runMatching() {
        const city = $('match-city').value;
        const btn = $('match-run');
        setBusy(btn, true, 'Running\u2026');
        try {
            const result = await api('/api/matching', 'POST', city ? { city: city } : undefined);
            // The matching reply lists pairs only, so the full edge set comes from /api/graph.
            const graph = await api('/api/graph' + (city ? '?city=' + encodeURIComponent(city) : ''));
            hideBanner();
            renderMatching(result, graph);
        } catch (err) {
            showBanner(err.message, runMatching);
        } finally {
            setBusy(btn, false);
        }
    }

    function renderMatching(result, graph) {
        const host = $('match-results');
        clear(host);
        const pairs = list(result.pairs);
        const summary = el('ul', { class: 'stats stats-small' });
        [['Pairs matched', num(result.matchCount), true], ['Patients', num(result.numPatients)], ['Donors', num(result.numDonors)],
         ['Donor filter', result.cityFilter ? result.cityFilter : 'All cities']].forEach(function (c) {
            summary.appendChild(el('li', { class: 'stat' + (c[2] ? ' stat-featured' : '') }, [
                el('p', { class: 'stat-value', text: c[1] }), el('p', { class: 'stat-label', text: c[0] })
            ]));
        });
        host.appendChild(summary);

        host.appendChild(el('h2', { text: 'Matching graph' }));
        appendGraph(host, graph, pairs);

        host.appendChild(el('h2', { text: 'Matched pairs' }));
        host.appendChild(pairs.length
            ? buildTable('Matched patient and donor pairs', [
                { header: 'Patient ID', rowHeader: true, cell: function (r) { return num(r.patientId); } },
                { header: 'Patient', cell: function (r) { return r.patientName; } },
                { header: 'Donor ID', cell: function (r) { return num(r.donorId); } },
                { header: 'Donor', cell: function (r) { return r.donorName; } }
            ], pairs)
            : emptyState('No pairs found', 'No patient has a compatible available donor for this filter.'));

        const up = list(result.unmatchedPatients), ud = list(result.unmatchedDonors);
        host.appendChild(el('h2', { text: 'Unmatched patients' }));
        host.appendChild(up.length
            ? buildTable('Unmatched patients', [COL.id('Patient ID'), COL.name(), COL.bg(), COL.city()], up)
            : emptyState('No unmatched patients'));
        host.appendChild(el('h2', { text: 'Unmatched donors' }));
        host.appendChild(ud.length
            ? buildTable('Unmatched donors', [COL.id('Donor ID'), COL.name(), COL.bg(), COL.city(), COL.status()], ud)
            : emptyState('No unmatched donors'));
    }

    // ------------------------------------------------------------------ view table and startup

    const VIEWS = {
        'dashboard': loadDashboard,
        'donor-register': function () {},
        'patient-register': function () {},
        'donors': loadDonors,
        'patients': loadPatients,
        'search': loadSearch,
        'graph': loadGraphView,
        'matching': loadMatchingView
    };

    function init() {
        setupForms();
        setupEditDialog();
        setupGraphTabs();

        fillBloodGroupSelect($('donor-bg-filter'), 'All blood groups');
        ['donor-search', 'donor-bg-filter', 'donor-avail-filter'].forEach(function (id) {
            $(id).addEventListener('input', renderDonorTable);
            $(id).addEventListener('change', renderDonorTable);
        });
        $('donor-refresh').addEventListener('click', loadDonors);
        $('patient-search').addEventListener('input', renderPatientTable);
        $('patient-refresh').addEventListener('click', loadPatients);

        $('search-form').addEventListener('submit', function (ev) { ev.preventDefault(); runSearch(); });
        $('match-run').addEventListener('click', runMatching);

        $('banner-retry').addEventListener('click', function () {
            const fn = bannerRetry;
            hideBanner();
            if (fn) { fn(); }
        });
        $('banner-dismiss').addEventListener('click', hideBanner);

        window.addEventListener('hashchange', function () { route(true); });
        route(false);
    }

    init();
}());