#include "profile_report.h"

#include <fstream>

namespace {
constexpr char report_header[] = R"PROFILE(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Renderer Profile</title><style>
:root{color-scheme:dark;--bg:#10151e;--panel:#19212e;--line:#334055;--text:#e7edf7;--muted:#a4b3c9;--accent:#74d6c5}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:14px system-ui,sans-serif}main{max-width:1500px;margin:auto;padding:32px}h1{font-size:30px;margin:0 0 8px}h2{font-size:21px}h3{font-size:16px}p{line-height:1.6}.muted,small{color:var(--muted)}nav{display:flex;gap:20px;margin:24px 0}a{color:var(--accent)}section{background:var(--panel);border:1px solid var(--line);border-radius:12px;padding:22px;margin:20px 0;scroll-margin-top:16px}.cards{display:flex;gap:14px;flex-wrap:wrap}.card{min-width:160px;background:var(--panel);border:1px solid var(--line);padding:16px;border-radius:10px}.card strong{display:block;font-size:24px;margin-top:7px}.controls{display:flex;align-items:center;gap:14px;flex-wrap:wrap;margin:16px 0}label{display:flex;gap:8px;align-items:center}input,select,button{font:inherit;color:var(--text);background:var(--bg);border:1px solid var(--line);border-radius:5px;padding:7px}input[type=number]{width:115px}button{cursor:pointer}.scroll{overflow:auto;max-height:460px;border:1px solid var(--line);border-radius:6px}table{border-collapse:collapse;width:100%;font-variant-numeric:tabular-nums}th,td{padding:9px 12px;text-align:left;border-bottom:1px solid var(--line);white-space:nowrap}th{position:sticky;top:0;background:#202c3d;z-index:1}td.wrap{white-space:normal;min-width:190px}canvas{display:block;width:100%;touch-action:pan-y}.timeline{overflow:auto;max-height:420px;background:var(--bg);border:1px solid var(--line);border-radius:6px}.chart{width:100%;height:190px;display:block}.legend{display:flex;gap:18px;flex-wrap:wrap;margin:8px 0}.warning{color:#ffcd86}#event_detail{white-space:pre-wrap;min-height:24px}.life{min-width:180px}.life span{display:block;height:12px;background:var(--accent);border-radius:2px}.empty{color:var(--muted);padding:16px}details{margin:12px 0}summary{cursor:pointer}#metadata td{white-space:normal}svg text{font:11px system-ui;fill:var(--muted)}@media(max-width:700px){main{padding:14px}section{padding:14px}nav{gap:12px;flex-wrap:wrap}}
</style></head><body><main>
<h1 id="title">Renderer Profile</h1><p id="subtitle" class="muted"></p><div id="summary" class="cards"></div>
<nav><a href="#cpu">CPU</a><a href="#gpu">GPU</a><a href="#memory">Memory</a><a href="#rdg">RenderGraph</a></nav>
<p id="dropped" class="warning"></p><details><summary>Capture metadata</summary><div id="metadata"></div></details>
<section id="cpu"><h2>CPU timeline</h2><p class="muted">Wall-clock scopes, including waits. Nested and overlapping scopes contribute inclusive time; totals are not CPU utilization.</p>
<p class="muted">Wheel: zoom at pointer · Shift + wheel or drag: pan · Double-click a scope: focus · Click: context · Keyboard: + / −, arrows, Home.</p>
<div class="controls"><label>Thread <select id="thread"></select></label><label>Search <input id="search" type="search" aria-label="CPU scope name"></label><label>From (ms) <input id="from" type="number" min="0" step="0.001"></label><label>To (ms) <input id="to" type="number" min="0" step="0.001"></label><button id="cpu_zoom_in" aria-label="Zoom in CPU timeline">Zoom +</button><button id="cpu_zoom_out" aria-label="Zoom out CPU timeline">Zoom −</button><button id="cpu_pan_left" aria-label="Pan CPU timeline left">←</button><button id="cpu_pan_right" aria-label="Pan CPU timeline right">→</button><button id="reset">Reset range</button></div>
<p id="cpu_status" class="muted"></p><div class="timeline"><canvas id="cpu_canvas" tabindex="0" style="touch-action:none;cursor:grab" aria-label="CPU scopes by thread"></canvas></div><p id="event_detail" class="muted"></p><h3>Scope ranking · visible range</h3><div id="cpu_table"></div></section>
<section id="gpu"><h2>GPU frames &amp; passes</h2><p class="muted">GPU offsets are relative to each GPU frame. CPU and GPU clocks are not calibrated and use separate timelines. Statistics use all captured frames, including warmup. Wheel: zoom · Drag: pan · Double-click a Pass: focus.</p><div id="gpu_summary" class="cards"></div><div id="gpu_chart"></div><div class="controls"><label>GPU frame <select id="gpu_frame"></select></label><button id="gpu_statistics_frame">Show counters frame</button></div><p id="gpu_statistics_status" class="muted"></p><div class="controls"><label>From (ms) <input id="gpu_from" type="number" min="0" step="0.001"></label><label>To (ms) <input id="gpu_to" type="number" min="0" step="0.001"></label><button id="gpu_zoom_in" aria-label="Zoom in GPU timeline">Zoom +</button><button id="gpu_zoom_out" aria-label="Zoom out GPU timeline">Zoom −</button><button id="gpu_pan_left" aria-label="Pan GPU timeline left">←</button><button id="gpu_pan_right" aria-label="Pan GPU timeline right">→</button><button id="gpu_reset">Reset range</button></div><div class="timeline"><canvas id="gpu_canvas" tabindex="0" style="touch-action:none;cursor:grab" aria-label="GPU passes in selected frame"></canvas></div><div id="gpu_table"></div><h3>Pass ranking · all captured frames</h3><div id="gpu_ranking"></div></section>
<section id="memory"><h2>CPU memory &amp; GPU allocations</h2><p id="coverage" class="muted"></p><p class="muted">Samples are stage boundaries, not a continuous peak measurement. Cached, pending release, upload and readback are subsets of owned GPU bytes. External allocations and OS budgets are separate.</p><div id="memory_chart"></div><div id="memory_table"></div><div class="controls"><label>Allocation snapshot <select id="memory_sample"></select></label></div><p id="budget" class="muted"></p><div id="allocations"></div></section>
<section id="rdg"><h2>RenderGraph</h2><div class="controls"><label>Graph frame <select id="graph_frame"></select></label></div><p class="muted">Dependencies refer to Pass IDs. Resource lifetimes use compiled execution indices; allocation IDs correlate with memory snapshots. Shared IDs indicate object pool reuse.</p><h3>Pass dependencies &amp; resource access</h3><div id="graph_passes"></div><h3>Resource lifetimes</h3><div id="graph_resources"></div></section>
<p class="muted">Renderer Profile · self-contained offline capture</p></main>
<script id="profile_data" type="application/json">
)PROFILE";

constexpr char report_footer[] = R"PROFILE(
</script><script>
'use strict';
const report = JSON.parse(document.getElementById('profile_data').textContent);
const by_id = (id) => document.getElementById(id);
const format = (value, digits = 3) =>
  Number(value || 0).toLocaleString('en-US', { maximumFractionDigits: digits });
const ms = (value) => format(value) + ' ms';
const mib = (value) => format(Number(value) / 1048576, 2) + ' MiB';
const absent = 'Unavailable';
function element(tag, text) {
  const node = document.createElement(tag);
  if (text !== undefined) node.textContent = String(text);
  return node;
}
function table(id, headers, rows) {
  const host = by_id(id);
  host.replaceChildren();
  if (!rows.length) {
    host.append(element('p', 'No captured data.'));
    return;
  }
  const scroll = element('div');
  scroll.className = 'scroll';
  const grid = element('table');
  const head = element('thead'),
    tr = element('tr');
  for (const name of headers) tr.append(element('th', name));
  head.append(tr);
  grid.append(head);
  const body = element('tbody');
  for (const row of rows) {
    const line = element('tr');
    for (const value of row) {
      const cell = element('td');
      if (value instanceof Node) cell.append(value);
      else cell.textContent = String(value);
      line.append(cell);
    }
    body.append(line);
  }
  grid.append(body);
  scroll.append(grid);
  host.append(scroll);
}
function wrapped(text) {
  const node = element('span', text);
  node.style.whiteSpace = 'normal';
  return node;
}
function cards(id, values) {
  by_id(id).replaceChildren(
    ...values.map(([label, value]) => {
      const node = element('div');
      node.className = 'card';
      node.append(element('small', label), element('strong', value));
      return node;
    }),
  );
}
function options(id, items) {
  by_id(id).replaceChildren(
    ...items.map(([value, label]) => {
      const node = element('option', label);
      node.value = String(value);
      return node;
    }),
  );
  by_id(id).disabled = !items.length;
}
function stats(values) {
  if (!values.length) return [absent, absent];
  const sorted = [...values].sort((a, b) => a - b);
  return [
    ms(values.reduce((a, b) => a + b, 0) / values.length),
    ms(sorted[Math.max(0, Math.ceil(sorted.length * 0.95) - 1)]),
  ];
}
function chart(id, series) {
  const host = by_id(id);
  host.replaceChildren();
  const points = series.flatMap((s) => s.points.filter((p) => p[1] !== null));
  if (!points.length) {
    host.append(element('p', 'No captured data.'));
    return;
  }
  const ns = 'http://www.w3.org/2000/svg';
  const make = (tag, attributes) => {
    const node = document.createElementNS(ns, tag);
    for (const [key, value] of Object.entries(attributes)) node.setAttribute(key, value);
    return node;
  };
  const svg = make('svg', {
    viewBox: '0 0 1100 190',
    class: 'chart',
    role: 'img',
    'aria-label': series.map((s) => s.name).join(', '),
  });
  const xmin = points.reduce((value, p) => Math.min(value, p[0]), Infinity),
    xmax = points.reduce((value, p) => Math.max(value, p[0]), xmin + 1);
  let ymax = 1;
  for (const p of points) ymax = Math.max(ymax, p[1]);
  const x = (v) => 70 + ((v - xmin) / (xmax - xmin)) * 1000,
    y = (v) => 155 - (v / ymax) * 135;
  for (let i = 0; i <= 4; i++) {
    const value = (ymax * i) / 4;
    svg.append(make('line', { x1: 70, x2: 1070, y1: y(value), y2: y(value), stroke: '#334055' }));
    const label = make('text', { x: 4, y: y(value) + 4 });
    label.textContent = format(value, 1);
    svg.append(label);
  }
  for (const [value, anchor] of [
    [xmin, 'start'],
    [xmax, 'end'],
  ]) {
    const label = make('text', { x: x(value), y: 182, 'text-anchor': anchor });
    label.textContent = format(value, 1);
    svg.append(label);
  }
  const legend = element('div');
  legend.className = 'legend';
  for (const s of series) {
    const label = element('span', s.name);
    label.style.color = s.color;
    legend.append(label);
    let segment = [];
    const flush = () => {
      if (!segment.length) return;
      svg.append(
        make('polyline', {
          points: segment.map((p) => x(p[0]) + ',' + y(p[1])).join(' '),
          fill: 'none',
          stroke: s.color,
          'stroke-width': 2,
        }),
      );
      for (const p of segment) {
        const dot = make('circle', { cx: x(p[0]), cy: y(p[1]), r: 3, fill: s.color });
        const title = make('title', {});
        title.textContent = p[2] || `${format(p[0])}: ${format(p[1])}`;
        dot.append(title);
        svg.append(dot);
      }
      segment = [];
    };
    for (const p of s.points) {
      if (p[1] === null) flush();
      else segment.push(p);
    }
    flush();
  }
  host.append(legend, svg);
}
const events = (report.traceEvents || []).filter((e) => e.ph === 'X');
const threads = new Map();
for (const event of report.traceEvents || [])
  if (event.ph === 'M' && event.name === 'thread_name')
    threads.set(String(event.tid), event.args.name);
for (const event of events)
  if (!threads.has(String(event.tid))) threads.set(String(event.tid), 'Thread ' + event.tid);
const duration = Math.max(Number(report.duration_ms) || 0, 0);
const gpu_frames = report.gpu_frames || [],
  graphs = report.graphs || [],
  samples = report.memory?.samples || [];
document.title = report.name + ' · Renderer Profile';
by_id('title').textContent = report.name + ' · Profile';
by_id('subtitle').textContent =
  `${ms(duration)} capture · ${events.length.toLocaleString()} CPU scopes · ${gpu_frames.length} GPU frames · ${graphs.length} render graphs`;
cards('summary', [
  ['Capture duration', ms(duration)],
  ['CPU scopes', format(events.length, 0)],
  ['GPU frames', format(gpu_frames.length, 0)],
  ['Memory samples', format(samples.length, 0)],
]);
by_id('dropped').textContent = [
  report.dropped_cpu_events ? `${format(report.dropped_cpu_events, 0)} CPU events were dropped. CPU timelines and rankings are incomplete.` : '',
  report.dropped_graphs ? `${format(report.dropped_graphs, 0)} graph captures were dropped because the serialization backlog was full or no worker pool was available.` : ''
].filter(Boolean).join(' ');
table('metadata', ['Setting', 'Value'], Object.entries(report.metadata || {}));
options('thread', [
  ['all', 'All threads'],
  ...[...threads].map(([id, name]) => [id, name + ' (#' + id + ')']),
]);
const colors = ['#74d6c5', '#83acff', '#d4a6ef', '#f4bc78', '#ef96a7'];
function color(name) {
  let hash = 0;
  for (const c of name) hash = (hash * 31 + c.charCodeAt(0)) >>> 0;
  return colors[hash % colors.length];
}
function canvas(id, height) {
  const node = by_id(id),
    width = Math.max(600, node.parentElement.clientWidth),
    ratio = window.devicePixelRatio || 1;
  node.width = width * ratio;
  node.height = height * ratio;
  node.style.width = width + 'px';
  node.style.height = height + 'px';
  const context = node.getContext('2d');
  context.scale(ratio, ratio);
  context.font = '11px system-ui';
  return { node, context, width };
}
let cpu_hits = [];
const thread_events = new Map();
for (const e of events) {
  const key = String(e.tid);
  if (!thread_events.has(key)) thread_events.set(key, []);
  thread_events.get(key).push(e);
}
// Assign lanes once per capture, preserving nesting without creating a DOM node per scope.
for (const list of thread_events.values()) {
  list.sort((a, b) => a.ts - b.ts || b.dur - a.dur);
  const ends = [];
  for (const e of list) {
    let lane = 0;
    while (lane < ends.length && ends[lane] > e.ts) lane++;
    ends[lane] = e.ts + e.dur;
    e.lane = lane;
  }
}
)PROFILE"
R"PROFILE(function timeline_controls(prefix, extent, hits, draw) {
  const node = by_id(prefix + '_canvas');
  const from = by_id(prefix === 'cpu' ? 'from' : prefix + '_from');
  const to = by_id(prefix === 'cpu' ? 'to' : prefix + '_to');
  const reset = by_id(prefix === 'cpu' ? 'reset' : prefix + '_reset');
  const limit = () => Math.max(0.001, extent());
  const range = () => {
    const start = Number(from.value), end = Number(to.value);
    return Number.isFinite(start) && Number.isFinite(end) && end > start
      ? [start, end] : [0, limit()];
  };
  const set_range = (start, end) => {
    const span = Math.min(limit(), Math.max(0.001, end - start));
    start = Math.max(0, Math.min(limit() - span, start));
    from.value = start;
    to.value = start + span;
    draw();
  };
  const zoom = (factor, anchor = 0.5) => {
    const [start, end] = range(), span = end - start;
    const next_span = Math.min(limit(), Math.max(0.001, span * factor));
    const next_start = start + span * anchor - next_span * anchor;
    set_range(next_start, next_start + next_span);
  };
  const pan = (fraction) => {
    const [start, end] = range(), offset = (end - start) * fraction;
    set_range(start + offset, end + offset);
  };
  const point = event => {
    const rect = node.getBoundingClientRect();
    return { x: (event.clientX - rect.left) * node.clientWidth / rect.width,
      y: event.clientY - rect.top };
  };
  const find_hit = event => {
    const { x, y } = point(event);
    return hits().findLast(hit => x >= hit.x && x <= hit.x + hit.w && y >= hit.y && y <= hit.y + 17);
  };
  node.addEventListener('wheel', event => {
    if (!event.deltaY && !event.deltaX) return;
    event.preventDefault();
    if (event.shiftKey || Math.abs(event.deltaX) > Math.abs(event.deltaY)) {
      pan(Math.sign(event.deltaX || event.deltaY) * 0.15);
    } else {
      const anchor = Math.max(0, Math.min(1, (point(event).x - 150) / (node.clientWidth - 166)));
      zoom(Math.exp(Math.max(-1, Math.min(1, event.deltaY * 0.002))), anchor);
    }
  }, { passive: false });
  let drag = null, dragged = false;
  node.addEventListener('pointerdown', event => {
    if (event.button !== 0) return;
    const [start, end] = range();
    drag = { id: event.pointerId, x: point(event).x, start, end };
    dragged = false;
    node.setPointerCapture(event.pointerId);
    node.style.cursor = 'grabbing';
    node.focus({ preventScroll: true });
  });
  node.addEventListener('pointermove', event => {
    if (!drag || event.pointerId !== drag.id) return;
    const delta = point(event).x - drag.x;
    if (Math.abs(delta) < 3 && !dragged) return;
    dragged = true;
    const offset = -delta / (node.clientWidth - 166) * (drag.end - drag.start);
    set_range(drag.start + offset, drag.end + offset);
  });
  const end_drag = event => {
    if (!drag || event.pointerId !== drag.id) return;
    drag = null;
    node.style.cursor = 'grab';
    if (node.hasPointerCapture(event.pointerId)) node.releasePointerCapture(event.pointerId);
  };
  node.addEventListener('pointerup', end_drag);
  node.addEventListener('pointercancel', end_drag);
  node.addEventListener('lostpointercapture', () => { drag = null; node.style.cursor = 'grab'; });
  node.addEventListener('click', event => {
    if (dragged) { event.stopImmediatePropagation(); event.preventDefault(); dragged = false; }
  }, true);
  node.addEventListener('dblclick', event => {
    const hit = find_hit(event);
    if (hit) {
      const padding = Math.max(0.001, (hit.end - hit.start) * 0.05);
      set_range(hit.start - padding, hit.end + padding);
    }
  });
  node.addEventListener('keydown', event => {
    if (event.key === '+' || event.key === '=') zoom(0.5);
    else if (event.key === '-' || event.key === '_') zoom(2);
    else if (event.key === 'ArrowLeft') pan(-0.2);
    else if (event.key === 'ArrowRight') pan(0.2);
    else if (event.key === 'Home') set_range(0, limit());
    else return;
    event.preventDefault();
  });
  by_id(prefix + '_zoom_in').onclick = () => zoom(0.5);
  by_id(prefix + '_zoom_out').onclick = () => zoom(2);
  by_id(prefix + '_pan_left').onclick = () => pan(-0.2);
  by_id(prefix + '_pan_right').onclick = () => pan(0.2);
  reset.onclick = () => set_range(0, limit());
  return { reset: reset.onclick, find_hit };
}
)PROFILE"
R"PROFILE(function render_cpu() {
  const start = Number(by_id('from').value),
    end = Number(by_id('to').value),
    thread = by_id('thread').value,
    query = by_id('search').value.toLowerCase();
  if (!Number.isFinite(start) || !Number.isFinite(end) || end <= start) {
    by_id('cpu_status').textContent = 'Choose a valid time range with To greater than From.';
    return;
  }
  const visible = events.filter(
    (e) =>
      (thread === 'all' || String(e.tid) === thread) &&
      e.name.toLowerCase().includes(query) &&
      e.ts / 1000 < end &&
      (e.ts + e.dur) / 1000 >= start,
  );
  by_id('cpu_status').textContent =
    `${format(visible.length, 0)} scopes · ${ms(start)} – ${ms(end)}`;
  const lanes = new Map();
  for (const e of visible)
    lanes.set(String(e.tid), Math.max(lanes.get(String(e.tid)) || 0, e.lane + 1));
  let height = 30;
  const tops = new Map();
  for (const [id, count] of lanes) {
    tops.set(id, height);
    height += 24 + count * 20;
  }
  const { context: c, width } = canvas('cpu_canvas', Math.max(80, height));
  cpu_hits = [];
  const left = 150,
    scale = (width - left - 16) / (end - start);
  c.fillStyle = '#a4b3c9';
  c.fillText(ms(start), left, 17);
  c.fillText(ms(end), width - 95, 17);
  for (const [id, top] of tops) c.fillText((threads.get(id) || id).slice(0, 22), 8, top + 14);
  const ranking = new Map();
  for (const e of visible) {
    const a = Math.max(start, e.ts / 1000),
      b = Math.min(end, (e.ts + e.dur) / 1000),
      x = left + (a - start) * scale,
      y = tops.get(String(e.tid)) + e.lane * 20,
      w = Math.max(1, (b - a) * scale);
    c.fillStyle = color(e.name);
    c.fillRect(x, y, w, 17);
    if (w > 35) {
      c.save();
      c.beginPath();
      c.rect(x, y, w, 17);
      c.clip();
      c.fillStyle = '#10151e';
      c.fillText(e.name, x + 3, y + 12);
      c.restore();
    }
    cpu_hits.push({ x, y, w, event: e, start: e.ts / 1000, end: (e.ts + e.dur) / 1000 });
    const row = ranking.get(e.name) || { count: 0, total: 0, max: 0 };
    row.count++;
    row.total += Math.max(0, b - a);
    row.max = Math.max(row.max, Math.max(0, b - a));
    ranking.set(e.name, row);
  }
  table(
    'cpu_table',
    ['Scope', 'Calls', 'Inclusive total', 'Mean', 'Max'],
    [...ranking]
      .sort((a, b) => b[1].total - a[1].total)
      .map(([name, s]) => [name, s.count, ms(s.total), ms(s.total / s.count), ms(s.max)]),
  );
}
const cpu_navigation = timeline_controls('cpu', () => duration, () => cpu_hits, render_cpu);
by_id('cpu_canvas').onclick = (event) => {
  const hit = cpu_navigation.find_hit(event);
  if (hit) {
    const e = hit.event,
      a = e.args || {};
    const frame = Number(a.frame_id) >= Number.MAX_SAFE_INTEGER ? 'none' : a.frame_id;
    by_id('event_detail').textContent =
      `${e.name} · ${threads.get(String(e.tid))} (#${e.tid}) · start ${ms(e.ts / 1000)} · duration ${ms(e.dur / 1000)}\nFrame: ${frame} · operation: ${a.operation_id} · Pass: ${a.pass_id === 4294967295 ? 'none' : a.pass_id}`;
  }
};
for (const id of ['from', 'to', 'thread']) by_id(id).onchange = render_cpu;
by_id('search').oninput = render_cpu;
cpu_navigation.reset();
const [gpu_mean, gpu_p95] = stats(gpu_frames.map((f) => f.gpu_ms));
cards('gpu_summary', [
  ['Mean GPU frame', gpu_mean],
  ['P95 GPU frame', gpu_p95],
]);
chart('gpu_chart', [
  {
    name: 'GPU frame time (ms) · x: frame ID',
    color: colors[0],
    points: gpu_frames.map((f) => [f.frame_id, f.gpu_ms, `Frame ${f.frame_id}: ${ms(f.gpu_ms)}`]),
  },
]);
options(
  'gpu_frame',
  gpu_frames.map((f, i) => [i, 'Frame ' + f.frame_id + ' · ' + ms(f.gpu_ms) +
    ((f.passes || []).some(p => p.pipeline_statistics?.available) ? ' · counters' : '')]),
);
let gpu_hits = [];
function render_gpu() {
  const frame = gpu_frames[Number(by_id('gpu_frame').value)],
    passes = frame?.passes || [];
  const start = Number(by_id('gpu_from').value), end = Number(by_id('gpu_to').value);
  if (!Number.isFinite(start) || !Number.isFinite(end) || end <= start) return;
  const { context: c, width } = canvas('gpu_canvas', Math.max(60, passes.length * 24 + 30));
  const scale = (width - 166) / (end - start);
  gpu_hits = [];
  c.fillStyle = '#a4b3c9';
  c.fillText(ms(start), 150, 10);
  c.fillText(ms(end), width - 95, 10);
  passes.forEach((p, i) => {
    c.fillStyle = '#a4b3c9';
    c.fillText(p.name.slice(0, 22), 8, 34 + i * 24);
    const a = Math.max(start, p.offset_ms), b = Math.min(end, p.offset_ms + p.time_ms);
    if (b < a || p.offset_ms >= end) return;
    const x = 150 + (a - start) * scale, y = 21 + i * 24, w = Math.max(1, (b - a) * scale);
    c.fillStyle = color(p.name);
    c.fillRect(x, y, w, 17);
    gpu_hits.push({ x, y, w, start: p.offset_ms, end: p.offset_ms + p.time_ms });
  });
  const measured = passes.filter(p => p.pipeline_statistics?.available).length;
  const status = frame?.pipeline_statistics_status;
  const failure_reasons = {
    query_heap_failed: 'GPU pipeline query heap creation failed.',
    backend_unavailable: 'The GPU backend was unavailable during capture.',
    readback_allocation_failed: 'GPU counter readback allocation failed.',
    readback_map_failed: 'GPU counter readback could not be mapped.',
    no_scopes: 'This frame has no GPU Pass queries.',
    not_requested: 'Pipeline counters were not requested for this frame.',
  };
  by_id('gpu_statistics_status').textContent = measured
    ? `Pipeline counters captured for ${measured} / ${passes.length} Passes. Zero is a measured count (for example CS on a graphics Pass).`
    : !frame ? 'No GPU frames were captured.'
    : (failure_reasons[status] || 'This older capture has no pipeline counters for the selected frame. Re-run with the current --profile; older builds required --profile-probes and frame 30.');
  table(
    'gpu_table',
    [
      'Pass',
      'ID',
      'Offset',
      'GPU time',
      'IA vertices',
      'VS invocations',
      'PS invocations',
      'CS invocations',
    ],
    passes.map((p) => {
      const s = p.pipeline_statistics;
      return [
        p.name,
        p.pass_id,
        ms(p.offset_ms),
        ms(p.time_ms),
        ...[
          s?.input_vertices,
          s?.vertex_invocations,
          s?.pixel_invocations,
          s?.compute_invocations,
        ].map((v) => (s?.available ? format(v, 0) : absent)),
      ];
    }),
  );
}
const pass_totals = new Map();
for (const f of gpu_frames)
  for (const p of f.passes || []) {
    const s = pass_totals.get(p.name) || [];
    s.push(p.time_ms);
    pass_totals.set(p.name, s);
  }
