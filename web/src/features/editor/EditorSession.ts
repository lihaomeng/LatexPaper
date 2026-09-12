import { validDraftPath } from "../session";
import { monaco } from "./monaco";
import { extractOutline, type OutlineEntry } from "./outline";
export interface EditorSeed { files: { path: string; content: string }[]; open: string[]; active: string | null }
export interface EditorView {
  files: { path: string; dirty: boolean }[]; open: string[]; active: string | null;
  outline: OutlineEntry[]; revision: number; line: number; column: number;
}
interface Entry { model: monaco.editor.ITextModel; savedVersion: number; savedContent: string; listener: monaco.IDisposable }
export class EditorSession {
  private readonly modelNamespace = crypto.randomUUID();
  private files = new Map<string, Entry>();
  private listeners = new Set<() => void>();
  private open: string[];
  private active: string | null;
  private revision = 0;
  private line = 1;
  private column = 1;
  private view: EditorView;
  private outlineCache = { path: "", version: -1, entries: [] as OutlineEntry[] };
  constructor(seed: EditorSeed) {
    this.open = [...seed.open];
    this.active = seed.active;
    for (const file of seed.files) this.addModel(file.path, file.content);
    this.view = this.buildView();
  }
  private addModel(path: string, content: string) {
    const model = monaco.editor.createModel(content, /\.(tex|bib)$/i.test(path) ? "latex" : "plaintext",
      monaco.Uri.parse("inmemory://session-" + this.modelNamespace + "/" + encodeURIComponent(path)));
    model.updateOptions({ tabSize: 2, insertSpaces: true });
    const listener = model.onDidChangeContent(() => this.emit(true));
    this.files.set(path, { model, savedVersion: model.getAlternativeVersionId(), savedContent: content, listener });
  }
  private buildView(): EditorView {
    const model = this.active ? this.files.get(this.active)!.model : null;
    if (this.outlineCache.path !== this.active || this.outlineCache.version !== model?.getVersionId()) {
      this.outlineCache = { path: this.active ?? "", version: model?.getVersionId() ?? -1, entries: model ? extractOutline(model.getValue()) : [] };
    }
    return { files: [...this.files].map(([path, entry]) => ({ path, dirty: entry.model.getAlternativeVersionId() !== entry.savedVersion })),
      open: [...this.open], active: this.active, revision: this.revision, line: this.line, column: this.column,
      outline: this.outlineCache.entries };
  }
  private emit(changed: boolean) {
    if (changed) this.revision++;
    this.view = this.buildView();
    for (const listener of this.listeners) listener();
  }
  subscribe = (listener: () => void) => { this.listeners.add(listener); return () => { this.listeners.delete(listener); }; };
  getView = () => this.view;
  model(path: string) { return this.files.get(path)?.model ?? null; }
  recoverySnapshot(path: string) {
    const entry = this.files.get(path);
    return entry ? { content: entry.model.getValue(), baseContent: entry.savedContent, version: entry.model.getVersionId(),
      dirty: entry.model.getAlternativeVersionId() !== entry.savedVersion } : null;
  }
  replaceFromDisk(path: string, content: string, expectedVersion: number): boolean {
    const entry = this.files.get(path);
    if (!entry || entry.model.getVersionId() !== expectedVersion) return false;
    entry.model.pushStackElement();
    entry.model.pushEditOperations([], [{ range: entry.model.getFullModelRange(), text: content }], () => null);
    entry.model.pushStackElement();
    entry.savedVersion = entry.model.getAlternativeVersionId();
    entry.savedContent = content;
    this.emit(true);
    return true;
  }
  replaceForMerge(path: string, content: string, expectedVersion: number): boolean {
    const entry = this.files.get(path);
    if (!entry || entry.model.getVersionId() !== expectedVersion) return false;
    entry.model.pushStackElement();
    entry.model.pushEditOperations([], [{ range: entry.model.getFullModelRange(), text: content }], () => null);
    entry.model.pushStackElement();
    this.emit(true);
    return true;
  }
  load(path: string, content: string) {
    if (!path || this.files.has(path)) { if (this.files.has(path)) this.activate(path); return; }
    if (this.files.size >= 32) throw new Error("同时打开的本地文件已达到 32 个上限。");
    this.addModel(path, content);
    this.activate(path);
  }
  activate(path: string) {
    if (!this.files.has(path)) return;
    if (!this.open.includes(path)) this.open.push(path);
    this.active = path;
    this.line = 1; this.column = 1;
    this.emit(true);
  }
  close(path: string) {
    const index = this.open.indexOf(path);
    if (index < 0) return;
    this.open = this.open.filter(item => item !== path);
    if (this.active === path) this.active = this.open[Math.min(index, this.open.length - 1)] ?? null;
    this.emit(true);
  }
  create(path: string) {
    if (!validDraftPath(path)) throw new Error("无效草稿路径。");
    const key = path.toLowerCase();
    if (this.files.size >= 32 || [...this.files.keys()].some(item => {
      const existing = item.toLowerCase();
      return existing === key || existing.startsWith(key + "/") || key.startsWith(existing + "/");
    }))
      throw new Error("文件已存在，或草稿文件数达到 32 个上限。");
    this.addModel(path, path.endsWith(".tex") ? "% " + path + "\n" : "");
    this.files.get(path)!.savedVersion = -1;
    this.activate(path);
  }
  rename(path: string, destination: string) {
    const entry = this.files.get(path);
    if (!entry || this.files.has(destination)) return;
    this.files.delete(path); this.files.set(destination, entry);
    this.open = this.open.map(item => item === path ? destination : item);
    if (this.active === path) this.active = destination;
    this.emit(true);
  }
  renameSaved(path: string, destination: string, expectedVersion: number): boolean {
    const entry = this.files.get(path);
    if (!entry || this.files.has(destination) || entry.model.getVersionId() !== expectedVersion)
      return false;
    this.files.delete(path);
    this.files.set(destination, entry);
    this.open = this.open.map(item => item === path ? destination : item);
    if (this.active === path) this.active = destination;
    entry.savedVersion = entry.model.getAlternativeVersionId();
    entry.savedContent = entry.model.getValue();
    this.emit(true);
    return true;
  }
  forget(path: string) {
    const entry = this.files.get(path);
    if (!entry) return;
    this.close(path);
    this.files.delete(path);
    entry.listener.dispose(); entry.model.dispose();
    this.emit(true);
  }
  setCursor(line: number, column: number) {
    if (line === this.line && column === this.column) return;
    this.line = line; this.column = column;
    this.emit(false);
  }
  capture() {
    return {
      snapshot: { version: 1 as const, files: [...this.files].map(([path, entry]) => ({ path, content: entry.model.getValue() })),
        open: [...this.open], active: this.active },
      versions: new Map([...this.files].map(([path, entry]) => [path, entry.model.getAlternativeVersionId()])),
      revision: this.revision,
    };
  }
  acknowledge(versions: Map<string, number>, contents?: Map<string, string>) {
    for (const [path, version] of versions) {
      const entry = this.files.get(path);
      if (entry) {
        entry.savedVersion = version;
        const saved = contents?.get(path);
        if (saved !== undefined) entry.savedContent = saved;
        else if (entry.model.getAlternativeVersionId() === version) entry.savedContent = entry.model.getValue();
      }
    }
    this.emit(false);
  }
  dispose() {
    this.listeners.clear();
    for (const entry of this.files.values()) { entry.listener.dispose(); entry.model.dispose(); }
    this.files.clear();
  }
}
