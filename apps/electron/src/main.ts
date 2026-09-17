import { app, BrowserWindow, dialog, ipcMain, session, type IpcMainEvent } from 'electron';
import path from 'node:path';
import { BackendClient, maxWireBytes, type NativeReply } from './backend';
import { applicationUrl, registerAssets } from './assets';
import { validateRpcRequest, validateCancellation } from '../../../web/.generated/rpc/protocol';
import { validatePing } from '../../../web/.generated/rpc/contract';

type Pending = { persistent: boolean; sent: boolean };
let window: BrowserWindow | undefined;
let client: BackendClient | undefined;
let generation = 0;
let quitting = false;
let barrier: Promise<void> = Promise.resolve();
const pending = new Map<number, Pending>();
const children = new Set<BackendClient>();

function trusted(event: IpcMainEvent): boolean {
  return !!window && !window.isDestroyed() && event.sender === window.webContents &&
    event.senderFrame === window.webContents.mainFrame && event.senderFrame.url === applicationUrl;
}
function reply(value: NativeReply): void {
  const item = pending.get(value.queryId);
  if (!item || !window || window.isDestroyed()) return;
  if (!item.persistent || !value.success) pending.delete(value.queryId);
  window.webContents.send('lol:reply', value);
}
function disconnect(): void {
  generation++;
  pending.clear();
  const old = client;
  client = undefined;
  if (old) {
    barrier = old.close().finally(() => children.delete(old));
  }
}
async function backend(expected: number): Promise<BackendClient> {
  await barrier;
  if (expected !== generation || quitting) throw new Error('TRANSPORT_UNAVAILABLE');
  if (!client) {
    const created = new BackendClient(path.join(path.dirname(process.execPath), 'LightOverLeafBackend.exe'),
      result => { if (client === created) reply(result); },
      () => {
        if (client !== created) return;
        for (const queryId of [...pending.keys()])
          reply({ queryId, success: false, payload: 'TRANSPORT_UNAVAILABLE' });
      });
    client = created;
    children.add(created);
  }
  return client;
}

ipcMain.on('lol:request', async (event, input: unknown) => {
  if (!trusted(event) || !input || typeof input !== 'object') return;
  const value = input as { queryId?: unknown; request?: unknown; persistent?: unknown };
  const queryId = value.queryId;
  if (typeof queryId !== 'number' || !Number.isInteger(queryId) || queryId < 1 || queryId > 2147483647) return;
  const reject = (payload: string) => event.sender.send('lol:reply', { queryId, success: false, payload });
  if (pending.has(queryId)) { reject('DUPLICATE_REQUEST'); return; }
  if (pending.size >= 65) { reject('RESOURCE_EXHAUSTED'); return; }
  if (typeof value.request !== 'string' || typeof value.persistent !== 'boolean' ||
      Buffer.byteLength(value.request, 'utf8') > maxWireBytes) { reject('INVALID_ARGUMENT'); return; }
  const expected = generation;
  try {
    const request: unknown = JSON.parse(value.request);
    if (!validateRpcRequest(request) && !validateCancellation(request) && !validatePing(request)) {
      reject('INVALID_ARGUMENT'); return;
    }
    const item: Pending = { persistent: value.persistent, sent: false };
    pending.set(queryId, item);
    const channel = await backend(expected);
    if (expected !== generation || pending.get(queryId) !== item) return;
    let selection: string | null = null;
    if (validateRpcRequest(request) &&
        (request.method === 'workspace.open' || request.method === 'export.selectDestination')) {
      if (value.persistent) throw new Error('INVALID_ARGUMENT');
      const result = await dialog.showOpenDialog(window!, {
        title: request.method === 'workspace.open' ? '打开 LaTeX 项目' : '选择导出目录',
        properties: ['openDirectory', 'createDirectory'],
      });
      if (expected !== generation || pending.get(queryId) !== item) return;
      selection = result.canceled ? null : (result.filePaths[0] ?? null);
    }
    item.sent = true;
    channel.request(queryId, value.request, value.persistent, selection);
  } catch (error) {
    if (expected !== generation) return;
    const code = error instanceof Error && error.message === 'RESOURCE_EXHAUSTED' ?
      error.message : 'TRANSPORT_UNAVAILABLE';
    if (pending.has(queryId)) reply({ queryId, success: false, payload: code }); else reject(code);
  }
});
ipcMain.on('lol:cancel', (event, queryId: unknown) => {
  if (!trusted(event) || typeof queryId !== 'number' || !Number.isSafeInteger(queryId)) return;
  const item = pending.get(queryId);
  if (!item) return;
  pending.delete(queryId);
  if (item.sent) {
    try { client?.cancel(queryId); } catch { /* The closing child already cancels its session. */ }
  }
});

app.setName('LightOverLeaf');
void app.whenReady().then(async () => {
  await registerAssets(path.join(app.getAppPath(), 'web'));
  session.defaultSession.setPermissionRequestHandler((_contents, _permission, callback) => callback(false));
  session.defaultSession.setPermissionCheckHandler(() => false);
  window = new BrowserWindow({
    width: 1440, height: 960, minWidth: 900, minHeight: 600, backgroundColor: '#111318',
    title: 'LightOverLeaf', show: false, autoHideMenuBar: true,
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'), contextIsolation: true, sandbox: true,
      nodeIntegration: false, webSecurity: true, webviewTag: false, devTools: false,
    },
  });
  window.setMenu(null);
  window.webContents.on('will-prevent-unload', event => {
    if (!window || window.isDestroyed()) return;
    const choice = dialog.showMessageBoxSync(window, {
      type: 'warning', title: '未保存的修改', message: '仍有修改尚未保存，确定退出或重新加载？',
      buttons: ['取消', '继续'], defaultId: 0, cancelId: 0,
    });
    if (choice === 1) event.preventDefault();
  });
  window.webContents.setWindowOpenHandler(() => ({ action: 'deny' }));
  window.webContents.on('will-navigate', event => event.preventDefault());
  window.webContents.on('will-attach-webview', event => event.preventDefault());
  window.webContents.on('did-start-navigation', (_event, _url, inPlace, mainFrame) => {
    if (mainFrame && !inPlace) disconnect();
  });
  window.webContents.on('render-process-gone', () => {
    disconnect();
    if (window && !window.isDestroyed()) {
      void dialog.showMessageBox(window, { type: 'error', message: '编辑器进程已退出',
        detail: '可关闭窗口后重新打开应用，恢复最近保存的草稿。' });
    }
  });
  window.on('ready-to-show', () => window?.show());
  window.on('closed', () => { disconnect(); window = undefined; });
  await window.loadURL(applicationUrl);
}).catch(() => {
  dialog.showErrorBox('LightOverLeaf', '桌面资源加载失败，请重新运行 --dev 构建。');
  app.quit();
});
app.on('window-all-closed', () => app.quit());
app.on('before-quit', event => {
  if (quitting) return;
  event.preventDefault();
  // Do not stop native services until the renderer accepts closing dirty documents.
  if (window && !window.isDestroyed()) { window.close(); return; }
  quitting = true;
  void Promise.all([...children].map(child => child.close())).finally(() => app.quit());
});
