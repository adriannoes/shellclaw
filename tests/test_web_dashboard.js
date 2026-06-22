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
assert(dash.providerSemanticClass('fallback', true) === 'dash-yellow', 'fallback yellow');
assert(dash.providerSemanticClass('standby', true) === 'dash-yellow', 'standby yellow');
assert(dash.discordSemanticClass('disconnected') === 'dash-red', 'discord red');
assert(dash.discordSemanticClass('connecting') === 'dash-yellow', 'discord connecting');

const html = dash.dashboardMarkup({ status: 'ok' }, { providers: [] }, {});
assert(html.indexOf('Dashboard') !== -1, 'dashboardMarkup');

const failoverHtml = dash.dashboardMarkup(
  { status: 'ok' },
  {
    active_provider: 'stub',
    last_error: 'stub-b unavailable',
    providers: [
      { name: 'stub-b', role: 'unavailable', reachable: false },
      { name: 'stub', role: 'fallback', reachable: true },
    ],
  },
  {},
);
assert(failoverHtml.indexOf('dash-banner-err') !== -1, 'last_error banner');
assert(failoverHtml.indexOf('stub *') !== -1, 'active provider mark');

const slice = pickStatusSlice({ type: 'provider_status', active_provider: 'stub', providers: [] });
assert(slice && slice.type === undefined && slice.active_provider === 'stub', 'pickStatusSlice');

console.log('test_web_dashboard: all tests passed');
