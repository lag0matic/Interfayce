// Detect with common read-only feed fields; never guess a reset RPC format.
const { createAdapter } = require('./slimevr_protocol.cjs');
function detectionRequest(legacy) {
  const p = legacy.protocol;
  const config = new p.DataFeedConfigT();
  // Slot 3: legacy bone mask / v22 server guards. Slot 5: legacy guards.
  config.boneMask = true;
  config.serverGuardsMask = true;
  const header = new p.DataFeedMessageHeaderT();
  header.messageType = p.DataFeedMessage.PollDataFeed;
  header.message = new p.PollDataFeedT(config);
  const bundle = new p.MessageBundleT();
  bundle.dataFeedMsgs = [header];
  return legacy.pack(bundle);
}
function detect(bytes, modern, legacy) {
  for (const adapter of [modern, legacy]) {
    try {
      const update = adapter.unpack(bytes).dataFeedMsgs?.find(h =>
        h.messageType === adapter.protocol.DataFeedMessage.DataFeedUpdate)?.message;
      if (update?.serverGuards != null) return adapter;
    } catch { /* Try the other decoder only for this read-only response. */ }
  }
  return null;
}
function connectSlime({ WebSocketClass = WebSocket, timeoutMs = 5000 } = {}) {
  const modern = createAdapter();
  const legacy = createAdapter(true);
  return new Promise((resolve, reject) => {
    const ws = new WebSocketClass('ws://127.0.0.1:21110');
    let selected, finished = false;
    const deadline = setTimeout(() => fail(new Error('SlimeVR data request timed out.')), timeoutMs);
    const cleanup = () => {
      clearTimeout(deadline);
      ws.removeEventListener('message', onMessage);
      ws.removeEventListener('error', onError);
      ws.removeEventListener('close', onClose);
    };
    function fail(error) {
      if (finished) return;
      finished = true;
      cleanup();
      ws.close();
      reject(error);
    }
    function onError() { fail(new Error('SlimeVR connection failed.')); }
    function onClose() { fail(new Error('SlimeVR disconnected before sending data.')); }
    async function onMessage({ data }) {
      try {
        const bytes = new Uint8Array(data.arrayBuffer ? await data.arrayBuffer() : data);
        if (finished) return;
        if (!selected) {
          selected = detect(bytes, modern, legacy);
          if (!selected) return;
          ws.send(selected.pollRequest());
          return;
        }
        const update = selected.unpack(bytes).dataFeedMsgs?.find(h =>
          h.messageType === selected.protocol.DataFeedMessage.DataFeedUpdate)?.message;
        if (!update || !update.serverGuards) return;
        finished = true;
        cleanup();
        resolve({ ws, adapter: selected, update });
      } catch (error) { fail(error); }
    }
    ws.addEventListener('open', () => ws.send(detectionRequest(legacy)));
    ws.addEventListener('message', onMessage);
    ws.addEventListener('error', onError);
    ws.addEventListener('close', onClose);
  });
}
module.exports = { connectSlime, detectionRequest, detect };
