const path = require('path');
function createAdapter(legacy = false) {
const folder = legacy ? 'solarxr-protocol-legacy' : 'solarxr-protocol';
const root = process.env.SLIMEVR_SERVER_SOURCE
  ? path.join(process.env.SLIMEVR_SERVER_SOURCE, folder)
  : path.join(__dirname, 'vendor', folder);
const protocol = require(path.join(root, 'protocol/typescript/dist/all_generated.js'));
const { Builder, ByteBuffer } = require(path.join(root, 'node_modules/flatbuffers'));
function pack(bundle) {
  const builder = new Builder(256);
  builder.finish(bundle.pack(builder));
  return builder.asUint8Array();
}
function unpack(bytes) {
  return protocol.MessageBundle.getRootAsMessageBundle(new ByteBuffer(bytes)).unpack();
}
function pollRequest() {
  const tracker = new protocol.TrackerDataMaskT();
  tracker.info = tracker.status = true;
  const device = new protocol.DeviceDataMaskT();
  device.deviceData = true;
  device.trackerData = tracker;
  const config = new protocol.DataFeedConfigT();
  config.dataMask = device;
  config.serverGuardsMask = true;
  const header = new protocol.DataFeedMessageHeaderT();
  header.messageType = protocol.DataFeedMessage.PollDataFeed;
  header.message = new protocol.PollDataFeedT(config);
  const bundle = new protocol.MessageBundleT();
  bundle.dataFeedMsgs = [header];
  return pack(bundle);
}
function summary(update) {
  const p = protocol.BodyPart;
  const percentages = new Map();
  for (const device of update.devices || []) {
    const battery = device.hardwareStatus?.batteryPctEstimate;
    if (!Number.isFinite(battery) || battery < 0 || battery > 100) continue;
    for (const tracker of device.trackers || []) {
      const role = tracker.info?.bodyPart;
      if (!role || tracker.info?.isComputed || ![protocol.TrackerStatus.OK, protocol.TrackerStatus.BUSY,
        protocol.TrackerStatus.OCCLUDED].includes(tracker.status)) continue;
      percentages.set(role, Math.min(percentages.get(role) ?? 100, Math.round(battery)));
    }
  }
  const slots = [[p.LEFT_UPPER_ARM], [p.RIGHT_UPPER_ARM],
    legacy ? [p.CHEST, p.UPPER_CHEST] : [p.UPPER_CHEST, p.LOWER_CHEST], legacy ? [p.HIP, p.WAIST] : [p.HIP, p.LOWER_WAIST, p.UPPER_WAIST],
    [p.LEFT_UPPER_LEG], [p.RIGHT_UPPER_LEG],
    [p.LEFT_FOOT, p.LEFT_LOWER_LEG], [p.RIGHT_FOOT, p.RIGHT_LOWER_LEG]];
  return [...slots.map(roles => {
    const role = roles.find(role => percentages.has(role));
    return role === undefined ? '--' : `${percentages.get(role)}%`;
  }), (legacy ? update.serverGuards?.canDoMounting === true : update.serverGuards?.mountingReset === protocol.ResetAvailability.AVAILABLE)
    ? 'MOUNT_OK' : 'MOUNT_WAIT'].join('\t');
}
function resetRequest(kind) {
  if (!['full', 'mounting'].includes(kind)) throw new Error('Expected full or mounting.');
  const request = new protocol.ResetRequestT();
  request.resetType = kind === 'full' ? (legacy ? protocol.ResetType.Full : protocol.ResetType.FULL) : (legacy ? protocol.ResetType.Mounting : protocol.ResetType.MOUNTING);
  const header = new protocol.RpcMessageHeaderT();
  header.messageType = protocol.RpcMessage.ResetRequest;
  header.message = request;
  const bundle = new protocol.MessageBundleT();
  bundle.rpcMsgs = [header];
  return { bytes: pack(bundle), resetType: request.resetType };
}
function resetProgress(header, resetType) {
  if (header.messageType !== (legacy ? protocol.RpcMessage.ResetResponse : protocol.RpcMessage.ResetStatusResponse)
      || header.message.resetType !== resetType) return null;
  const response = header.message;
  if (legacy) {
    const text = protocol.ResetStatus[response.status] || 'UNKNOWN';
    return { terminal: response.status === protocol.ResetStatus.FINISHED, failed: false, text };
  }
  const done = response.lifecycle === protocol.ResetLifecycle.DONE;
  const failed = [protocol.ResetLifecycle.CANCELED, protocol.ResetLifecycle.FAILED].includes(response.lifecycle);
  return { terminal: done || failed, failed,
    text: protocol.ResetLifecycle[response.lifecycle] || 'UNKNOWN' };
}
return { protocol, pack, unpack, pollRequest, summary, resetRequest, resetProgress, legacy };
}
module.exports = { ...createAdapter(), createAdapter };
