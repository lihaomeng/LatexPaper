import { validateRpcRequest, validateResponse, validateCapabilitiesResponse,
  type RpcRequest, type Response, type CapabilitiesResponseResult } from '../../.generated/rpc/protocol';
import type { CefBridge } from './index';

const maxRpcWireBytes = 32 * 1024 * 1024;

export interface SystemTransport {
  send(request: string, success: (response: string) => void, failure: (code: string) => void): () => void;
}

export class CefSystemTransport implements SystemTransport {
  constructor(private readonly bridge: CefBridge) {}
  send(request: string, success: (response: string) => void, failure: (code: string) => void): () => void {
    const id = this.bridge.query({ request, persistent: false, onSuccess: success,
      onFailure: (_code, message) => failure(message || 'TRANSPORT_UNAVAILABLE') });
    return () => this.bridge.cancel(id);
  }
}

/** Only for explicit browser development; no native services are simulated. */
export class FakeSystemTransport implements SystemTransport {
  send(serialized: string, success: (response: string) => void): () => void {
    const request: unknown = JSON.parse(serialized);
    if (!validateRpcRequest(request)) throw new Error('INVALID_ARGUMENT');
    const base = { version: 2, id: request.id, clientSequence: request.clientSequence };
    const response = request.method === 'system.ping' ? { ...base, ok: true, method: request.method, result: {} }
      : request.method === 'system.getCapabilities' ? { ...base, ok: true, method: request.method,
        result: { nativeFiles: false, build: false, pdf: false, syncTex: false } }
      : { ...base, ok: false, error: { code: 'METHOD_NOT_FOUND', messageKey: 'rpc.error' } };
    success(JSON.stringify(response));
    return () => {};
  }
}

export class SystemRpcClient {
  private readonly pending = new Set<string>();
  constructor(private readonly transport: SystemTransport, private readonly timeoutMs = 5000) {
    if (!Number.isFinite(timeoutMs) || timeoutMs <= 0 || timeoutMs > 60000) throw new Error('INVALID_TIMEOUT');
  }

  async request(request: RpcRequest, signal?: AbortSignal): Promise<Response> {
    if (!validateRpcRequest(request)) throw new Error('INVALID_ARGUMENT');
    if (signal?.aborted) throw new Error('RPC_CANCELLED');
    // Capture before asynchronous work: caller mutations cannot change correlation.
    const serialized = JSON.stringify(request);
    if (new TextEncoder().encode(serialized).length > maxRpcWireBytes)
      throw new Error('RESOURCE_EXHAUSTED');
    const { id, clientSequence, method } = request;
    if (this.pending.has(id)) throw new Error('DUPLICATE_REQUEST');
    if (this.pending.size >= 64) throw new Error('RESOURCE_EXHAUSTED');
    this.pending.add(id);
    return new Promise<Response>((resolve, reject) => {
      let settled = false;
      let cancel: (() => void) | undefined;
      let cancellationRequested = false;
      const release = () => { try { cancel?.(); } catch { /* Context may already be gone. */ } };
      const finish = (error?: string, response?: Response) => {
        if (settled) return;
        settled = true;
        clearTimeout(timer);
        signal?.removeEventListener('abort', abort);
        this.pending.delete(id);
        if (error) reject(new Error(error)); else if (response) resolve(response);
      };
      const abort = () => {
        if (settled) return;
        cancellationRequested = true;
        finish('RPC_CANCELLED');
        release();
      };
      const timer = setTimeout(() => {
        if (settled) return;
        cancellationRequested = true;
        finish('RPC_TIMEOUT');
        release();
      }, this.timeoutMs);
      signal?.addEventListener('abort', abort, { once: true });
      try {
        cancel = this.transport.send(serialized, wire => {
          if (settled) return;
          try {
            if (new TextEncoder().encode(wire).length > maxRpcWireBytes) throw new Error('oversize');
            const response: unknown = JSON.parse(wire);
            if (!validateResponse(response) || response.id !== id || response.clientSequence !== clientSequence ||
                (response.ok && response.method !== method)) throw new Error('correlation');
            if (!response.ok) finish(response.error.code); else finish(undefined, response);
          } catch { finish('INVALID_RESPONSE'); }
        }, code => finish(code || 'TRANSPORT_UNAVAILABLE'));
        if (cancellationRequested) release();
      } catch { finish('TRANSPORT_UNAVAILABLE'); }
    });
  }

  async ping(signal?: AbortSignal): Promise<void> {
    await this.request({ version: 2, id: crypto.randomUUID(), method: 'system.ping', params: {}, clientSequence: 0 }, signal);
  }

  async getCapabilities(signal?: AbortSignal): Promise<CapabilitiesResponseResult> {
    const response = await this.request({ version: 2, id: crypto.randomUUID(), method: 'system.getCapabilities', params: {}, clientSequence: 0 }, signal);
    if (!validateCapabilitiesResponse(response)) throw new Error('INVALID_RESPONSE');
    return response.result;
  }
}

export function createSystemConnection(allowFake: boolean): SystemRpcClient {
  if (window.cefQuery && window.cefQueryCancel) {
    return new SystemRpcClient(new CefSystemTransport({ query: window.cefQuery.bind(window), cancel: window.cefQueryCancel.bind(window) }));
  }
  if (allowFake) return new SystemRpcClient(new FakeSystemTransport());
  return new SystemRpcClient({ send: () => { throw new Error('TRANSPORT_UNAVAILABLE'); } });
}
