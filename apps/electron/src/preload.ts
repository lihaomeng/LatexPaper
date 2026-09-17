import { contextBridge, ipcRenderer } from 'electron';

type QueryOptions = {
  request: string; persistent: boolean;
  onSuccess(response: string): void;
  onFailure(code: number, message: string): void;
};
const pending = new Map<number, QueryOptions>();
let sequence = 0;
ipcRenderer.on('lol:reply', (_event, value: { queryId: number; success: boolean; payload: string }) => {
  const options = pending.get(value.queryId);
  if (!options) return;
  if (!options.persistent || !value.success) pending.delete(value.queryId);
  if (value.success) options.onSuccess(value.payload);
  else options.onFailure(1, value.payload);
});
contextBridge.exposeInMainWorld('lightoverleaf', {
  query(options: QueryOptions): number {
    if (pending.size >= 65 || sequence >= 2147483647) throw new Error('RESOURCE_EXHAUSTED');
    if (typeof options.request !== 'string' || typeof options.persistent !== 'boolean' ||
        typeof options.onSuccess !== 'function' || typeof options.onFailure !== 'function')
      throw new Error('INVALID_ARGUMENT');
    const queryId = ++sequence;
    pending.set(queryId, options);
    ipcRenderer.send('lol:request', { queryId, request: options.request, persistent: options.persistent });
    return queryId;
  },
  cancel(queryId: number): void {
    if (!pending.delete(queryId)) return;
    ipcRenderer.send('lol:cancel', queryId);
  },
});