table(
  'gpu_ranking',
  ['Pass', 'Samples', 'Mean', 'P95', 'Total'],
  [...pass_totals]
    .sort((a, b) => b[1].reduce((x, y) => x + y, 0) - a[1].reduce((x, y) => x + y, 0))
    .map(([name, values]) => [
      name,
      values.length,
      ...stats(values),
      ms(values.reduce((a, b) => a + b, 0)),
    ]),
);
const gpu_navigation = timeline_controls('gpu',
  () => gpu_frames[Number(by_id('gpu_frame').value)]?.gpu_ms || 0.001,
  () => gpu_hits, render_gpu);
const measured_frames = gpu_frames.map((f, i) =>
  (f.passes || []).some(p => p.pipeline_statistics?.available) ? i : -1).filter(i => i >= 0);
by_id('gpu_statistics_frame').disabled = !measured_frames.length;
by_id('gpu_statistics_frame').onclick = () => {
  const current = Number(by_id('gpu_frame').value);
  by_id('gpu_frame').value = measured_frames.find(i => i > current) ?? measured_frames[0];
  gpu_navigation.reset();
};
if (measured_frames.length) by_id('gpu_frame').value = measured_frames[0];
by_id('gpu_frame').onchange = gpu_navigation.reset;
for (const id of ['gpu_from', 'gpu_to']) by_id(id).onchange = render_gpu;
gpu_navigation.reset();
)PROFILE"
R"PROFILE(by_id('coverage').textContent = report.cpu_memory_coverage || '';
chart(
  'memory_chart',
  [
    {
      name: 'Process private (MiB) · x: capture ms',
      color: colors[0],
      value: (s) => s.process_private_bytes,
    },
    { name: 'Working set (MiB)', color: colors[1], value: (s) => s.working_set_bytes },
    { name: 'Tracked CPU (MiB)', color: colors[2], value: (s) => s.tracked.current_bytes },
    {
      name: 'Owned GPU (MiB)',
      color: colors[3],
      value: (s) => (s.gpu_available ? s.gpu.allocated_bytes : null),
    },
  ].map((s) => ({
    ...s,
    points: samples.map((p) => {
      const value = s.value(p);
      return [
        p.time_ms,
        value === null ? null : value / 1048576,
        p.name + ': ' + (value === null ? absent : mib(value)),
      ];
    }),
  })),
);
table(
  'memory_table',
  [
    'Stage',
    'Time',
    'Private',
    'Working set',
    'Tracked CPU',
    'Owned GPU',
    'Cached GPU',
    'Pending release',
    'Upload',
    'Readback',
    'External',
  ],
  samples.map((s) => [
    s.name,
    ms(s.time_ms),
    mib(s.process_private_bytes),
    mib(s.working_set_bytes),
    mib(s.tracked.current_bytes),
    ...[
      'allocated_bytes',
      'cached_bytes',
      'pending_release_bytes',
      'upload_bytes',
      'readback_bytes',
      'external_bytes',
    ].map((k) => (s.gpu_available ? mib(s.gpu[k]) : absent)),
  ]),
);
options(
  'memory_sample',
  samples.map((s, i) => [i, s.name + ' · ' + ms(s.time_ms)]),
);
function render_allocations() {
  const s = samples[Number(by_id('memory_sample').value)];
  by_id('budget').textContent = !s?.gpu_available
    ? 'GPU memory is unavailable for this sample.'
    : s.gpu.budget_available
      ? `OS local usage / budget: ${mib(s.gpu.local_usage)} / ${mib(s.gpu.local_budget)} · nonlocal: ${mib(s.gpu.nonlocal_usage)} / ${mib(s.gpu.nonlocal_budget)}`
      : 'OS GPU budget is unavailable for this sample.';
  table(
    'allocations',
    ['Allocation ID', 'Resource', 'Category', 'Bytes', 'Ownership', 'Cache'],
    s?.gpu_available
      ? [...(s.gpu.allocations || [])]
          .sort((a, b) => b.bytes - a.bytes)
          .map((a) => [
            a.id,
            a.name,
            a.category,
            mib(a.bytes),
            a.external ? 'External' : 'Owned',
            a.cached ? 'Cached' : 'Active',
          ])
      : [],
  );
}
by_id('memory_sample').onchange = render_allocations;
render_allocations();
options(
  'graph_frame',
  graphs.map((g, i) => [i, 'Frame ' + g.frame_id]),
);
function render_graph() {
  const graph = graphs[Number(by_id('graph_frame').value)],
    passes = graph?.passes || [],
    resources = graph?.resources || [];
  const pass_names = new Map(passes.map((p) => [p.id, p.name])),
    resource_names = new Map(resources.map((r) => [r.id, r.name]));
  table(
    'graph_passes',
    ['ID', 'Pass', 'Execution index', 'State', 'Dependencies', 'Accesses'],
    [...passes]
      .sort((a, b) => a.order - b.order)
      .map((p) => [
        p.id,
        p.name,
        p.culled ? '—' : p.order,
        p.culled ? 'Culled' : 'Executed',
        wrapped(
          (p.dependencies || []).map((id) => `#${id} ${pass_names.get(id) || ''}`).join(', ') ||
            'None',
        ),
        wrapped(
          (p.accesses || [])
            .map(
              (a) =>
                `${a.output ? 'Write' : 'Read'} #${a.resource_id} ${resource_names.get(a.resource_id) || ''}`,
            )
            .join(', ') || 'None',
        ),
      ]),
  );
  let count = 1;
  for (const p of passes) if (!p.culled) count = Math.max(count, p.order + 1);
  table(
    'graph_resources',
    [
      'ID',
      'Resource',
      'Origin',
      'First use',
      'Last use',
      'Lifetime (execution indices)',
      'Allocation ID',
    ],
    resources.map((r) => {
      const valid = r.first_use !== 4294967295 && r.last_use !== 4294967295;
      const life = element('div');
      life.className = 'life';
      if (valid) {
        const bar = element('span');
        bar.style.marginLeft = (r.first_use / count) * 100 + '%';
        bar.style.width = ((r.last_use - r.first_use + 1) / count) * 100 + '%';
        bar.title = `${r.first_use} – ${r.last_use}`;
        life.append(bar);
      } else life.textContent = 'Unused';
      return [
        r.id,
        r.name,
        r.imported ? 'Imported' : 'Transient',
        valid ? r.first_use : '—',
        valid ? r.last_use : '—',
        life,
        r.allocation_id || 'Unresolved',
      ];
    }),
  );
}
by_id('graph_frame').onchange = render_graph;
render_graph();
let resize_timer;
window.addEventListener('resize', () => {
  clearTimeout(resize_timer);
  resize_timer = setTimeout(() => {
    render_cpu();
    render_gpu();
  }, 100);
});
</script></body></html>
)PROFILE";
}

bool write_profile_html(const std::filesystem::path& path, std::string_view report) {
    std::ofstream output(path, std::ios::binary);
    if (!output) return false;
    output << report_header;
    // JSON script blocks are raw HTML text: escape '<' so names cannot close the
    // block (including mixed-case </script>) or introduce HTML comment parsing.
    size_t start = 0;
    for (size_t i = 0; i < report.size(); ++i) {
        if (report[i] != '<' && report[i] != '>' && report[i] != '&') continue;
        output.write(report.data() + start, static_cast<std::streamsize>(i - start));
        output << (report[i] == '<' ? "\\u003c" : report[i] == '>' ? "\\u003e" : "\\u0026");
        start = i + 1;
    }
    output.write(report.data() + start, static_cast<std::streamsize>(report.size() - start));
    output << report_footer;
    output.close();
    return bool(output);
}
