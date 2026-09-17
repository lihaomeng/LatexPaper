import { test } from "node:test";
import assert from "node:assert/strict";
import { CefNativeApi, createNativeConnection, type CefBridge, type CefQueryOptions } from "../src/native-api/index.ts";
import type { PingRequest } from "../.generated/rpc/contract.ts";

const request: PingRequest = { version: 1, id: "real-ping", method: "system.ping", params: {}, clientSequence: 4 };
test("CEF ping crosses injected bridge and validates echo", async () => {
  let sent = "";
  const bridge: CefBridge = {
    query: options => { sent = options.request; options.onSuccess(sent); return 1; },
    cancel: () => { throw new Error("unexpected cancellation"); },
  };
  await new CefNativeApi(bridge).ping(request);
  assert.deepEqual(JSON.parse(sent), request);
});
for (const response of ["invalid-json", "null", JSON.stringify({ ...request, id: "other" }),
  JSON.stringify({ ...request, clientSequence: 3 }), JSON.stringify({ ...request, extra: true })]) {
  test(`reject invalid acknowledgement: ${response}`, async () => {
    const bridge: CefBridge = { query: options => { options.onSuccess(response); return 1; }, cancel: () => {} };
    await assert.rejects(new CefNativeApi(bridge).ping(request), /INVALID_RESPONSE/);
  });
}
test("native failure is surfaced", async () => {
  const bridge: CefBridge = { query: options => { options.onFailure(2, "INVALID_ARGUMENT"); return 1; }, cancel: () => {} };
  await assert.rejects(new CefNativeApi(bridge).ping(request), /INVALID_ARGUMENT/);
});
test("timeout cancels pending native query and ignores late reply", async () => {
  let callbacks: CefQueryOptions | undefined;
  let cancelled = 0;
  const bridge: CefBridge = { query: options => { callbacks = options; return 7; }, cancel: id => { cancelled = id; } };
  await assert.rejects(new CefNativeApi(bridge, 10).ping(request), /RPC_TIMEOUT/);
  assert.equal(cancelled, 7);
  callbacks?.onSuccess(JSON.stringify(request));
});
test("synchronous transport failure rejects", async () => {
  const bridge: CefBridge = { query: () => { throw new Error("gone"); }, cancel: () => {} };
  await assert.rejects(new CefNativeApi(bridge).ping(request), /TRANSPORT_UNAVAILABLE/);
});

for (const allowFake of [false, true]) {
  test(`missing native bridge: fake allowed=${allowFake}`, async context => {
    const previous = Object.getOwnPropertyDescriptor(globalThis, "window");
    Object.defineProperty(globalThis, "window", { configurable: true, value: {} });
    context.after(() => {
      if (previous) Object.defineProperty(globalThis, "window", previous);
      else Reflect.deleteProperty(globalThis, "window");
    });
    const connection = createNativeConnection(allowFake);
    if (allowFake) {
      assert.match(connection.mode, /Fake/);
      await connection.api.ping(request);
    } else {
      assert.doesNotMatch(connection.mode, /Fake/);
      await assert.rejects(connection.api.ping(request), /TRANSPORT_UNAVAILABLE/);
    }
  });
}
test("native connection takes precedence over development fake", async context => {
  const previous = Object.getOwnPropertyDescriptor(globalThis, "window");
  let calls = 0;
  Object.defineProperty(globalThis, "window", { configurable: true, value: {
    cefQuery: (options: CefQueryOptions) => { calls++; options.onSuccess(options.request); return 1; },
    cefQueryCancel: () => {},
  } });
  context.after(() => {
    if (previous) Object.defineProperty(globalThis, "window", previous);
    else Reflect.deleteProperty(globalThis, "window");
  });
  const connection = createNativeConnection(true);
  assert.doesNotMatch(connection.mode, /Fake/);
  await connection.api.ping(request);
  assert.equal(calls, 1);
});
test("invalid timeout is rejected at the API boundary", () => {
  const bridge: CefBridge = { query: () => 1, cancel: () => {} };
  for (const timeout of [0, -1, NaN, Infinity, 60001]) {
    assert.throws(() => new CefNativeApi(bridge, timeout), /INVALID_TIMEOUT/);
  }
});
