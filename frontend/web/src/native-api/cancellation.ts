import { validateCancellation, validateCancellationResponse, type Cancellation } from '../../.generated/rpc/protocol';
import type { SystemTransport } from './system';

/** Acknowledges stop requests; true is not proof that the operation has stopped. */
export class CancellationRpcClient {
  private readonly pending = new Set<() => void>();
  private closed = false;
  constructor(private readonly transport: SystemTransport, private readonly timeoutMs = 5000) {
    if (!Number.isFinite(timeoutMs) || timeoutMs <= 0 || timeoutMs > 60000) throw new Error('INVALID_TIMEOUT');
  }
  async cancel(command: Cancellation): Promise<boolean> {
    if (!validateCancellation(command)) throw new Error('INVALID_ARGUMENT');
    if (this.closed) throw new Error('TRANSPORT_UNAVAILABLE');
    if (this.pending.size >= 64) throw new Error('RESOURCE_EXHAUSTED');
    const expected = { ...command };
    return new Promise<boolean>((resolve, reject) => {
      let settled = false;
      let release: (() => void) | undefined;
      let releaseRequested = false;
      const cancelQuery = () => {
        releaseRequested = true;
        try { release?.(); } catch { /* Context may already be gone. */ }
        release = undefined;
      };
      const finish = (error?: string, accepted = false) => {
        if (settled) return;
        settled = true;
        clearTimeout(timer);
        this.pending.delete(dispose);
        if (error) reject(new Error(error)); else resolve(accepted);
      };
      const dispose = () => { finish('RPC_CANCELLED'); cancelQuery(); };
      const timer = setTimeout(() => { finish('RPC_TIMEOUT'); cancelQuery(); }, this.timeoutMs);
      this.pending.add(dispose);
      try {
        release = this.transport.send(JSON.stringify(expected), wire => {
          if (settled) return;
          try {
            if (wire.length > 8192) throw new Error('size');
            const value: unknown = JSON.parse(wire);
            if (!validateCancellationResponse(value) || value.sessionId !== expected.sessionId ||
                value.id !== expected.id || value.generation !== expected.generation) throw new Error('correlation');
            finish(undefined, value.accepted);
          } catch { finish('INVALID_RESPONSE'); }
        }, code => finish(code || 'TRANSPORT_UNAVAILABLE'));
        if (releaseRequested) cancelQuery();
      } catch { finish('TRANSPORT_UNAVAILABLE'); }
    });
  }
  close(): void {
    this.closed = true;
    for (const dispose of [...this.pending]) dispose();
  }
}
