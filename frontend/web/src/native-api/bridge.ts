export interface NativeQueryOptions {
  request: string;
  persistent: boolean;
  onSuccess(response: string): void;
  onFailure(code: number, message: string): void;
}
export interface NativeBridge {
  query(options: NativeQueryOptions): number;
  cancel(id: number): void;
}
declare global {
  interface Window {
    lightoverleaf?: NativeBridge;
    /** Legacy Qt/CEF build only. */
    cefQuery?: NativeBridge['query'];
    cefQueryCancel?: NativeBridge['cancel'];
  }
}
export function getNativeBridge(): NativeBridge | undefined {
  if (window.lightoverleaf) return window.lightoverleaf;
  if (window.cefQuery && window.cefQueryCancel)
    return { query: window.cefQuery.bind(window), cancel: window.cefQueryCancel.bind(window) };
  return undefined;
}
