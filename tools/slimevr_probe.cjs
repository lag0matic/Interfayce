// Read-only diagnostic for the locally running SlimeVR server.
// It deliberately requests only the GUI's public data feed: no resets,
// calibration, settings writes, or tracker commands are sent.

const path = require('path');

const serverRoot = process.env.SLIMEVR_SERVER_SOURCE ||
  'C:\\Users\\lag0m\\AppData\\Local\\Temp\\slimevr-server-reference';
const protocolRoot = path.join(serverRoot, 'solarxr-protocol');
const protocol = require(path.join(protocolRoot, 'protocol', 'typescript', 'dist', 'all_generated.js'));
const { Builder, ByteBuffer } = require(path.join(protocolRoot, 'node_modules', 'flatbuffers'));
const WebSocket = globalThis.WebSocket;
const summaryOnly = process.argv.includes('--summary');

const trackerMask = new protocol.TrackerDataMaskT();
trackerMask.position = true;
trackerMask.rotation = true;
trackerMask.info = true;
trackerMask.status = true;
trackerMask.temp = true;

const deviceMask = new protocol.DeviceDataMaskT();
deviceMask.deviceData = true;
deviceMask.trackerData = trackerMask;

const feedConfig = new protocol.DataFeedConfigT();
feedConfig.dataMask = deviceMask;
feedConfig.minimumTimeSinceLast = 1000;
feedConfig.syntheticTrackersMask = trackerMask;
feedConfig.stayAlignedPoseMask = true;
feedConfig.serverGuardsMask = true;

const ws = new WebSocket('ws://127.0.0.1:21110');
let reported = false;
let deadline;

function trackerPercentByBodyPart(devices) {
  const percent = new Map();
  for (const device of devices) {
    const battery = device.hardwareStatus?.batteryPctEstimate;
    if (battery == null) continue;
    for (const tracker of device.trackers || []) {
      if (tracker.info?.isComputed || tracker.info?.bodyPart == null) continue;
      percent.set(tracker.info.bodyPart, Math.round(battery));
    }
  }
  const format = (bodyPart, fallback) => {
    const value = percent.get(bodyPart) ?? (fallback == null ? null : percent.get(fallback));
    return value == null ? '--' : `${value}%`;
  };
  return [
    format(16), format(17), format(3), format(5),
    format(6), format(7), format(10, 8), format(11, 9),
  ];
}

function sendFeedRequest() {
  const request = new protocol.StartDataFeedT();
  request.dataFeeds = [feedConfig];

  const header = new protocol.DataFeedMessageHeaderT();
  header.messageType = protocol.DataFeedMessage.StartDataFeed;
  header.message = request;

  const bundle = new protocol.MessageBundleT();
  bundle.dataFeedMsgs = [header];

  const builder = new Builder(256);
  builder.finish(bundle.pack(builder));
  ws.send(builder.asUint8Array());

  // Ask for one immediate update as well. This is the protocol's read-only
  // bootstrap path and avoids relying on the server's periodic tick.
  const pollHeader = new protocol.DataFeedMessageHeaderT();
  pollHeader.messageType = protocol.DataFeedMessage.PollDataFeed;
  pollHeader.message = new protocol.PollDataFeedT(feedConfig);
  const pollBundle = new protocol.MessageBundleT();
  pollBundle.dataFeedMsgs = [pollHeader];
  const pollBuilder = new Builder(256);
  pollBuilder.finish(pollBundle.pack(pollBuilder));
  ws.send(pollBuilder.asUint8Array());
}

ws.addEventListener('open', sendFeedRequest);
ws.addEventListener('message', async ({ data }) => {
  const buffer = data.arrayBuffer ? await data.arrayBuffer() : data;
  const bytes = new Uint8Array(buffer);
  const bundle = protocol.MessageBundle.getRootAsMessageBundle(new ByteBuffer(bytes)).unpack();
  for (const header of bundle.dataFeedMsgs || []) {
    if (header.messageType !== protocol.DataFeedMessage.DataFeedUpdate) continue;
    const update = header.message;
    if (reported || !update.devices?.length) continue;
    reported = true;
    if (summaryOnly) {
      console.log([...trackerPercentByBodyPart(update.devices),
        update.serverGuards?.canDoMounting ? 'MOUNT_OK' : 'MOUNT_WAIT'].join('\t'));
    } else {
      console.log(JSON.stringify(update.devices, null, 2));
    }
    clearTimeout(deadline);
    ws.close();
  }
});

ws.addEventListener('close', ({ code, reason }) => {
  if (!reported && code) console.error(`SlimeVR connection closed (${code}${reason ? `: ${reason}` : ''}).`);
});

ws.addEventListener('error', (event) => {
  console.error(event.message || 'WebSocket connection failed.');
  process.exitCode = 1;
});

deadline = setTimeout(() => {
  if (!reported) console.error('No SlimeVR data-feed update received within 5 seconds.');
  ws.close();
}, 5000);
