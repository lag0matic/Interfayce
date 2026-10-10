const { test } = require('node:test');
const assert = require('node:assert/strict');
const path = require('path');
process.env.SLIMEVR_SERVER_SOURCE ||= path.resolve(__dirname, '../packaging/.cache');
const adapter = require('../tools/slimevr_protocol.cjs');
const { protocol: p, summary, unpack, pollRequest, resetRequest, resetProgress } = adapter;
const device = (role, battery, status = p.TrackerStatus.OK) => ({
  hardwareStatus: { batteryPctEstimate: battery }, trackers: [{ info: { bodyPart: role }, status }],
});
test('v22 anatomical roles occupy the correct wrist slots', () => {
  const roles = ['LEFT_UPPER_ARM', 'RIGHT_UPPER_ARM', 'UPPER_CHEST', 'HIP',
    'LEFT_UPPER_LEG', 'RIGHT_UPPER_LEG', 'LEFT_FOOT', 'RIGHT_FOOT'];
  const devices = roles.map((role, i) => device(p.BodyPart[role], 10 + i));
  assert.equal(summary({ devices, serverGuards: { mountingReset: p.ResetAvailability.AVAILABLE } }),
    '10%\t11%\t12%\t13%\t14%\t15%\t16%\t17%\tMOUNT_OK');
});
test('fallback roles, zero battery, unassigned and disconnected trackers', () => {
  const devices = [device(p.BodyPart.LOWER_CHEST, 0), device(p.BodyPart.UPPER_WAIST, 45),
    device(p.BodyPart.LEFT_LOWER_LEG, 60), device(p.BodyPart.NONE, 99),
    device(p.BodyPart.RIGHT_UPPER_ARM, 90, p.TrackerStatus.DISCONNECTED),
    device(p.BodyPart.LEFT_UPPER_ARM, null), device(p.BodyPart.RIGHT_FOOT, 255)];
  assert.equal(summary({ devices }), '--\t--\t0%\t45%\t--\t--\t60%\t--\tMOUNT_WAIT');
});
test('an empty valid feed stays distinguishable from connection failure', () => {
  assert.equal(summary({ devices: [] }).split('\t').length, 9);
  assert.ok(summary({}).endsWith('MOUNT_WAIT'));
});
test('poll requests only needed fields and never sends an RPC', () => {
  const bundle = unpack(pollRequest());
  assert.equal(bundle.rpcMsgs.length, 0);
  assert.equal(bundle.dataFeedMsgs[0].messageType, p.DataFeedMessage.PollDataFeed);
  const config = bundle.dataFeedMsgs[0].message.config;
  assert.equal(config.dataMask.trackerData.info, true);
  assert.equal(config.dataMask.trackerData.rotation, false);
  assert.equal(config.serverGuardsMask, true);
});
test('reset requests encode v22 FULL and MOUNTING, preserving server defaults', () => {
  for (const kind of ['full', 'mounting']) {
    const request = resetRequest(kind);
    const header = unpack(request.bytes).rpcMsgs[0];
    assert.equal(header.messageType, p.RpcMessage.ResetRequest);
    assert.equal(header.message.resetType, p.ResetType[kind.toUpperCase()]);
    assert.equal(header.message.delay, null);
    assert.deepEqual(header.message.bodyParts, []);
  }
  assert.throws(() => resetRequest('unknown'));
});
test('reset lifecycle handles done, canceled and failed without accepting another reset type', () => {
  const header = { messageType: p.RpcMessage.ResetStatusResponse,
    message: { resetType: p.ResetType.FULL, lifecycle: p.ResetLifecycle.RUNNING } };
  assert.equal(resetProgress(header, p.ResetType.MOUNTING), null);
  assert.equal(resetProgress(header, p.ResetType.FULL).terminal, false);
  header.message.lifecycle = p.ResetLifecycle.DONE;
  assert.deepEqual(resetProgress(header, p.ResetType.FULL), { terminal: true, failed: false, text: 'DONE' });
  for (const lifecycle of [p.ResetLifecycle.CANCELED, p.ResetLifecycle.FAILED]) {
    header.message.lifecycle = lifecycle;
    assert.equal(resetProgress(header, p.ResetType.FULL).failed, true);
  }
});

const legacy = adapter.createAdapter(true);
const { connectSlime, detectionRequest, detect } = require('../tools/slimevr_connection.cjs');
function feed(a, devices = []) {
  const p = a.protocol;
  const header = new p.DataFeedMessageHeaderT();
  header.messageType = p.DataFeedMessage.DataFeedUpdate;
  header.message = new p.DataFeedUpdateT();
  header.message.devices = devices;
  header.message.serverGuards = new p.ServerGuardsT();
  const bundle = new p.MessageBundleT();
  bundle.dataFeedMsgs = [header];
  return a.pack(bundle);
}
for (const a of [adapter, legacy]) {
  const label = a.legacy ? 'legacy' : 'v22';
  test(`${label}: common detection request is read-only and requests guards`, () => {
    const bundle = a.unpack(detectionRequest(legacy));
    assert.equal(bundle.rpcMsgs.length, 0);
    const header = bundle.dataFeedMsgs[0];
    assert.equal(header.messageType, a.protocol.DataFeedMessage.PollDataFeed);
    assert.equal(header.message.config.serverGuardsMask, true);
    assert.equal(header.message.config.dataMask, null);
    assert.equal(detect(feed(a), adapter, legacy), a);
  });
  test(`${label}: connection detects protocol before requesting tracker data`, async () => {
    let sends = 0;
    class FakeSocket extends EventTarget {
      constructor() { super(); queueMicrotask(() => this.dispatchEvent(new Event('open'))); }
      send(bytes) {
        const request = a.unpack(bytes);
        assert.equal(request.rpcMsgs.length, 0);
        const config = request.dataFeedMsgs[0].message.config;
        assert.equal(config.serverGuardsMask, true);
        if (++sends === 2) assert.equal(config.dataMask.trackerData.info, true);
        queueMicrotask(() => this.dispatchEvent(new MessageEvent('message', { data: feed(a) })));
      }
      close() {}
    }
    const result = await connectSlime({ WebSocketClass: FakeSocket, timeoutMs: 200 });
    assert.equal(result.adapter.legacy, a.legacy);
    assert.equal(sends, 2);
  });
}
test('legacy role IDs and reset encodings remain compatible', () => {
  const p = legacy.protocol;
  assert.equal(legacy.summary({ devices: [device(p.BodyPart.LEFT_UPPER_ARM, 80)],
    serverGuards: { canDoMounting: true } }), '80%\t--\t--\t--\t--\t--\t--\t--\tMOUNT_OK');
  for (const kind of ['full', 'mounting']) {
    const request = legacy.resetRequest(kind);
    const header = legacy.unpack(request.bytes).rpcMsgs[0];
    assert.equal(header.message.resetType, kind === 'full' ? p.ResetType.Full : p.ResetType.Mounting);
  }
  assert.equal(legacy.resetProgress({ messageType: p.RpcMessage.ResetResponse,
    message: { resetType: p.ResetType.Full, status: p.ResetStatus.FINISHED } }, p.ResetType.Full).terminal, true);
});
test('unknown messages are not mistaken for a protocol confirmation', () => {
  assert.equal(detect(adapter.pack(new p.MessageBundleT()), adapter, legacy), null);
});
test('connection timeout rejects without issuing reset commands', async () => {
  class SilentSocket extends EventTarget { close() {} }
  await assert.rejects(connectSlime({ WebSocketClass: SilentSocket, timeoutMs: 5 }), /timed out/);
});
