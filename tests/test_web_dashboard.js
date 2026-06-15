#!/usr/bin/env node
/**
 * @file test_web_dashboard.js
 * @brief Node tests for dashboard helpers in web/js/dashboardView.js.
 */
'use strict';

const dash = require('../web/js/dashboardView.js');
const escapeHtml = dash.escapeHtml;
const coerceBool = dash.coerceBool;
const pickStatusSlice = dash.pickStatusSlice;
const mergeStatusWithWs = dash.mergeStatusWithWs;

function assert(cond, msg) {
  if (!cond) {
    console.error('FAIL:', msg);
    process.exit(1);
  }
}

assert(escapeHtml(null) === '', 'null');
assert(escapeHtml(undefined) === '', 'undefined');
assert(escapeHtml('<script>alert(1)</script>').indexOf('<script>') === -1, 'script tag');
assert(escapeHtml('a & b').indexOf('&amp;') !== -1, 'ampersand');

assert(coerceBool(true) === true, 'bool true');
assert(coerceBool('false') === false, 'string false');
assert(coerceBool(0) === false, 'zero');
assert(coerceBool('1') === true, 'string one');

assert(dash.providerSemanticClass('primary', true) === 'dash-green', 'primary green');
assert(dash.discordSemanticClass('disconnected') === 'dash-red', 'discord red');

const html = dash.dashboardMarkup({ status: 'ok' }, { providers: [] }, {});
assert(html.indexOf('Dashboard') !== -1, 'dashboardMarkup');

const slice = pickStatusSlice({ type: 'provider_status', active_provider: 'stub', providers: [] });
assert(slice && slice.type === undefined && slice.active_provider === 'stub', 'pickStatusSlice');

const prevStatus = {
  active_provider: 'primary',
  providers: [{ name: 'primary', role: 'primary', reachable: true }],
  generated_at: '2026-01-01T00:00:00Z'
};
const wsDelta = {
  type: 'provider_status',
  active_provider: 'fallback',
  providers: [{ name: 'fallback', role: 'fallback', reachable: true }],
  last_error: 'primary unreachable'
};
const merged = mergeStatusWithWs(prevStatus, wsDelta);
assert(merged.active_provider === 'fallback', 'merge active_provider');
assert(merged.providers.length === 1 && merged.providers[0].name === 'fallback', 'merge providers');
assert(merged.last_error === 'primary unreachable', 'merge last_error');
assert(merged.generated_at === '2026-01-01T00:00:00Z', 'preserve generated_at');

const clearedErr = mergeStatusWithWs(prevStatus, { type: 'provider_status', last_error: null });
assert(clearedErr.last_error === null, 'merge clears last_error when null');

assert(mergeStatusWithWs(null, wsDelta) === null, 'merge null prev');
assert(mergeStatusWithWs(prevStatus, null) === prevStatus, 'merge null ws');

console.log('test_web_dashboard: all tests passed');
