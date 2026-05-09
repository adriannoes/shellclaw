/* ShellClaw Web UI - vanilla JS SPA */
(function () {
  const TOKEN_KEY = 'shellclaw_token';
  const API_BASE = '';

  function escapeHtml(str) {
    if (str == null || str === undefined) return '';
    const s = String(str);
    const div = document.createElement('div');
    div.textContent = s;
    return div.innerHTML;
  }

  function getToken() { return localStorage.getItem(TOKEN_KEY); }
  function setToken(t) { if (t) localStorage.setItem(TOKEN_KEY, t); else localStorage.removeItem(TOKEN_KEY); }

  function api(method, path, body) {
    const opts = { method, headers: {} };
    const token = getToken();
    if (token) opts.headers['Authorization'] = 'Bearer ' + token;
    if (body) {
      opts.headers['Content-Type'] = 'application/json';
      opts.body = typeof body === 'string' ? body : JSON.stringify(body);
    }
    return fetch(API_BASE + path, opts).then(r => {
      if (r.status === 401) { setToken(null); window.location.hash = '#/pair'; return Promise.reject('Unauthorized'); }
      return r.json().catch(() => ({}));
    });
  }

  function wsConnect(onMessage) {
    const token = getToken();
    if (!token) return null;
    const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
    const ws = new WebSocket(proto + '//' + location.host + '/ws?token=' + encodeURIComponent(token));
    ws.onmessage = e => { try { const d = JSON.parse(e.data); onMessage(d); } catch (_) {} };
    return ws;
  }

  const routes = {
    '/': renderDashboard,
    '/chat': renderChat,
    '/config': renderConfig,
    '/skills': renderSkills,
    '/memory': renderMemory,
    '/sessions': renderSessions,
    '/cron': renderCron,
    '/logs': renderLogs,
    '/asap': renderAsap,
    '/pair': renderPair
  };

  function getRoute() {
    const h = window.location.hash.slice(1) || '/';
    return h.split('?')[0];
  }

  function render(el, html) {
    el.innerHTML = html;
  }

  function renderDashboard() {
    const token = getToken();
    if (!token) { window.location.hash = '#/pair'; return; }
    api('GET', '/health').then(d => {
      const sandboxEnabled = d.sandbox_enabled != null ? d.sandbox_enabled : false;
      const asapEnabled = d.asap_enabled != null ? d.asap_enabled : false;
      const asapError = d.asap_last_error || '';
      const registryTs = d.registry_last_fetch || '';
      render(app, '<h1>Dashboard</h1>' +
        '<div class="dashboard-cards">' +
        '<div class="card"><div class="card-label">Status</div><div class="card-value status-ok">' + escapeHtml(d.status || 'ok') + '</div></div>' +
        '<div class="card"><div class="card-label">Uptime</div><div class="card-value">' + escapeHtml(d.uptime != null ? d.uptime + 's' : '-') + '</div></div>' +
        '<div class="card"><div class="card-label">Version</div><div class="card-value">' + escapeHtml(d.version || '-') + '</div></div>' +
        '<div class="card"><div class="card-label">Sandbox</div><div class="card-value ' + (sandboxEnabled ? 'status-ok' : 'status-warn') + '">' + (sandboxEnabled ? 'enabled' : 'disabled') + '</div></div>' +
        '<div class="card"><div class="card-label">ASAP</div><div class="card-value ' + (asapEnabled ? 'status-ok' : 'status-warn') + '">' + (asapEnabled ? 'connected' : 'disabled') + '</div></div>' +
        '<div class="card"><div class="card-label">Registry</div><div class="card-value">' + escapeHtml(registryTs || 'never') + '</div></div>' +
        (asapError ? '<div class="card card-full"><div class="card-label status-err">Last ASAP Error</div><div class="card-value">' + escapeHtml(asapError) + '</div></div>' : '') +
        '</div>');
    }).catch(e => {
      render(app, '<h1>Dashboard</h1><p class="status-err">Failed to fetch: ' + escapeHtml(e || 'Unknown') + '</p>');
    });
  }

  function renderChat() {
    const token = getToken();
    if (!token) { window.location.hash = '#/pair'; return; }
    render(app, '<h1>Chat</h1><div id="chat-log"></div><input id="chat-input" placeholder="Type message...">');
    const log = document.getElementById('chat-log');
    const input = document.getElementById('chat-input');
    const ws = wsConnect(function (msg) {
      if (msg.type === 'message') log.textContent += (msg.text || '') + '\n';
      log.scrollTop = log.scrollHeight;
    });
    if (!ws) { log.textContent = 'Not authenticated. Go to pairing.'; return; }
    input.onkeydown = function (e) {
      if (e.key !== 'Enter') return;
      const text = input.value.trim();
      if (!text) return;
      ws.send(JSON.stringify({ type: 'message', text }));
      log.textContent += 'You: ' + text + '\n';
      input.value = '';
      log.scrollTop = log.scrollHeight;
    };
  }

  function renderConfig() {
    const token = getToken();
    if (!token) { window.location.hash = '#/pair'; return; }
    api('GET', '/api/config').then(d => {
      render(app, '<h1>Config</h1><form class="config-form" id="config-form">' +
        '<div class="form-row"><label>Model</label><input name="model" value="' + escapeHtml(d.model || '') + '"></div>' +
        '<div class="form-row"><label>Max tokens</label><input type="number" name="max_tokens" value="' + escapeHtml(String(d.max_tokens ?? 4096)) + '"></div>' +
        '<div class="form-row"><label>Temperature</label><input type="number" step="0.1" name="temperature" value="' + escapeHtml(String(d.temperature ?? 0.7)) + '"></div>' +
        '<div class="form-row"><label>Gateway host</label><input name="gateway_host" value="' + escapeHtml(d.gateway_host || '') + '"></div>' +
        '<div class="form-row"><label>Gateway port</label><input type="number" name="gateway_port" value="' + escapeHtml(String(d.gateway_port ?? 18789)) + '"></div>' +
        '<button type="submit">Save</button></form>');
      document.getElementById('config-form').onsubmit = function (e) {
        e.preventDefault();
        const f = e.target;
        const body = {
          model: f.model.value,
          max_tokens: parseInt(f.max_tokens.value, 10),
          temperature: parseFloat(f.temperature.value),
          gateway_host: f.gateway_host.value,
          gateway_port: parseInt(f.gateway_port.value, 10)
        };
        api('PUT', '/api/config', body).then(() => { alert('Saved'); }).catch(err => alert('Error: ' + err));
      };
    }).catch(e => { render(app, '<h1>Config</h1><p class="status-err">' + escapeHtml(e || 'Load failed') + '</p>'); });
  }

  function renderSkills() {
    const token = getToken();
    if (!token) { window.location.hash = '#/pair'; return; }
    api('GET', '/api/skills').then(arr => {
      let html = '<h1>Skills</h1><ul class="skill-list" id="skill-list"></ul>' +
        '<h2>Create skill</h2><form id="skill-create"><input name="name" placeholder="Name"><textarea name="content" placeholder="Content" rows="4"></textarea><button type="submit">Create</button></form>';
      render(app, html);
      const ul = document.getElementById('skill-list');
      (arr || []).forEach(n => {
        const li = document.createElement('li');
        li.innerHTML = '<span>' + escapeHtml(n) + '</span><span><a href="#/skills/' + encodeURIComponent(n) + '">View</a> <button class="danger" data-delete="' + escapeHtml(n) + '">Delete</button></span>';
        li.querySelector('[data-delete]').onclick = () => {
          if (!confirm('Delete ' + n + '?')) return;
          api('DELETE', '/api/skills/' + encodeURIComponent(n)).then(() => { window.location.hash = '#/skills'; });
        };
        ul.appendChild(li);
      });
      document.getElementById('skill-create').onsubmit = function (e) {
        e.preventDefault();
        const f = e.target;
        api('POST', '/api/skills', { name: f.name.value, content: f.content.value }).then(() => { window.location.hash = '#/skills'; });
      };
    }).catch(e => { render(app, '<h1>Skills</h1><p class="status-err">' + escapeHtml(e || 'Load failed') + '</p>'); });
  }

  function renderSkillView(name) {
    const token = getToken();
    if (!token) { window.location.hash = '#/pair'; return; }
    api('GET', '/api/skills/' + encodeURIComponent(name)).then(d => {
      render(app, '<h1>Skill: ' + escapeHtml(name) + '</h1><textarea id="skill-content" rows="12" style="width:100%">' + escapeHtml(d.content || '') + '</textarea><button id="skill-save">Save</button> <a href="#/skills">Back</a>');
      document.getElementById('skill-save').onclick = () => {
        api('PUT', '/api/skills/' + encodeURIComponent(name), { content: document.getElementById('skill-content').value }).then(() => alert('Saved'));
      };
    }).catch(e => { render(app, '<h1>Skill</h1><p class="status-err">' + escapeHtml(e || 'Not found') + '</p>'); });
  }

  function renderMemory() {
    const token = getToken();
    if (!token) { window.location.hash = '#/pair'; return; }
    render(app, '<h1>Memory</h1><input id="mem-q" placeholder="Search..." style="width:100%;margin-bottom:0.5rem"><div id="mem-results" class="pre-wrap"></div>');
    const q = document.getElementById('mem-q');
    const res = document.getElementById('mem-results');
    function search() {
      const query = q.value.trim();
      api('GET', '/api/memory?q=' + encodeURIComponent(query) + '&limit=20').then(d => {
        res.textContent = (d.results || 'No results');
      }).catch(e => { res.textContent = 'Error: ' + e; });
    }
    q.onkeydown = e => { if (e.key === 'Enter') search(); };
    q.oninput = () => { if (q.value.length >= 2) search(); };
  }

  function renderSessions() {
    const token = getToken();
    if (!token) { window.location.hash = '#/pair'; return; }
    api('GET', '/api/sessions').then(arr => {
      let html = '<h1>Sessions</h1><ul class="session-list" id="session-list"></ul>';
      render(app, html);
      const ul = document.getElementById('session-list');
      (arr || []).forEach(id => {
        const li = document.createElement('li');
        li.innerHTML = '<span>' + escapeHtml(id) + '</span><button class="danger" data-delete="' + escapeHtml(id) + '">Delete</button>';
        li.querySelector('[data-delete]').onclick = () => {
          api('DELETE', '/api/sessions/' + encodeURIComponent(id)).then(() => { window.location.hash = '#/sessions'; });
        };
        ul.appendChild(li);
      });
    }).catch(e => { render(app, '<h1>Sessions</h1><p class="status-err">' + escapeHtml(e || 'Load failed') + '</p>'); });
  }

  function renderCron() {
    const token = getToken();
    if (!token) { window.location.hash = '#/pair'; return; }
    api('GET', '/api/cron').then(() => {}).catch(() => {});
    render(app, '<h1>Cron</h1><p>GET /api/cron returns 501 Not Implemented. Placeholder for create/delete/toggle.</p>');
  }

  function renderLogs() {
    const token = getToken();
    if (!token) { window.location.hash = '#/pair'; return; }
    render(app, '<h1>Logs</h1><div id="log-output" class="pre-wrap"></div>');
    const out = document.getElementById('log-output');
    const ws = wsConnect(function (msg) {
      if (msg.type === 'log') {
        const cls = 'log-' + escapeHtml(msg.level || 'info');
        const line = document.createElement('div');
        line.className = 'log-line ' + cls;
        line.textContent = msg.text || '';
        out.appendChild(line);
        out.scrollTop = out.scrollHeight;
      }
    });
    if (!ws) out.textContent = 'Not authenticated.';
  }

  function renderAsap() {
    const token = getToken();
    if (!token) { window.location.hash = '#/pair'; return; }
    render(app, '<h1>ASAP</h1><div id="asap-content"><p class="status-warn">Loading...</p></div>');
    const content = document.getElementById('asap-content');
    const manifestP = fetch(API_BASE + '/.well-known/asap/manifest.json')
      .then(r => r.ok ? r.json() : Promise.reject('HTTP ' + r.status))
      .catch(() => null);
    const logP = api('GET', '/api/asap/log').catch(() => null);
    Promise.all([manifestP, logP]).then(function (results) {
      const manifest = results[0];
      const logData = results[1];
      let html = '';
      /* Dashboard cards */
      html += '<div class="dashboard-cards">';
      if (manifest) {
        html += '<div class="card"><div class="card-label">Agent URN</div><div class="card-value">' + escapeHtml(manifest.urn || manifest.agent_urn || '-') + '</div></div>';
        html += '<div class="card"><div class="card-label">ASAP Version</div><div class="card-value status-ok">' + escapeHtml(manifest.asap_version || '-') + '</div></div>';
        html += '<div class="card"><div class="card-label">Agent Name</div><div class="card-value">' + escapeHtml(manifest.name || '-') + '</div></div>';
      } else {
        html += '<div class="card"><div class="card-label">Manifest</div><div class="card-value status-err">unavailable</div></div>';
      }
      html += '</div>';
      /* Manifest detail */
      if (manifest) {
        html += '<h2>Manifest</h2><pre class="pre-wrap">' + escapeHtml(JSON.stringify(manifest, null, 2)) + '</pre>';
      }
      /* Envelope log table */
      html += '<h2>Envelope Log</h2>';
      if (logData && Array.isArray(logData.entries) && logData.entries.length > 0) {
        html += '<table class="asap-log-table"><thead><tr><th>Time</th><th>Dir</th><th>Type</th><th>ID</th></tr></thead><tbody>';
        logData.entries.forEach(function (e) {
          const dir = e.direction === 'in' ? '&larr; in' : '&rarr; out';
          html += '<tr><td>' + escapeHtml(e.timestamp || '-') + '</td><td>' + dir + '</td><td>' + escapeHtml(e.payload_type || '-') + '</td><td class="mono">' + escapeHtml((e.id || '-').substring(0, 26)) + '</td></tr>';
        });
        html += '</tbody></table>';
      } else if (logData && Array.isArray(logData.entries)) {
        html += '<p class="status-note">No envelope events recorded yet.</p>';
      } else {
        html += '<p class="status-warn">Could not load envelope log (unauthorized or server error).</p>';
      }
      content.innerHTML = html;
    });
  }

  function renderPair() {
    render(app, '<h1>Pair</h1><p>Enter the 6-digit pairing code shown on the server.</p>' +
      '<form class="pair-form" id="pair-form"><input name="code" placeholder="Code" maxlength="6" autocomplete="off">' +
      '<button type="submit">Pair</button></form>');
    document.getElementById('pair-form').onsubmit = function (e) {
      e.preventDefault();
      const code = e.target.code.value.trim();
      api('POST', '/pair', { code }).then(d => {
        if (d.token) { setToken(d.token); window.location.hash = '#/'; }
      }).catch(err => alert('Invalid code: ' + err));
    };
  }

  const app = document.getElementById('app');
  function navigate() {
    const route = getRoute();
    const m = route.match(/^\/skills\/(.+)$/);
    if (m) { renderSkillView(decodeURIComponent(m[1])); return; }
    const fn = routes[route] || routes['/'];
    if (fn) fn();
    document.querySelectorAll('nav a').forEach(a => {
      a.classList.toggle('active', a.getAttribute('href') === '#' + route);
    });
  }
  window.addEventListener('hashchange', navigate);
  navigate();
})();
