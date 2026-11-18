// DOM integration checks for generated reports. Canvas calls are stubbed; this
// validates offline data and interactions, not browser layout or GPU drawing.
// Install jsdom in build/profile-html-tools and set NODE_PATH to its node_modules.
// Usage: node test/tools/profile_report.cjs <report.html> [other-report.html ...]
const assert = require('node:assert/strict');
const fs = require('node:fs');
const { JSDOM, VirtualConsole } = require('jsdom');

function load_report(html, rectangles = new Map(), errors = []) {
    const virtualConsole = new VirtualConsole();
    virtualConsole.on('jsdomError', error => errors.push(error));
    return new JSDOM(html, {
        runScripts: 'dangerously',
        virtualConsole,
        beforeParse(window) {
            window.HTMLCanvasElement.prototype.getContext = function() {
                const boxes = [];
                rectangles.set(this.id, boxes);
                return new Proxy({}, {
                    get: (_, key) => key === 'fillRect' ? (...args) => boxes.push(args) : () => {},
                });
            };
            window.HTMLCanvasElement.prototype.getBoundingClientRect = function() {
                return { left: 0, top: 0, width: 1200, height: parseFloat(this.style.height) || 80 };
            };
            window.HTMLCanvasElement.prototype.setPointerCapture = function(id) { this.capture_id = id; };
            window.HTMLCanvasElement.prototype.hasPointerCapture = function(id) { return this.capture_id === id; };
            window.HTMLCanvasElement.prototype.releasePointerCapture = function() { delete this.capture_id; };
            Object.defineProperty(window.HTMLElement.prototype, 'clientWidth', { get: () => 1200 });
            window.fetch = () => { throw new Error('Report must work offline'); };
        },
    });
}

