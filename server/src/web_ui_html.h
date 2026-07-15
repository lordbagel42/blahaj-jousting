// server/src/web_ui_html.h
#pragma once

static const char UI_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Blahaj Jousting</title>
<style>
  body{font-family:monospace;background:#111;color:#eee;max-width:600px;margin:0 auto;padding:1rem}
  h1{color:#f90;margin:0 0 1rem}
  .card{background:#1e1e1e;border-radius:8px;padding:1rem;margin:.5rem 0}
  .dot{display:inline-block;width:10px;height:10px;border-radius:50%;margin-right:.5rem}
  .green{background:#0f0}.red{background:#f00}.gray{background:#555}
  select{background:#333;color:#eee;border:1px solid #555;padding:.3rem;border-radius:4px}
  button{background:#f90;color:#111;border:none;padding:.5rem 1.2rem;border-radius:4px;
         font-weight:bold;cursor:pointer;margin:.3rem}
  button:disabled{background:#555;color:#888;cursor:not-allowed}
  button.danger{background:#c00;color:#fff}
  #status{margin-top:.5rem;font-size:.85rem;color:#888}
  table{width:100%;border-collapse:collapse}
  td{padding:.4rem .3rem;vertical-align:middle}
</style>
</head>
<body>
<h1>&#x1F988; Blahaj Jousting</h1>

<div class="card">
  <b>Devices</b>
  <table id="deviceTable"><tr><td>Scanning...</td></tr></table>
</div>

<div class="card">
  <b>Pairings</b>
  <div id="pairings">No cars discovered yet.</div>
  <button id="confirmBtn" disabled onclick="confirmPairings()">Confirm Pairings</button>
</div>

<div class="card">
  <b>Match Control</b>
  <button id="startBtn" disabled onclick="startMatch()">Start Match</button>
  <button id="endBtn"   class="danger" disabled onclick="endRound()">End Round</button>
  <div id="matchState">State: —</div>
  <div id="scores"></div>
</div>

<div id="status"></div>

<script>
let lastStatus = null;

async function poll() {
  try {
    const r = await fetch('/api/status');
    const d = await r.json();
    render(d);
    lastStatus = d;
    document.getElementById('status').textContent = 'Updated ' + new Date().toLocaleTimeString();
  } catch(e) {
    document.getElementById('status').textContent = 'Connection error';
  }
}

function dot(connected) {
  return `<span class="dot ${connected ? 'green' : 'red'}"></span>`;
}

function macStr(mac) {
  return mac.map(b => b.toString(16).padStart(2,'0')).join(':');
}

function render(d) {
  // Device table
  const rows = [...d.cars, ...d.clients].map(dev => `
    <tr>
      <td>${dot(dev.connected)}${dev.type} #${dev.device_id}</td>
      <td style="color:#888;font-size:.8rem">${macStr(dev.mac)}</td>
      <td>${dev.paired ? '&#x2714; paired' : '<span style="color:#f90">unpaired</span>'}</td>
    </tr>`).join('');
  document.getElementById('deviceTable').innerHTML = rows || '<tr><td>No devices yet</td></tr>';

  // Pairing selects — one per car
  if (d.cars.length > 0) {
    const selects = d.cars.map((car, i) => {
      const opts = d.clients.map((c, ci) =>
        `<option value="${ci}" ${car.partner_slot===ci?'selected':''}>Client #${c.device_id} (${macStr(c.mac).slice(0,8)}…)</option>`
      ).join('');
      return `<div style="margin:.3rem 0">Car #${car.device_id}: <select id="pair_${i}">${opts}</select></div>`;
    }).join('');
    document.getElementById('pairings').innerHTML = selects;
    document.getElementById('confirmBtn').disabled = d.clients.length === 0;
  }

  // Match state
  const stateNames = ['LOBBY','COUNTDOWN','RACING','ROUND_END'];
  document.getElementById('matchState').textContent = 'State: ' + (stateNames[d.game_state] || '?');
  document.getElementById('startBtn').disabled = d.game_state !== 0 || !d.all_paired;
  document.getElementById('endBtn').disabled   = d.game_state !== 2;

  // Scores
  if (d.round_wins) {
    const sc = d.cars.map((car, i) =>
      `Car #${car.device_id}: ${d.round_wins[i] || 0} wins`
    ).join(' &nbsp;|&nbsp; ');
    document.getElementById('scores').innerHTML = sc;
  }
}

async function confirmPairings() {
  const pairs = [];
  const cars = lastStatus?.cars || [];
  cars.forEach((_, i) => {
    const sel = document.getElementById('pair_' + i);
    if (sel) pairs.push({car: i, client: parseInt(sel.value)});
  });
  await fetch('/api/pair', {method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify(pairs)});
  poll();
}

async function startMatch() {
  await fetch('/api/start', {method:'POST'});
  poll();
}

async function endRound() {
  await fetch('/api/end', {method:'POST'});
  poll();
}

poll();
setInterval(poll, 2000);
</script>
</body>
</html>
)rawhtml";
