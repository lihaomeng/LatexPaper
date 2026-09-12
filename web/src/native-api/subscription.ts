import { validateConnectedEvent, validateAcceptedEvent, type AcceptedEvent, type RequestEvent } from '../../.generated/rpc/protocol';
import { RpcEventCursor } from './events';
import { CancellationRpcClient } from './cancellation';
import { CefSystemTransport } from './system';
import type { CefBridge } from './index';

/** One persistent query, owned by the mounted application, never by a Feature. */
export class NativeEventSubscription {
  readonly ready: Promise<void>;
  private queryId: number | undefined;
  private closed = false;
  private cursor: RpcEventCursor | undefined;
  private rejectReady: (error: Error) => void = () => {};
  private timer: ReturnType<typeof setTimeout> | undefined;
  private readonly cancellations: CancellationRpcClient;
  private sessionId = '';
  private acceptedGeneration = 0;
  private eventSequence = 0;

  constructor(private readonly bridge: CefBridge, onEvent: (event: RequestEvent) => void,
    private readonly onError: (code: string) => void, timeoutMs = 5000,
    onAccepted: (event: AcceptedEvent) => void = () => {}) {
    if (!Number.isFinite(timeoutMs) || timeoutMs <= 0 || timeoutMs > 60000) throw new Error('INVALID_TIMEOUT');
    this.cancellations = new CancellationRpcClient(new CefSystemTransport(bridge), timeoutMs);
    this.ready = new Promise<void>((resolve, reject) => {
      this.rejectReady = reject;
      this.timer = setTimeout(() => this.fail('RPC_TIMEOUT'), timeoutMs);
      try {
        this.queryId = bridge.query({
          request: JSON.stringify({ version: 2, id: crypto.randomUUID(), clientSequence: 0, method: 'rpc.subscribe', params: {} }),
          persistent: true,
          onSuccess: wire => {
            if (this.closed) return;
            try {
              if (wire.length > 8192) throw new Error('size');
              const value: unknown = JSON.parse(wire);
              if (!this.cursor) {
                if (!validateConnectedEvent(value)) throw new Error('handshake');
                this.cursor = new RpcEventCursor(value.sessionId);
                this.sessionId = value.sessionId;
                clearTimeout(this.timer);
                resolve();
                return;
              }
              if (validateAcceptedEvent(value)) {
                if (value.sessionId === this.sessionId && value.generation > this.acceptedGeneration &&
                    value.sequence > this.eventSequence) {
                  this.acceptedGeneration = value.generation;
                  this.eventSequence = value.sequence;
                  onAccepted({ ...value });
                }
                return;
              }
              const event = this.cursor.accept(value);
              if (event && event.sequence > this.eventSequence) {
                this.eventSequence = event.sequence;
                onEvent(event);
              }
            } catch { this.fail('INVALID_EVENT'); }
          },
          onFailure: (_code, message) => this.fail(message || 'TRANSPORT_UNAVAILABLE'),
        });
        if (this.closed) this.cancelQuery();
      } catch { this.fail('TRANSPORT_UNAVAILABLE'); }
    });
  }

  async cancelRequest(ticket: AcceptedEvent): Promise<boolean> {
    if (!validateAcceptedEvent(ticket)) throw new Error('INVALID_ARGUMENT');
    const expected = { ...ticket };
    await this.ready;
    if (this.closed || !this.cursor) throw new Error('TRANSPORT_UNAVAILABLE');
    if (expected.sessionId !== this.sessionId) throw new Error('INVALID_SESSION');
    return this.cancellations.cancel(this.cursor.cancellation(expected.id, expected.generation));
  }

  close(): void {
    if (this.closed) return;
    this.closed = true;
    clearTimeout(this.timer);
    this.cancellations.close();
    this.rejectReady(new Error('RPC_CANCELLED'));
    this.cancelQuery();
  }

  private fail(code: string): void {
    if (this.closed) return;
    this.rejectReady(new Error(code));
    this.close();
    this.onError(code);
  }

  private cancelQuery(): void {
    if (this.queryId === undefined) return;
    const id = this.queryId;
    this.queryId = undefined;
    try { this.bridge.cancel(id); } catch { /* The renderer context may already be gone. */ }
  }
}

export function createNativeEvents(allowFake: boolean, onError: (code: string) => void): Pick<NativeEventSubscription, 'ready' | 'close'> {
  if (window.cefQuery && window.cefQueryCancel) {
    return new NativeEventSubscription({ query: window.cefQuery.bind(window), cancel: window.cefQueryCancel.bind(window) }, () => {}, onError);
  }
  return { ready: allowFake ? Promise.resolve() : Promise.reject(new Error('TRANSPORT_UNAVAILABLE')), close: () => {} };
}
