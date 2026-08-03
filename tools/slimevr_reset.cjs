// Deliberate write helper. It is invoked only by the held confirmation in the
// Interfayce Rig deck; do not call it from polling or startup paths.
const path = require('path');
const fs = require('fs');
const kind = process.argv[2];
if (kind !== 'full' && kind !== 'mounting') throw new Error('Expected full or mounting.');
const protocolRoot = process.env.SLIMEVR_SERVER_SOURCE
  ? path.join(process.env.SLIMEVR_SERVER_SOURCE, 'solarxr-protocol')
  : path.join(__dirname, 'vendor', 'solarxr-protocol');
if (!fs.existsSync(protocolRoot)) {
  throw new Error('The pinned SolarXR protocol runtime is not installed.');
}
const protocol = require(path.join(protocolRoot, 'protocol', 'typescript', 'dist', 'all_generated.js'));
const { Builder, ByteBuffer } = require(path.join(protocolRoot, 'node_modules', 'flatbuffers'));
const ws = new WebSocket('ws://127.0.0.1:21110');
const resetType = kind === 'full' ? protocol.ResetType.Full : protocol.ResetType.Mounting;
const deadline = setTimeout(() => { console.error('SlimeVR reset timed out.'); ws.close(); }, 12000);
ws.addEventListener('open', () => {
  const request = new protocol.ResetRequestT();
  request.resetType = resetType;
  const header = new protocol.RpcMessageHeaderT();
  header.messageType = protocol.RpcMessage.ResetRequest;
  header.message = request;
  const bundle = new protocol.MessageBundleT();
  bundle.rpcMsgs = [header];
  const builder = new Builder(128);
  builder.finish(bundle.pack(builder));
  ws.send(builder.asUint8Array());
});
ws.addEventListener('message', async ({ data }) => {
  const bytes = new Uint8Array(data.arrayBuffer ? await data.arrayBuffer() : data);
  const bundle = protocol.MessageBundle.getRootAsMessageBundle(new ByteBuffer(bytes)).unpack();
  for (const header of bundle.rpcMsgs || []) {
    if (header.messageType !== protocol.RpcMessage.ResetResponse) continue;
    const response = header.message;
    if (response.resetType !== resetType) continue;
    console.log(`${protocol.ResetStatus[response.status]} ${response.progress}/${response.duration}`);
    if (response.status === protocol.ResetStatus.FINISHED) { clearTimeout(deadline); ws.close(); }
  }
});
