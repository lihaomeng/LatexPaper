import { NativeEventSubscription, createNativeEvents } from './subscription';
import { CefSystemTransport, SystemRpcClient } from './system';
import type { CefBridge } from './index';

/** Test probe uses the real bridge and checks three independent observations. */
export function cancellationProbe(bridge: CefBridge): { ready: Promise<void>; close(): void } {
  const controller = new AbortController();
  const id = crypto.randomUUID();
  let subscription: NativeEventSubscription | undefined;
  let closed = false;
  let acknowledged = false;
  let cancelled = false;
  let terminal = false;
  let acceptedSequence = 0;
  let generation = 0;
  let finish: (error?: string) => void = () => {};
  const ready = new Promise<void>((resolve, reject) => {
    const timer = setTimeout(() => finish('RPC_PROBE_TIMEOUT'), 8000);
    finish = (error?: string) => {
      if (closed) return;
      if (!error && !(acknowledged && cancelled && terminal)) return;
      closed = true;
      clearTimeout(timer);
      controller.abort();
      subscription?.close();
      if (error) reject(new Error(error)); else resolve();
    };
    subscription = new NativeEventSubscription(bridge, event => {
      if (event.id !== id) return;
      if (event.outcome !== 'cancelled' || event.generation !== generation || event.sequence <= acceptedSequence) {
        finish('RPC_PROBE_TERMINAL');
        return;
      }
      terminal = true;
      finish();
    }, code => finish(code), 5000, ticket => {
      if (ticket.id !== id || !subscription) return;
      acceptedSequence = ticket.sequence;
      generation = ticket.generation;
      void subscription.cancelRequest(ticket).then(accepted => {
        if (!accepted) { finish('RPC_PROBE_NOT_ACCEPTED'); return; }
        acknowledged = true;
        finish();
      }).catch(() => finish('RPC_PROBE_CANCEL_FAILED'));
    });
    void subscription.ready.then(async () => {
      const client = new SystemRpcClient(new CefSystemTransport(bridge));
      try {
        await client.request({ version: 2, id, clientSequence: 0, method: 'system.ping', params: {} }, controller.signal);
        finish('RPC_PROBE_UNEXPECTED_SUCCESS');
      } catch (error) {
        if (!(error instanceof Error) || error.message !== 'RPC_CANCELLED') { finish('RPC_PROBE_RESPONSE'); return; }
        cancelled = true;
        finish();
      }
    }).catch(() => finish('RPC_PROBE_CONNECTION'));
  });
  return { ready, close: () => finish('RPC_CANCELLED') };
}

export function createStartupEvents(allowFake: boolean, onError: (code: string) => void, smoke: boolean):
Pick<NativeEventSubscription, 'ready' | 'close'> {
  if (!smoke) return createNativeEvents(allowFake, onError);
  if (!window.cefQuery || !window.cefQueryCancel) {
    return { ready: Promise.reject(new Error('TRANSPORT_UNAVAILABLE')), close: () => {} };
  }
  const probe = cancellationProbe({ query: window.cefQuery.bind(window), cancel: window.cefQueryCancel.bind(window) });
  let events: Pick<NativeEventSubscription, 'ready' | 'close'> | undefined;
  let closed = false;
  return {
    ready: probe.ready.then(async () => {
      if (closed) throw new Error('RPC_CANCELLED');
      events = createNativeEvents(false, onError);
      await events.ready;
    }),
    close: () => { closed = true; probe.close(); events?.close(); },
  };
}
