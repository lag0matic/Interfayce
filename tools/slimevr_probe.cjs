// Read-only protocol detection and battery feed; no calibration or writes.
const { connectSlime } = require('./slimevr_connection.cjs');
connectSlime().then(({ ws, adapter, update }) => {
  console.log(process.argv.includes('--summary') ? adapter.summary(update)
    : JSON.stringify({ protocol: adapter.legacy ? 'legacy' : 'v22', devices: update.devices || [] }, null, 2));
  ws.close();
}).catch(error => { console.error(error.message); process.exitCode = 1; });
