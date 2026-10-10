// Reset only after read-only detection, on the same confirmed connection.
const { connectSlime } = require('./slimevr_connection.cjs');
const kind = process.argv[2];
if (!['full', 'mounting'].includes(kind)) throw new Error('Expected full or mounting.');
connectSlime().then(({ ws, adapter }) => {
  const request = adapter.resetRequest(kind);
  let finished = false;
  function finish(error) {
    if (finished) return;
    finished = true;
    clearTimeout(deadline);
    if (error) { console.error(error); process.exitCode = 1; }
    ws.close();
  }
  const deadline = setTimeout(() => finish('SlimeVR reset response timed out.'), 120000);
  ws.addEventListener('message', async ({ data }) => {
    try {
      const bytes = new Uint8Array(data.arrayBuffer ? await data.arrayBuffer() : data);
      for (const header of adapter.unpack(bytes).rpcMsgs || []) {
        const progress = adapter.resetProgress(header, request.resetType);
        if (!progress || finished) continue;
        console.log(progress.text);
        if (progress.terminal) finish(progress.failed ? 'SlimeVR reset did not complete.' : undefined);
      }
    } catch { finish('Could not decode SlimeVR reset response.'); }
  });
  ws.addEventListener('error', () => finish('SlimeVR reset connection failed.'));
  ws.addEventListener('close', () => { if (!finished) finish('SlimeVR disconnected during reset.'); });
  ws.send(request.bytes);
}).catch(error => { console.error(error.message); process.exitCode = 1; });
