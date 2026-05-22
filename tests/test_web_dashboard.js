#!/usr/bin/env node
/**
 * @file test_web_dashboard.js
 * @brief Node tests for dashboard helpers in web/js/dashboardView.js.
 */
'use strict';

const dash = require('../web/js/dashboardView.js');
const escapeHtml = dash.escapeHtml;
const coerceBool = dash.coerceBool;

function pickStatusSlice(wsMsg) {
  if (!wsMsg || typeof wsMsg !== 'object') return null;
  const o = JSON.parse(JSON.stringify(wsMsg));
  delete o.type;
  return o;
}

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

console.log('test_web_dashboard: all tests passed');
