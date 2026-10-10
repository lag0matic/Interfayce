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
const fs = require('fs');
const vm = require('vm');
function probeHarness() {
  let socket, timeout;
  const output = [];
  const fakeProcess = { argv: ['node', 'probe', '--summary'], exitCode: 0 };
  class Socket {
    constructor() { socket = this; this.handlers = {}; }
    addEventListener(name, fn) { this.handlers[name] = fn; }
    send() {}
    close() { this.handlers.close?.(); }
  }
  vm.runInNewContext(fs.readFileSync(path.resolve(__dirname, '../tools/slimevr_probe.cjs'), 'utf8'), {
    require: () => adapter, WebSocket: Socket, Uint8Array,
    console: { log: value => output.push(value), error: () => {} }, process: fakeProcess,
    setTimeout: fn => { timeout = fn; return 1; }, clearTimeout: () => {},
  });
  return { socket, output, fakeProcess, timeout: () => timeout() };
}
for (const failure of ['error', 'close', 'timeout']) {
  test(`probe exits unsuccessfully on ${failure}`, () => {
    const h = probeHarness();
    if (failure === 'timeout') h.timeout(); else h.socket.handlers[failure]();
    assert.equal(h.fakeProcess.exitCode, 1);
    assert.equal(h.output.length, 0);
  });
}
test('probe accepts a decoded empty data feed without requiring trackers', async () => {
  const h = probeHarness();
  const bundle = new p.MessageBundleT();
  const header = new p.DataFeedMessageHeaderT();
  header.messageType = p.DataFeedMessage.DataFeedUpdate;
  header.message = new p.DataFeedUpdateT();
  bundle.dataFeedMsgs = [header];
  await h.socket.handlers.message({ data: adapter.pack(bundle) });
  assert.equal(h.fakeProcess.exitCode, 0);
  assert.equal(h.output[0], '--\t--\t--\t--\t--\t--\t--\t--\tMOUNT_WAIT');
});
