// Write helper: invoked only by the user's held reset confirmation on the Rig deck.
const { unpack, resetRequest, resetProgress } = require('./slimevr_protocol.cjs');
const request = resetRequest(process.argv[2]);
const ws = new WebSocket('ws://127.0.0.1:21110');
let finished = false;
function finish(error) {
  if (finished) return;
  finished = true;
  clearTimeout(deadline);
  if (error) { console.error(error); process.exitCode = 1; }
  ws.close();
}
// Step mounting can take longer than the old fixed countdown.
const deadline = setTimeout(() => finish('SlimeVR reset response timed out.'), 120000);
ws.addEventListener('open', () => ws.send(request.bytes));
ws.addEventListener('message', async ({ data }) => {
  try {
    const bytes = new Uint8Array(data.arrayBuffer ? await data.arrayBuffer() : data);
    for (const header of unpack(bytes).rpcMsgs || []) {
      const progress = resetProgress(header, request.resetType);
      if (!progress || finished) continue;
      console.log(progress.text);
      if (progress.terminal) finish(progress.failed ? 'SlimeVR reset did not complete.' : undefined);
    }
  } catch { finish('Could not decode SlimeVR reset response.'); }
});
ws.addEventListener('error', () => finish('SlimeVR reset connection failed.'));
ws.addEventListener('close', () => { if (!finished) finish('SlimeVR disconnected during reset.'); });
