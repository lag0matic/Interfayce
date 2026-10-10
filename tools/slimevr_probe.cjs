// Read-only: one data-feed poll, never resets or configuration writes.
const { protocol, unpack, pollRequest, summary } = require('./slimevr_protocol.cjs');
const ws = new WebSocket('ws://127.0.0.1:21110');
let reported = false;
let finished = false;
function finish(error) {
  if (finished) return;
  finished = true;
  clearTimeout(deadline);
  if (error) { console.error(error); process.exitCode = 1; }
  ws.close();
}
const deadline = setTimeout(() => finish('No SlimeVR data-feed update received within 5 seconds.'), 5000);
ws.addEventListener('open', () => ws.send(pollRequest()));
ws.addEventListener('message', async ({ data }) => {
  try {
    const bytes = new Uint8Array(data.arrayBuffer ? await data.arrayBuffer() : data);
    for (const header of unpack(bytes).dataFeedMsgs || []) {
      if (finished || header.messageType !== protocol.DataFeedMessage.DataFeedUpdate) continue;
      reported = true;
      console.log(process.argv.includes('--summary') ? summary(header.message)
        : JSON.stringify(header.message.devices || [], null, 2));
      finish();
    }
  } catch { finish('Could not decode SlimeVR data feed.'); }
});
ws.addEventListener('error', () => finish('SlimeVR connection failed.'));
ws.addEventListener('close', () => { if (!reported) finish('SlimeVR closed before sending tracker data.'); });
