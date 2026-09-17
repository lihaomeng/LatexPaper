import { validatePing, type PingRequest, type NativeApi } from "../../.generated/rpc/contract";
export { createSystemConnection, SystemRpcClient, BridgeSystemTransport, CefSystemTransport, FakeSystemTransport } from './system';
export { WorkspaceRpcClient } from './workspace';
export { AuthoringRpcClient } from './authoring';
export { ExportRpcClient } from './export';
export { PreferencesSessionRpcClient } from './preferencesSession';
export type { Preferences, SessionSnapshot } from './preferencesSession';
export { RpcEventCursor } from './events';
export { NativeEventSubscription, createNativeEvents } from './subscription';
export { CancellationRpcClient } from './cancellation';
export { createStartupEvents } from './smoke';
export { payloadSmokeProbe } from './payloadSmoke';
export type { PingRequest, NativeApi } from "../../.generated/rpc/contract";
/** M0 fake is explicit: no native capability is advertised or simulated. */
export class FakeNativeApi implements NativeApi {
  async ping(request: PingRequest): Promise<void> {
    if (!validatePing(request)) throw new Error("INVALID_ARGUMENT");
  }
}

import { getNativeBridge, type NativeBridge } from './bridge';
export type { NativeBridge, NativeQueryOptions } from './bridge';
export type { NativeBridge as CefBridge, NativeQueryOptions as CefQueryOptions } from './bridge';


export class BridgeNativeApi implements NativeApi {
  private bridge: NativeBridge;
  private timeoutMs: number;
  constructor(bridge: NativeBridge, timeoutMs = 5000) {
    if (!Number.isFinite(timeoutMs) || timeoutMs <= 0 || timeoutMs > 60000) throw new Error("INVALID_TIMEOUT");
    this.bridge = bridge;
    this.timeoutMs = timeoutMs;
  }
  async ping(request: PingRequest): Promise<void> {
    if (!validatePing(request)) throw new Error("INVALID_ARGUMENT");
    const serialized = JSON.stringify(request);
    const expectedId = request.id;
    const expectedSequence = request.clientSequence;
    return new Promise<void>((resolve, reject) => {
      let settled = false;
      let queryId: number | undefined;
      const finish = (error?: Error) => {
        if (settled) return;
        settled = true;
        clearTimeout(timer);
        if (error) reject(error); else resolve();
      };
      const timer = setTimeout(() => {
        finish(new Error("RPC_TIMEOUT"));
        if (queryId !== undefined) {
          try { this.bridge.cancel(queryId); } catch { /* Context may already be gone. */ }
        }
      }, this.timeoutMs);
      try {
        queryId = this.bridge.query({
          request: serialized,
          persistent: false,
          onSuccess: response => {
            try {
              const acknowledgement: unknown = JSON.parse(response);
              if (!validatePing(acknowledgement) || acknowledgement.id !== expectedId ||
                  acknowledgement.clientSequence !== expectedSequence) throw new Error("INVALID_RESPONSE");
              finish();
            } catch { finish(new Error("INVALID_RESPONSE")); }
          },
          onFailure: (_code, message) => finish(new Error(message || "RPC_FAILED")),
        });
      } catch { finish(new Error("TRANSPORT_UNAVAILABLE")); }
    });
  }
}

export function createNativeConnection(allowFake: boolean): { api: NativeApi; mode: string } {
  const bridge = getNativeBridge();
  if (bridge) {
    return {
      api: new BridgeNativeApi(bridge),
      mode: window.lightoverleaf ? "Electron 原生通信" : "CEF 原生通信",
    };
  }
  if (allowFake) return { api: new FakeNativeApi(), mode: "浏览器开发模式 · Fake NativeApi" };
  return { api: { ping: async () => { throw new Error("TRANSPORT_UNAVAILABLE"); } }, mode: "原生通信不可用" };
}


// Source compatibility for the explicit legacy desktop and its existing fixtures.
export { BridgeNativeApi as CefNativeApi };
