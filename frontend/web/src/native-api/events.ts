import { validateRequestEvent, validateCancellation, type RequestEvent, type Cancellation } from '../../.generated/rpc/protocol';

/** One cursor per live transport session; never reuse it after Renderer reconnect. */
export class RpcEventCursor {
  private sequence = 0;
  constructor(private readonly sessionId: string) {
    if (!validateCancellation({ version: 2, method: 'rpc.cancel', sessionId, id: 'probe', generation: 1 }))
      throw new Error('INVALID_SESSION');
  }
  accept(value: unknown): RequestEvent | undefined {
    if (!validateRequestEvent(value) || value.sessionId !== this.sessionId || value.sequence <= this.sequence) return;
    // Return an owned snapshot; transport buffers/callers must not mutate accepted state.
    this.sequence = value.sequence;
    return { ...value };
  }
  cancellation(id: string, generation: number): Cancellation {
    const command = { version: 2, method: 'rpc.cancel', sessionId: this.sessionId, id, generation };
    if (!validateCancellation(command)) throw new Error('INVALID_ARGUMENT');
    return command;
  }
}