assert(process.argv.length > 2, 'Supply generated HTML reports');
for (const path of process.argv.slice(2)) {
    const errors = [];
    const rectangles = new Map();
    const html = fs.readFileSync(path, 'utf8');
    const dom = load_report(html, rectangles, errors);
    const { document, Event } = dom.window;
    const node = id => document.getElementById(id);
    const capture = JSON.parse(node('profile_data').textContent);
    const events = capture.traceEvents.filter(event => event.ph === 'X');
    const change = (id, value, type = 'change') => {
        node(id).value = String(value);
        node(id).dispatchEvent(new Event(type));
    };
    const rows = id => document.querySelectorAll('#' + id + ' tbody tr');
    const check_navigation = (prefix) => {
        const from = node(prefix === 'cpu' ? 'from' : prefix + '_from');
        const to = node(prefix === 'cpu' ? 'to' : prefix + '_to');
        const reset = node(prefix === 'cpu' ? 'reset' : prefix + '_reset');
        const canvas = node(prefix + '_canvas');
        const span = () => Number(to.value) - Number(from.value);
        const close = (actual, expected) => assert(Math.abs(actual - expected) < 1e-7,
            `Expected ${actual} to be close to ${expected}`);
        reset.click();
        const full = span();
        node(prefix + '_zoom_in').click();
        close(span(), Math.max(0.001, full / 2));
        if (full > 0.002) {
            const start = Number(from.value);
            node(prefix + '_pan_right').click();
            assert(Number(from.value) > start);
            node(prefix + '_pan_left').click();
            close(Number(from.value), start);
        }
        node(prefix + '_zoom_out').click();
        close(span(), full);
        reset.click();
        const anchor = full * 0.75;
        const wheel = new dom.window.WheelEvent('wheel', {
            clientX: 150 + (1200 - 166) * 0.75, clientY: 35,
            deltaY: -200, bubbles: true, cancelable: true,
        });
        canvas.dispatchEvent(wheel);
        assert(wheel.defaultPrevented);
        close(Number(from.value) + span() * 0.75, anchor);
        const zoomed_span = span();
        canvas.dispatchEvent(new dom.window.KeyboardEvent('keydown', { key: '+', bubbles: true }));
        close(span(), Math.max(0.001, zoomed_span / 2));
        if (full > 0.002) {
            const before_pan = Number(from.value);
            canvas.dispatchEvent(new dom.window.WheelEvent('wheel', { deltaY: 100, shiftKey: true, bubbles: true }));
            assert(Number(from.value) > before_pan);
            close(span(), Math.max(0.001, zoomed_span / 2));
            canvas.dispatchEvent(new dom.window.KeyboardEvent('keydown', { key: 'ArrowLeft', bubbles: true }));
            assert(Number(from.value) < before_pan + span() * 0.15);
        }
        const before_drag = Number(from.value), drag_span = span();
        const pointer = (type, x) => {
            const event = new dom.window.MouseEvent(type, {clientX: x, clientY: 35, button: 0, bubbles: true});
            Object.defineProperty(event, 'pointerId', {value: 1});
            canvas.dispatchEvent(event);
        };
        pointer('pointerdown', 700);
        pointer('pointermove', 760);
        if (before_drag > 0 && full > 0.002) assert(Number(from.value) < before_drag);
        close(span(), drag_span);
        pointer('pointerup', 760);
        assert.equal(canvas.style.cursor, 'grab');
        canvas.dispatchEvent(new dom.window.KeyboardEvent('keydown', { key: 'Home', bubbles: true }));
        close(span(), full);
        close(Number(from.value), 0);
        const box = rectangles.get(canvas.id).find(([x, y, width]) => x >= 150 && width < 500);
        if (box && full > 0.01) {
            canvas.dispatchEvent(new dom.window.MouseEvent('dblclick', {
                clientX: box[0] + Math.min(box[2] / 2, 1), clientY: box[1] + 5, bubbles: true,
            }));
            assert(span() < full, 'Double-click should focus a scope or Pass');
        }
        reset.click();
        node(prefix + '_pan_left').click();
        close(Number(from.value), 0);
        node(prefix + '_pan_right').click();
        close(span(), full);
    };
    assert.equal(errors.length, 0, errors.map(error => error.stack).join('\n'));
    assert(node('title').textContent.includes(capture.name));
    assert.equal(rows('memory_table').length, capture.memory.samples.length);
    assert.equal(node('gpu_frame').options.length, capture.gpu_frames.length);
    assert.equal(node('graph_frame').options.length, capture.graphs.length);
    assert.equal(dom.window.profile_injected, undefined);
    assert(!document.querySelector('script[src],link[href],iframe'));
    if (events.length) {
        const event = events.find(item => item.name === 'LoadWorker') || events[0];
        change('search', event.name, 'input');
        assert(rows('cpu_table').length > 0);
        for (const row of rows('cpu_table')) assert(row.cells[0].textContent.includes(event.name));
        change('thread', event.tid);
        assert(rows('cpu_table').length > 0);
        change('search', 'no scope has this name 1024387', 'input');
        assert.equal(rows('cpu_table').length, 0);
        change('search', '', 'input');
    } else {
        assert.equal(rows('cpu_table').length, 0);
    }
    change('to', 0);
    assert(node('cpu_status').textContent.includes('valid time range'));
    node('reset').click();
    assert(!node('cpu_status').textContent.includes('valid time range'));
    check_navigation('cpu');
    if (capture.gpu_frames.length) {
        const index = capture.gpu_frames.length - 1;
        change('gpu_frame', index);
        const passes = capture.gpu_frames[index].passes;
        assert.equal(rows('gpu_table').length, passes.length);
        passes.forEach((pass, i) => {
            assert.equal(rows('gpu_table')[i].cells[0].textContent, pass.name);
            if (!pass.pipeline_statistics.available) {
                assert.equal(rows('gpu_table')[i].cells[4].textContent, 'Unavailable');
            } else {
                const expected = [pass.pipeline_statistics.input_vertices,
                    pass.pipeline_statistics.vertex_invocations, pass.pipeline_statistics.pixel_invocations,
                    pass.pipeline_statistics.compute_invocations];
                expected.forEach((value, column) => assert.equal(
                    rows('gpu_table')[i].cells[column + 4].textContent, Number(value).toLocaleString('en-US')));
            }
        });
        if (passes.some(pass => pass.pipeline_statistics.available))
            assert(node('gpu_statistics_status').textContent.includes('Pipeline counters captured'));
        else assert(node('gpu_statistics_status').textContent.length > 0);
        check_navigation('gpu');
        const measured = capture.gpu_frames.findIndex(frame => frame.passes.some(pass => pass.pipeline_statistics.available));
        assert.equal(node('gpu_statistics_frame').disabled, measured < 0);
        if (measured >= 0) {
            node('gpu_statistics_frame').click();
            assert(capture.gpu_frames[Number(node('gpu_frame').value)].passes.some(pass => pass.pipeline_statistics.available));
        }
    }
    if (capture.graphs.length) {
        const index = capture.graphs.length - 1;
        change('graph_frame', index);
        assert.equal(rows('graph_passes').length, capture.graphs[index].passes.length);
        assert.equal(rows('graph_resources').length, capture.graphs[index].resources.length);
    }
    for (let index = 0; index < capture.memory.samples.length; ++index) {
        change('memory_sample', index);
        const sample = capture.memory.samples[index];
        assert.equal(rows('allocations').length, sample.gpu_available ? sample.gpu.allocations.length : 0);
        if (!sample.gpu_available) assert(node('budget').textContent.includes('unavailable'));
    }
    assert.equal(errors.length, 0, errors.map(error => error.stack).join('\n'));
    // Exercise legacy captures and actual query failures without fabricating counts.
    if (capture.gpu_frames.length && path === process.argv[2]) {
        const variant = structuredClone(capture);
        variant.traceEvents = [];
        variant.graphs = [];
        variant.memory.samples = [];
        const measured = variant.gpu_frames.find(frame => frame.passes.some(pass => pass.pipeline_statistics.available));
        const data_block = /(<script id="profile_data" type="application\/json">)[\s\S]*?(<\/script>)/;
        const variant_html = () => html.replace(data_block, (_, start, end) => start +
            JSON.stringify(variant).replaceAll('<', '\\u003c') + end);
        for (const [status, message] of [[undefined, 'older capture'], ['readback_map_failed', 'could not be mapped']]) {
            for (const frame of variant.gpu_frames) {
                if (status) frame.pipeline_statistics_status = status;
                else delete frame.pipeline_statistics_status;
                for (const pass of frame.passes) pass.pipeline_statistics.available = false;
            }
            const view = load_report(variant_html(), new Map(), errors);
            assert(view.window.document.getElementById('gpu_statistics_status').textContent.includes(message));
            assert(view.window.document.getElementById('gpu_statistics_frame').disabled);
            for (const row of view.window.document.querySelectorAll('#gpu_table tbody tr'))
                assert.equal(row.cells[4].textContent, 'Unavailable');
            view.window.close();
        }
        if (measured) {
            const original = capture.gpu_frames.find(frame => frame.passes.some(pass => pass.pipeline_statistics.available));
            variant.gpu_frames = [variant.gpu_frames[0], structuredClone(original)];
            const view = load_report(variant_html(), new Map(), errors);
            assert.equal(view.window.document.getElementById('gpu_frame').value, '1');
            assert(view.window.document.getElementById('gpu_statistics_status').textContent.includes('Pipeline counters captured'));
            view.window.close();
        }
    }
    assert.equal(errors.length, 0, errors.map(error => error.stack).join('\n'));
    console.log('PASS ' + path + ' (offline data, zoom, pan, focus, counters and selectors)');
    dom.window.close();
}
