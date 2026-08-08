// Read or deliberately change the symmetric SlimeVR leg proportions.
// Values on the SolarXR wire are metres. A profile changes only UPPER_LEG and
// LOWER_LEG, preserving their current ratio, then reads them back to verify.
const path = require('path');
const fs = require('fs');

const action = process.argv[2] || 'status';
const targets = { config: 0.901, play: 1.000 };
if (action !== 'status' && !Object.hasOwn(targets, action)) {
  throw new Error('Expected status, config, or play.');
}

const protocolRoot = process.env.SLIMEVR_SERVER_SOURCE
  ? path.join(process.env.SLIMEVR_SERVER_SOURCE, 'solarxr-protocol')
  : path.join(__dirname, 'vendor', 'solarxr-protocol');
if (!fs.existsSync(protocolRoot)) {
  throw new Error('The pinned SolarXR protocol runtime is not installed.');
}

const protocol = require(path.join(
  protocolRoot, 'protocol', 'typescript', 'dist', 'all_generated.js'));
const { Builder, ByteBuffer } = require(path.join(
  protocolRoot, 'node_modules', 'flatbuffers'));

const upperBone = protocol.SkeletonBone.UPPER_LEG;
const lowerBone = protocol.SkeletonBone.LOWER_LEG;
const tolerance = 0.00075; // 0.075 cm, tighter than the GUI's 0.1 cm display.
const ws = new WebSocket('ws://127.0.0.1:21110');
let original = null;
let applying = false;
let rollingBack = false;
let finished = false;

function sendHeaders(headers) {
  const bundle = new protocol.MessageBundleT();
  bundle.rpcMsgs = headers;
  const builder = new Builder(256);
  builder.finish(bundle.pack(builder));
  ws.send(builder.asUint8Array());
}

function header(type, message) {
  const result = new protocol.RpcMessageHeaderT();
  result.messageType = type;
  result.message = message;
  return result;
}

function requestConfig() {
  sendHeaders([header(
    protocol.RpcMessage.SkeletonConfigRequest,
    new protocol.SkeletonConfigRequestT())]);
}

function changeLegs(upper, lower) {
  sendHeaders([
    header(protocol.RpcMessage.ChangeSkeletonConfigRequest,
      new protocol.ChangeSkeletonConfigRequestT(upperBone, upper)),
    header(protocol.RpcMessage.ChangeSkeletonConfigRequest,
      new protocol.ChangeSkeletonConfigRequestT(lowerBone, lower)),
  ]);
}

function legValues(response) {
  const upper = response.skeletonParts.find((part) => part.bone === upperBone)?.value;
  const lower = response.skeletonParts.find((part) => part.bone === lowerBone)?.value;
  if (!Number.isFinite(upper) || !Number.isFinite(lower) || upper <= 0 || lower <= 0) {
    throw new Error('SlimeVR did not return valid upper and lower leg lengths.');
  }
  return { upper, lower, total: upper + lower };
}

function classify(total) {
  if (Math.abs(total - targets.config) <= tolerance) return 'CONFIG';
  if (Math.abs(total - targets.play) <= tolerance) return 'PLAY';
  return 'CUSTOM';
}

function report(values) {
  console.log([
    classify(values.total), values.upper.toFixed(6), values.lower.toFixed(6),
    values.total.toFixed(6),
  ].join('\t'));
  finished = true;
  clearTimeout(deadline);
  ws.close();
}

function fail(message) {
  if (finished) return;
  finished = true;
  clearTimeout(deadline);
  console.error(message);
  ws.close();
  process.exitCode = 1;
}

const deadline = setTimeout(() => fail('SlimeVR leg-profile request timed out.'), 6000);

ws.addEventListener('open', requestConfig);
ws.addEventListener('message', async ({ data }) => {
  try {
    const raw = data.arrayBuffer ? await data.arrayBuffer() : data;
    const bundle = protocol.MessageBundle.getRootAsMessageBundle(
      new ByteBuffer(new Uint8Array(raw))).unpack();
    for (const rpc of bundle.rpcMsgs || []) {
      if (rpc.messageType !== protocol.RpcMessage.SkeletonConfigResponse) continue;
      const current = legValues(rpc.message);
      if (action === 'status') {
        report(current);
        return;
      }
      if (!applying) {
        original = current;
        const targetTotal = targets[action];
        const upperRatio = current.upper / current.total;
        applying = true;
        changeLegs(targetTotal * upperRatio, targetTotal * (1 - upperRatio));
        setTimeout(requestConfig, 80);
        return;
      }
      const expectedTotal = targets[action];
      if (!rollingBack && Math.abs(current.total - expectedTotal) <= tolerance) {
        report(current);
        return;
      }
      if (!rollingBack && original) {
        rollingBack = true;
        changeLegs(original.upper, original.lower);
        setTimeout(() => fail('SlimeVR did not verify the requested leg profile; original values restored.'), 100);
        return;
      }
    }
  } catch (error) {
    fail(error instanceof Error ? error.message : String(error));
  }
});
ws.addEventListener('error', () => fail('Could not connect to the SlimeVR SolarXR service.'));
