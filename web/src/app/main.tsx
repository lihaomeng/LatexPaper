import { createRoot } from "react-dom/client";
import { useCallback, useEffect, useRef, useState, useSyncExternalStore } from "react";
import { createNativeConnection, createSystemConnection, createStartupEvents, WorkspaceRpcClient } from "../native-api";
import type { WorkspaceStateResponseResult } from "../../.generated/rpc/protocol";
import { EditorSession, EditorSurface, type EditorCommands } from "../features/editor";
import { Explorer } from "../features/explorer";
import { PreviewPanel } from "../features/preview";
import { validDraftPath } from "../features/session";
import { Icon, type IconName } from "../shared/Icon";
import { Splitter } from "../shared/Splitter";
import { useDraftWorkspace, type CacheStatus } from "./useDraftWorkspace";
import { LocalDocumentSaveError, saveLocalDocuments } from "./saveLocalDocuments";
import { adoptDiskVersion, compareDocument, type DocumentComparison } from "./documentRecovery";
import { reconcileWorkspaceRefresh } from "./refreshLocalWorkspace";
import { validWorkspaceDirectoryId } from "./workspacePaths";
import "./style.css";

const connection = createNativeConnection(import.meta.env.DEV);
const systemConnection = createSystemConnection(import.meta.env.DEV);
const workspaceConnection = new WorkspaceRpcClient(systemConnection);
const labels: Record<CacheStatus, string> = { loading: "正在读取草稿", pending: "有待缓存修改", saving: "正在缓存草稿", saved: "草稿已缓存", error: "草稿缓存失败" };
function Tool({ icon, label, onClick, pressed, disabled }: { icon: IconName; label: string; onClick(): void; pressed?: boolean; disabled?: boolean }) {
  return <button className="tool-button" title={label} aria-label={label} aria-pressed={pressed} onClick={onClick} disabled={disabled}><Icon name={icon} /></button>;
}
function Workbench({ session: draftSession, status, error, save: saveDraft }: {
  session: EditorSession; status: CacheStatus; error: string; save(): void;
}) {
  const [localSession, setLocalSession] = useState<EditorSession | null>(null);
  const localSessionRef = useRef<EditorSession | null>(null);
  const [project, setProject] = useState<WorkspaceStateResponseResult | null>(null);
  const revisions = useRef(new Map<string, string>());
  const [localStatus, setLocalStatus] = useState<CacheStatus>("saved");
  const [localError, setLocalError] = useState("");
  const [workspaceBusy, setWorkspaceBusy] = useState(false);
  const [nativeFiles, setNativeFiles] = useState(false);
  const documentOpenSequence = useRef(0);
  const saveInFlight = useRef(false);
  const saveDeferred = useRef(false);
  const conflictRef = useRef<string | null>(null);
  const [conflictFile, setConflictFile] = useState<string | null>(null);
  const [comparison, setComparison] = useState<DocumentComparison | null>(null);
  const [recoveryBusy, setRecoveryBusy] = useState(false);
  const [recoveryError, setRecoveryError] = useState("");
  const recoverySequence = useRef(0);
  const refreshInFlight = useRef(false);
  const refreshSequence = useRef(0);
  const refreshDeferred = useRef(false);
  const mutationBusy = useRef(false);
  const [fileOperation, setFileOperation] = useState<'create' | 'rename' | 'remove'>('create');
  const [operationSource, setOperationSource] = useState('');
  const [directoryOperation, setDirectoryOperation] = useState<'create' | 'rename' | 'remove'>('create');
  const [directorySource, setDirectorySource] = useState('');
  const [selectedDirectory, setSelectedDirectory] = useState<string | null>(null);
  const session = localSession ?? draftSession;
  const view = useSyncExternalStore(session.subscribe, session.getView);
  const editor = useRef<EditorCommands>(null);
  const [native, setNative] = useState<"pending" | "ready" | "error">("pending");
  const [editorReady, setEditorReady] = useState(false);
  const [sidebar, setSidebar] = useState(true);
  const [preview, setPreview] = useState(true);
  const [wrap, setWrap] = useState(true);
  const [sidebarWidth, setSidebarWidth] = useState(252);
  const [editorWidth, setEditorWidth] = useState(Math.max(380, window.innerWidth * .43));
  const [filter, setFilter] = useState("");
  const [showSearch, setShowSearch] = useState(false);
  const [modal, setModal] = useState<"new" | "help" | "conflict" | "manage" | "directory" | null>(null);
  const [filename, setFilename] = useState("");
  const [fileError, setFileError] = useState("");
  const [outlineLine, setOutlineLine] = useState<number | null>(null);
  const search = useRef<HTMLInputElement>(null);
  const activeStatus = conflictFile ? "error" : localSession ? localStatus : status;
  const activeError = conflictFile ? `${conflictFile} 已被外部修改，自动保存已暂停。请对比版本后处理。` : localError || (localSession ? "" : error);
  const hasChanges = localSession ? view.files.some(file => file.dirty) || saveInFlight.current : activeStatus !== "saved";
  const save = useCallback(() => {
    if (!localSession) { saveDraft(); return; }
    if (refreshInFlight.current) { saveDeferred.current = true; return; }
    if (mutationBusy.current || conflictRef.current || saveInFlight.current || !localSession.getView().files.some(file => file.dirty)) return;
    saveDeferred.current = false;
    saveInFlight.current = true;
    setLocalStatus("saving"); setLocalError("");
    void saveLocalDocuments(localSession, revisions.current, workspaceConnection,
      () => localSessionRef.current === localSession).then(() => {
      if (localSessionRef.current !== localSession) return;
      setLocalStatus(localSession.getView().files.some(item => item.dirty) ? "pending" : "saved");
    }).catch(failure => {
      if (localSessionRef.current !== localSession) return;
      const code = (failure as Error).message;
      if (failure instanceof LocalDocumentSaveError && code === "FILE_CONFLICT") {
        conflictRef.current = failure.fileId;
        setConflictFile(failure.fileId);
      }
      setLocalStatus("error");
      setLocalError(code === "FILE_CONFLICT" ? "文件已被外部修改，请使用对比版本处理冲突。" :
        "保存本地文件失败：" + code);
    }).finally(() => { saveInFlight.current = false; });
  }, [localSession, saveDraft]);
  const refreshWorkspace = useCallback(async () => {
    if (!localSession || !project || workspaceBusy || refreshInFlight.current || mutationBusy.current ||
        saveInFlight.current || conflictRef.current) return;
    refreshInFlight.current = true;
    const sequence = ++refreshSequence.current;
    const isCurrent = () => localSessionRef.current === localSession && sequence === refreshSequence.current;
    try {
      const next = await workspaceConnection.refresh();
      if (!isCurrent() || next.workspaceId !== project.workspaceId) return;
      if (next.revision === project.revision && !refreshDeferred.current) return;
      const summary = await reconcileWorkspaceRefresh(localSession, revisions.current, next,
        workspaceConnection, isCurrent);
      if (!isCurrent()) return;
      refreshDeferred.current = summary.deferred > 0;
      setProject(next);
      setSelectedDirectory(current => current &&
        next.entries.some(entry => entry.directory && entry.fileId === current) ? current : null);
      const dirty = localSession.getView().files.some(file => file.dirty);
      setLocalStatus(dirty ? "pending" : "saved");
      setLocalError(summary.deferred ? `磁盘目录已刷新；${summary.deferred} 个并发编辑文件保持原内容。` :
        `磁盘目录已刷新：重载 ${summary.reloaded} 个，关闭已删除 ${summary.removed} 个。`);
    } catch (failure) {
      if (isCurrent()) setLocalError((failure as Error).message === "STALE_WORKSPACE" ?
        "刷新期间项目状态已变化，本次结果已忽略。" : "刷新本地项目失败：" + (failure as Error).message);
    } finally {
      if (sequence === refreshSequence.current) {
        refreshInFlight.current = false;
        if (saveDeferred.current && localSessionRef.current === localSession) void save();
      }
    }
  }, [localSession, project, save, workspaceBusy]);
  useEffect(() => {
    if (!localSession) return;
    let timer: ReturnType<typeof setTimeout> | undefined;
    const unsubscribe = localSession.subscribe(() => {
      if (conflictRef.current) return;
      if (!localSession.getView().files.some(file => file.dirty)) return;
      setLocalStatus("pending");
      clearTimeout(timer);
      timer = setTimeout(save, 600);
    });
    return () => { clearTimeout(timer); unsubscribe(); };
  }, [localSession, save]);
  useEffect(() => {
    if (!localSession) return;
    const timer = window.setInterval(() => {
      if (document.visibilityState === "visible") void refreshWorkspace();
    }, 5000);
    return () => window.clearInterval(timer);
  }, [localSession, refreshWorkspace]);
  useEffect(() => {
    if (modal !== "conflict") return;
    return () => { ++recoverySequence.current; };
  }, [modal]);
  useEffect(() => () => {
    ++documentOpenSequence.current;
    ++recoverySequence.current;
    ++refreshSequence.current;
    saveDeferred.current = false;
    const current = localSessionRef.current;
    localSessionRef.current = null;
    current?.dispose();
  }, []);
  useEffect(() => {
    let active = true;
    let connectionFailed = false;
    const events = createStartupEvents(import.meta.env.DEV, () => {
      connectionFailed = true;
      if (active) setNative("error");
    }, window.location.hash === "#rpc-smoke");
    events.ready.then(() => connection.api.ping({ version: 1, id: "workbench-startup", method: "system.ping", params: {}, clientSequence: 0 }))
      .then(() => systemConnection.ping())
      .then(() => systemConnection.getCapabilities())
      .then(capabilities => { if (active && !connectionFailed) { setNativeFiles(capabilities.nativeFiles); setNative("ready"); } })
      .catch(() => { if (active) setNative("error"); });
    return () => { active = false; events.close(); };
  }, []);
  useEffect(() => {
    if (native === "ready" && editorReady && activeStatus === "saved") document.title = "LightOverLeaf · Native Ready";
    else if (native === "error") document.title = "LightOverLeaf · Native Error";
  }, [native, editorReady, activeStatus]);
  useEffect(() => {
    const keydown = (event: KeyboardEvent) => {
      if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "s") { event.preventDefault(); save(); }
      if (event.key === "Escape") setModal(null);
    };
    window.addEventListener("keydown", keydown);
    return () => window.removeEventListener("keydown", keydown);
  }, [save]);
  useEffect(() => {
    const unload = (event: BeforeUnloadEvent) => { if (hasChanges) { event.preventDefault(); event.returnValue = ""; } };
    window.addEventListener("beforeunload", unload);
    return () => window.removeEventListener("beforeunload", unload);
  }, [hasChanges]);
  useEffect(() => { if (showSearch) search.current?.focus(); }, [showSearch]);
  useEffect(() => {
    if (!modal) return;
    const previous = document.activeElement as HTMLElement | null;
    const dialog = document.querySelector<HTMLElement>(".modal");
    const focusable = () => Array.from(dialog?.querySelectorAll<HTMLElement>("button:not([disabled]), input, textarea") ?? []);
    (dialog?.querySelector<HTMLInputElement>("input") ?? focusable()[0])?.focus();
    const trap = (event: KeyboardEvent) => {
      if (event.key !== "Tab") return;
      const elements = focusable();
      const first = elements[0], last = elements.at(-1);
      if (event.shiftKey && document.activeElement === first) { event.preventDefault(); last?.focus(); }
      else if (!event.shiftKey && document.activeElement === last) { event.preventDefault(); first?.focus(); }
    };
    dialog?.addEventListener("keydown", trap);
    return () => { dialog?.removeEventListener("keydown", trap); if (previous?.isConnected) previous.focus(); };
  }, [modal]);
  const inspectConflict = async () => {
    if (!localSession || !conflictRef.current || saveInFlight.current) return;
    const sequence = ++recoverySequence.current;
    const fileId = conflictRef.current;
    setModal("conflict"); setComparison(null); setRecoveryError(""); setRecoveryBusy(true);
    const isCurrent = () => localSessionRef.current === localSession && sequence === recoverySequence.current;
    try {
      const result = await compareDocument(localSession, fileId, workspaceConnection, isCurrent);
      if (isCurrent()) setComparison(result);
    } catch (failure) {
      if (isCurrent()) setRecoveryError((failure as Error).message === "STALE_DOCUMENT"
        ? "读取期间编辑内容已变化，请重新读取对比。" : "读取磁盘版本失败：" + (failure as Error).message);
    } finally { if (isCurrent()) setRecoveryBusy(false); }
  };
  const acceptDisk = () => {
    if (!localSession || !comparison || recoveryBusy || saveInFlight.current) return;
    try {
      adoptDiskVersion(localSession, comparison, revisions.current,
        () => localSessionRef.current === localSession && conflictRef.current === comparison.fileId);
      conflictRef.current = null; setConflictFile(null); setComparison(null); setModal(null); setLocalError("");
      setLocalStatus(localSession.getView().files.some(file => file.dirty) ? "pending" : "saved");
      save();
    } catch {
      setComparison(null); setRecoveryError("编辑内容已变化，未替换任何内容。请重新读取对比。");
    }
  };
  const openWorkspace = async () => {
    if (!nativeFiles || workspaceBusy || refreshInFlight.current) return;
    ++refreshSequence.current;
    if (localSession && (conflictRef.current || saveInFlight.current || localSession.getView().files.some(file => file.dirty))) {
      setLocalError("当前项目仍有未保存修改，请保存成功后再切换项目。");
      return;
    }
    ++documentOpenSequence.current;
    setWorkspaceBusy(true); setLocalError("");
    let openedWorkspace = false;
    try {
      const openedProject = await workspaceConnection.open();
      openedWorkspace = true;
      const candidates = openedProject.entries.filter(entry => !entry.directory && /\.(tex|bib|md|txt)$/i.test(entry.fileId));
      const first = candidates.find(entry => entry.fileId.toLowerCase() === "main.tex") ?? candidates[0];
      const next = new EditorSession({ files: [], open: [], active: null });
      const nextRevisions = new Map<string, string>();
      if (first) {
        const document = await workspaceConnection.openDocument(first.fileId);
        next.load(document.fileId, document.content);
        nextRevisions.set(document.fileId, document.revision);
      }
      localSessionRef.current?.dispose();
      localSessionRef.current = next;
      revisions.current = nextRevisions;
      refreshDeferred.current = false;
      setProject(openedProject); setLocalSession(next); setLocalStatus("saved");
      setSelectedDirectory(null); setOutlineLine(null);
    } catch (failure) {
      const code = (failure as Error).message;
      if (openedWorkspace) {
        void workspaceConnection.close().catch(() => {});
        localSessionRef.current?.dispose(); localSessionRef.current = null;
        setLocalSession(null); setProject(null); setSelectedDirectory(null); revisions.current.clear();
      }
      if (code !== "USER_CANCELLED") setLocalError("打开本地项目失败：" + code);
    } finally { setWorkspaceBusy(false); }
  };
  const openLocalFile = async (path: string) => {
    if (workspaceBusy) return;
    if (!localSession) { session.activate(path); setOutlineLine(null); return; }
    if (localSession.model(path)) { localSession.activate(path); setOutlineLine(null); return; }
    const sequence = ++documentOpenSequence.current;
    try {
      const document = await workspaceConnection.openDocument(path);
      if (localSessionRef.current !== localSession || sequence !== documentOpenSequence.current) return;
      localSession.load(document.fileId, document.content);
      revisions.current.set(document.fileId, document.revision);
      setLocalStatus(localSession.getView().files.some(file => file.dirty) ? "pending" : "saved");
      setLocalError(""); setOutlineLine(null);
    } catch (failure) {
      if (localSessionRef.current !== localSession || sequence !== documentOpenSequence.current) return;
      setLocalStatus("error"); setLocalError("打开文件失败：" + (failure as Error).message);
    }
  };
  const newFile = () => {
    if (localSession) { startFileOperation('create'); return; }
    setFilename(""); setFileError(""); setModal("new");
  };
  const startFileOperation = (operation: 'create' | 'rename' | 'remove') => {
    if (!localSession || workspaceBusy || refreshInFlight.current) return;
    if (conflictRef.current || saveInFlight.current || localSession.getView().files.some(file => file.dirty)) {
      setLocalError('请先保存所有修改并处理冲突，再进行文件操作。'); return;
    }
    if (operation !== 'create' && !view.active) return;
    setFileOperation(operation); setOperationSource(view.active ?? '');
    setFilename(operation === 'rename' ? view.active ?? '' : ''); setFileError(''); setModal('manage');
  };
  const manageFile = async () => {
    if (!localSession || !project || mutationBusy.current || refreshInFlight.current) return;
    if (saveInFlight.current || conflictRef.current || localSession.getView().files.some(file => file.dirty)) {
      setFileError('仍有未保存修改，请先保存。'); return;
    }
    if (fileOperation !== 'remove' && !validDraftPath(filename)) {
      setFileError('请输入有效的 .tex、.bib、.md 或 .txt 相对路径；父文件夹必须已经存在。'); return;
    }
    const source = fileOperation === 'create' ? filename : operationSource;
    const destination = fileOperation === 'rename' ? filename : '';
    mutationBusy.current = true; setWorkspaceBusy(true); setFileError('');
    ++documentOpenSequence.current;
    try {
      const next = await workspaceConnection.manageFile(project.workspaceId, fileOperation, source, destination);
      if (localSessionRef.current !== localSession) return;
      setProject(next);
      refreshDeferred.current = false;
      if (fileOperation === 'rename') {
        localSession.rename(source, destination);
        const revision = revisions.current.get(source);
        revisions.current.delete(source); if (revision) revisions.current.set(destination, revision);
      } else if (fileOperation === 'remove') {
        if (!localSession.getView().files.find(file => file.path === source)?.dirty) localSession.forget(source);
        revisions.current.delete(source);
      }
      setModal(null); setLocalStatus(localSession.getView().files.some(file => file.dirty) ? 'pending' : 'saved');
      setLocalError(fileOperation === 'remove' ? '文件已移入项目内 .lightoverleaf-trash，可在资源管理器中移回原位置恢复。' : '');
    } catch (failure) {
      const code = (failure as Error).message;
      setFileError(code === 'FILE_CONFLICT' ? '目标文件已存在，未覆盖。请换一个名称。' : '文件操作失败：' + code);
    } finally { mutationBusy.current = false; setWorkspaceBusy(false); }
  };
  const startDirectoryOperation = () => {
    if (!localSession || workspaceBusy || refreshInFlight.current) return;
    if (conflictRef.current || saveInFlight.current || localSession.getView().files.some(file => file.dirty)) {
      setLocalError('请先保存所有修改并处理冲突，再进行目录操作。'); return;
    }
    const activeParent = view.active?.includes('/') ? view.active.slice(0, view.active.lastIndexOf('/')) : '';
    const source = selectedDirectory ?? activeParent;
    setDirectoryOperation('create'); setDirectorySource(source);
    setFilename(source ? source + '/新文件夹' : '新文件夹');
    setFileError(''); setModal('directory');
  };
  const manageDirectory = async () => {
    if (!localSession || !project || mutationBusy.current || refreshInFlight.current) return;
    if (saveInFlight.current || conflictRef.current || localSession.getView().files.some(file => file.dirty)) {
      setFileError('仍有未保存修改，请先保存。'); return;
    }
    const source = directoryOperation === 'create' ? filename : directorySource;
    const destination = directoryOperation === 'rename' ? filename : '';
    if (!validWorkspaceDirectoryId(source) ||
        (directoryOperation === 'rename' && !validWorkspaceDirectoryId(destination))) {
      setFileError('请输入安全的项目内相对目录；不能使用 ..、反斜杠、保留设备名或回收目录。'); return;
    }
    if (directoryOperation === 'rename' &&
        destination.toLowerCase().startsWith(source.toLowerCase() + '/')) {
      setFileError('不能把目录移动到它自己的子目录。'); return;
    }
    mutationBusy.current = true; setWorkspaceBusy(true); setFileError('');
    ++documentOpenSequence.current;
    try {
      const next = await workspaceConnection.manageDirectory(
        project.workspaceId, directoryOperation, source, destination);
      if (localSessionRef.current !== localSession) return;
      if (directoryOperation === 'rename') {
        const affected = localSession.getView().files
          .map(file => file.path)
          .filter(path => path.startsWith(source + '/'));
        for (const oldPath of affected) {
          const newPath = destination + oldPath.slice(source.length);
          localSession.rename(oldPath, newPath);
          const revision = revisions.current.get(oldPath);
          revisions.current.delete(oldPath);
          if (revision) revisions.current.set(newPath, revision);
        }
        setSelectedDirectory(destination);
      } else if (directoryOperation === 'remove') {
        for (const file of localSession.getView().files.filter(item => item.path.startsWith(source + '/'))) {
          localSession.forget(file.path);
          revisions.current.delete(file.path);
        }
        setSelectedDirectory(null);
      } else setSelectedDirectory(source);
      setProject(next); refreshDeferred.current = false; setModal(null);
      setLocalStatus(localSession.getView().files.some(file => file.dirty) ? 'pending' : 'saved');
      setLocalError(directoryOperation === 'remove' ?
        '目录已连同内容移入项目内 .lightoverleaf-trash，可在资源管理器中恢复。' : '');
    } catch (failure) {
      const code = (failure as Error).message;
      setFileError(code === 'FILE_CONFLICT' ? '目标目录已存在，未覆盖。请换一个名称。' :
        '目录操作失败：' + code);
    } finally { mutationBusy.current = false; setWorkspaceBusy(false); }
  };
  const explorerFiles = project ? project.entries.filter(entry => !entry.directory).map(entry => ({
    path: entry.fileId, dirty: view.files.find(file => file.path === entry.fileId)?.dirty ?? false,
  })) : view.files;
  const explorerDirectories = project ? project.entries.filter(entry => entry.directory)
    .map(entry => entry.fileId) : [];
  const statusLabel = localSession ? ({ loading: "正在读取文件", pending: "有未保存修改", saving: "正在保存文件",
    saved: "本地文件已保存", error: "本地文件操作失败" } satisfies Record<CacheStatus, string>)[activeStatus] : labels[activeStatus];
  return <main className="workbench">
    <header className="topbar">
      <a className="brand" href="#" onClick={event => { event.preventDefault(); setModal("help"); }} aria-label="关于 LightOverLeaf"><Icon name="leaf" size={23} /><span>LightOverLeaf</span></a>
      <nav className="main-menu" aria-label="主菜单">
        <button onClick={() => void openWorkspace()} disabled={!nativeFiles || workspaceBusy}>
          {workspaceBusy ? "正在打开…" : "打开项目"}</button>
        <button onClick={() => void refreshWorkspace()} disabled={!localSession || workspaceBusy}>刷新</button>
        <button onClick={newFile} disabled={workspaceBusy}>新建</button>
        {localSession && <button onClick={startDirectoryOperation} disabled={workspaceBusy}>目录</button>}
        {localSession && <><button disabled={!view.active || workspaceBusy} onClick={() => startFileOperation('rename')}>重命名</button>
          <button disabled={!view.active || workspaceBusy} onClick={() => startFileOperation('remove')}>删除</button></>}
        <button onClick={save}>{localSession ? "保存文件" : "缓存草稿"}</button>
        <button onClick={() => editor.current?.run("find")}>查找</button>
        <button onClick={() => setModal("help")}>帮助</button>
      </nav>
      <div className="project-title">{project?.displayName ?? "未命名项目"}
        <span className="draft-badge">{project ? "本地" : "草稿"}</span></div>
      <div className="topbar-right"><span className={"connection " + (native === "error" ? "error" : "")}><i />{native === "error" ? "原生通信异常" : connection.mode.startsWith("CEF") ? "本地 · 离线" : "浏览器开发模式"}</span>
        <button className="layout-button" aria-pressed={preview} onClick={() => setPreview(!preview)}><Icon name="split" />布局</button></div>
    </header>
    <div className="workspace-body">
      <aside className="activity-bar" aria-label="工作区工具">
        <button className={sidebar && !showSearch ? "active" : ""} aria-label="切换文件侧栏" title="文件" onClick={() => { setSidebar(!sidebar || showSearch); setShowSearch(false); }}><Icon name="file" size={21} /></button>
        <button className={showSearch ? "active" : ""} aria-label="筛选文件" title="筛选文件" onClick={() => { setSidebar(true); setShowSearch(!showSearch); }}><Icon name="search" size={20} /></button>
        <button aria-label="新建文件" title="新建文件" disabled={workspaceBusy} onClick={newFile}><Icon name="plus" size={20} /></button>
        <div className="rail-spacer" /><button aria-label="工作台帮助" title="帮助" onClick={() => setModal("help")}><Icon name="info" size={19} /></button>
      </aside>
      {sidebar && <><aside className="sidebar" style={{ width: sidebarWidth }} aria-label="文件与大纲">
        <div className="panel-heading"><span><span className="down-chevron">⌄</span> 文件树</span>
          <div className="heading-actions"><Tool icon="plus" label="新建文件" disabled={workspaceBusy} onClick={newFile} />
            {localSession && <Tool icon="folder" label="管理目录" disabled={workspaceBusy} onClick={startDirectoryOperation} />}
            <Tool icon="close" label="隐藏侧栏" onClick={() => setSidebar(false)} /></div></div>
        <div className="workspace-label"><span className="tiny-dot" /> {project?.displayName ?? "应用内草稿"} <span>{explorerFiles.length}</span></div>
        {showSearch && <div className="file-search"><Icon name="search" size={14} /><input ref={search} aria-label="按文件名筛选" placeholder="按文件名筛选…" value={filter} onChange={event => setFilter(event.target.value)} /></div>}
        <Explorer files={explorerFiles} directories={explorerDirectories} active={view.active}
          activeDirectory={selectedDirectory} filter={showSearch ? filter : ""}
          onSelectDirectory={setSelectedDirectory} onOpen={path => void openLocalFile(path)} />
        <section className="outline-panel" aria-label="文档大纲">
          <div className="panel-heading"><span><span className="down-chevron">⌄</span> 文档大纲</span><small>{view.outline.length}</small></div>
          <div className="outline-list">
            {view.outline.map(item => <button key={item.line} className={"outline-item " + (outlineLine === item.line ? "selected" : "")}
              style={{ paddingLeft: 14 + Math.max(0, item.level - 1) * 17 }}
              onClick={() => { setOutlineLine(item.line); editor.current?.reveal(item.line); }} title={item.title}>
              <span className="outline-marker">{item.level <= 1 ? "⌄" : "·"}</span><span className="truncate">{item.title}</span></button>)}
            {!view.outline.length && <p className="empty-small muted">当前文件没有可导航的章节。</p>}
          </div>
          <div className="sidebar-footer">章节大纲 · 当前文件</div>
        </section>
      </aside><Splitter label="调整侧栏宽度" min={190} max={380} value={sidebarWidth} onChange={setSidebarWidth} /></>}
      <section className="editor-panel" aria-label="源码编辑区域" style={preview ? { flexBasis: editorWidth, flexGrow: 0, flexShrink: 1 } : undefined}>
        <div className="editor-tabs" role="tablist" aria-label="打开的文件">
          {view.open.map(path => <div key={path} className={"editor-tab " + (path === view.active ? "active" : "")}>
            <button role="tab" aria-selected={path === view.active} onClick={() => session.activate(path)} title={path}><Icon name="file" size={14} />{path.split("/").at(-1)}{view.files.find(file => file.path === path)?.dirty && <span className="dirty-dot" />}</button>
            <button className="tab-close" aria-label={"关闭标签 " + path} title="关闭标签，不删除文件" onClick={() => session.close(path)}><Icon name="close" size={12} /></button>
          </div>)}
          <button className="tab-add" title="新建文件" aria-label="添加文件" disabled={workspaceBusy} onClick={newFile}><Icon name="plus" size={14} /></button>
        </div>
        <div className="editor-toolbar">
          <Tool icon="save" label={localSession ? "保存本地文件 (Ctrl+S)" : "立即缓存草稿 (Ctrl+S)"} onClick={save} />
          <span className="toolbar-divider" />
          <Tool icon="undo" label="撤销 (Ctrl+Z)" disabled={!view.active} onClick={() => editor.current?.run("undo")} />
          <Tool icon="redo" label="重做 (Ctrl+Y)" disabled={!view.active} onClick={() => editor.current?.run("redo")} />
          <span className="toolbar-divider" />
          <button className="text-tool" title="插入粗体命令" aria-label="插入粗体" disabled={!view.active} onClick={() => editor.current?.run("bold")}><b>B</b></button>
          <button className="text-tool" title="插入斜体命令" aria-label="插入斜体" disabled={!view.active} onClick={() => editor.current?.run("italic")}><i>I</i></button>
          <Tool icon="wrap" label="自动换行" pressed={wrap} onClick={() => setWrap(!wrap)} />
          <div className="toolbar-spacer" /><span className="code-pill"><Icon name="code" size={13} />源码</span>
          <Tool icon="search" label="查找当前文档 (Ctrl+F)" disabled={!view.active} onClick={() => editor.current?.run("find")} />
        </div>
        <div className="editor-breadcrumb"><span>{project?.displayName ?? "草稿"}</span><span> / </span><span>{view.active ?? "未打开文件"}</span></div>
        <div className="editor-content"><EditorSurface session={session} wrap={wrap} readOnly={workspaceBusy} ref={editor} onReady={() => setEditorReady(true)} />
          {!view.active && <div className="editor-empty"><Icon name="code" size={38} /><p>从左侧选择文件，继续写作。</p>{!localSession && <button onClick={newFile}>新建草稿</button>}</div>}
        </div>
        <div className={"draft-notice " + (activeStatus === "error" ? "error" : "")} role={activeError ? "alert" : undefined}><Icon name={activeStatus === "error" ? "warning" : "info"} size={13} /><span>{activeError || (localSession ? "本地保存使用 Revision 冲突保护，不会静默覆盖外部修改。" : "草稿缓存仅用于恢复，尚未保存为本地项目文件。")}</span>
          {conflictFile && <button onClick={() => void inspectConflict()}>对比版本</button>}</div>
      </section>
      {preview && <><Splitter label="调整编辑器宽度" min={300} max={Math.max(400, window.innerWidth - 400)} value={editorWidth} onChange={setEditorWidth} /><PreviewPanel /></>}
    </div>
    <footer className="statusbar"><span className="status-brand"><Icon name="leaf" size={12} /> LOCAL</span>
      <span className={activeStatus === "error" ? "error" : ""} role="status"><Icon name={activeStatus === "saved" ? "check" : activeStatus === "error" ? "warning" : "save"} size={13} />
        {statusLabel}</span>
      <span className="status-spacer" /><span>行 {view.line}，列 {view.column}</span><span>UTF-8</span><span>LaTeX</span><span className="version">开发预览</span></footer>
    {modal && <div className="modal-backdrop" onMouseDown={event => { if (event.target === event.currentTarget) setModal(null); }}>
      <section className={"modal" + (modal === "conflict" ? " conflict-modal" : "")} role="dialog" aria-modal="true" aria-labelledby="dialog-title">
        <div className="modal-title"><h2 id="dialog-title">{modal === "directory" ? "目录管理" : modal === "manage" ? ({ create: '新建本地文件', rename: '重命名文件', remove: '删除文件' })[fileOperation] : modal === "conflict" ? "处理文件冲突" : modal === "new" ? "新建草稿文件" : "你的本地写作工作台"}</h2><Tool icon="close" label="关闭对话框" onClick={() => setModal(null)} /></div>
        {modal === "directory" ? <form onSubmit={event => { event.preventDefault(); void manageDirectory(); }}>
          <label htmlFor="directory-operation">操作</label>
          <select id="directory-operation" disabled={workspaceBusy} value={directoryOperation}
            onChange={event => {
              const operation = event.target.value as 'create' | 'rename' | 'remove';
              const selected = selectedDirectory ?? directorySource;
              setDirectoryOperation(operation); setDirectorySource(selected); setFileError('');
              setFilename(operation === 'create' ? (selected ? selected + '/新文件夹' : '新文件夹') :
                operation === 'rename' ? selected : '');
            }}>
            <option value="create">新建目录</option><option value="rename">重命名或移动目录</option>
            <option value="remove">可恢复删除目录</option>
          </select>
          {directoryOperation !== 'create' && <>
            <label htmlFor="directory-source">源目录相对路径</label>
            <input id="directory-source" disabled={workspaceBusy} value={directorySource}
              onChange={event => setDirectorySource(event.target.value)} maxLength={160} />
          </>}
          {directoryOperation !== 'remove' ? <>
            <label htmlFor="directory-destination">
              {directoryOperation === 'create' ? '新目录相对路径' : '目标目录相对路径'}</label>
            <input id="directory-destination" disabled={workspaceBusy} value={filename}
              onChange={event => setFilename(event.target.value)} maxLength={160} />
            <p>父目录必须已存在；同名目标不会被覆盖。</p>
          </> : <p>目录及其全部内容将移入项目内 .lightoverleaf-trash，不会直接永久删除。</p>}
          {fileError && <p role="alert" className="error">{fileError}</p>}
          <div className="modal-actions"><button type="button" onClick={() => setModal(null)}>取消</button>
            <button type="submit" className="primary" disabled={workspaceBusy}>
              {workspaceBusy ? '处理中…' : '确认'}</button></div>
        </form> : modal === "manage" ? <form onSubmit={event => { event.preventDefault(); void manageFile(); }}>
          {fileOperation === 'remove' ? <p>将 {operationSource} 移入项目内 .lightoverleaf-trash。可在资源管理器中移回原位置恢复。</p> : <>
            <label htmlFor="local-filename">{fileOperation === 'rename' ? '新名称或相对路径' : '文件名或相对路径'}</label>
            <input id="local-filename" disabled={workspaceBusy} value={filename} onChange={event => setFilename(event.target.value)} maxLength={160} />
            <p>父文件夹必须已存在；同名文件不会被覆盖。</p></>}
          {fileError && <p role="alert" className="error">{fileError}</p>}
          <div className="modal-actions"><button type="button" onClick={() => setModal(null)}>取消</button>
            <button type="submit" className="primary" disabled={workspaceBusy}>{workspaceBusy ? '处理中…' : '确认'}</button></div>
        </form> : modal === "conflict" ? <>
          <p>{conflictFile}</p>
          <p>关闭窗口会保留当前编辑。采用磁盘版本将替换编辑区内容，可通过撤销找回；不会写入磁盘。</p>
          {recoveryBusy && <p role="status">正在读取磁盘版本…</p>}
          {recoveryError && <p className="error" role="alert">{recoveryError}</p>}
          {comparison && <div className="conflict-columns">
            <label>当前编辑<textarea readOnly aria-label="当前编辑版本" value={comparison.localContent.slice(0, 32768)} /></label>
            <label>磁盘版本<textarea readOnly aria-label="磁盘版本" value={comparison.diskContent.slice(0, 32768)} /></label>
          </div>}
          {comparison && Math.max(comparison.localContent.length, comparison.diskContent.length) > 32768 &&
            <p>对比区仅显示前 32768 个字符；采用磁盘版本时使用完整内容。</p>}
          <div className="modal-actions"><button onClick={() => setModal(null)}>保留当前编辑</button>
            <button disabled={recoveryBusy} onClick={() => void inspectConflict()}>重新读取对比</button>
            <button className="primary" disabled={!comparison || recoveryBusy} onClick={acceptDisk}>采用磁盘版本</button></div>
        </> : modal === "new" ? <form onSubmit={event => {
          event.preventDefault();
          if (!validDraftPath(filename)) { setFileError("请输入有效相对路径，支持 .tex、.bib、.md、.txt，不能包含 .. 或反斜杠。"); return; }
          try { session.create(filename); setModal(null); } catch (failure) { setFileError((failure as Error).message); }
        }}>
          <label htmlFor="draft-filename">文件名或相对路径</label>
          <input id="draft-filename" autoFocus value={filename} onChange={event => setFilename(event.target.value)} placeholder="例如 sections/introduction.tex" maxLength={160} />
          <p className="muted">创建在应用草稿区，不会创建或覆盖磁盘上的文件。</p>
          {fileError && <p className="error" role="alert">{fileError}</p>}
          <div className="modal-actions"><button type="button" onClick={() => setModal(null)}>取消</button><button className="primary" type="submit">创建草稿</button></div>
        </form> : <>
          <p>已支持源码编辑、多标签、文件筛选、章节跳转和自动草稿缓存。</p>
          <div className="shortcut"><span>立即缓存草稿</span><kbd>Ctrl + S</kbd></div>
          <div className="shortcut"><span>查找当前文档</span><kbd>Ctrl + F</kbd></div>
          <div className="shortcut"><span>撤销 / 重做</span><kbd>Ctrl + Z / Y</kbd></div>
          <p className="muted">缓存只属于当前应用配置。清除应用缓存会丢失草稿；异常退出可能丢失尚未缓存的最后输入。关闭标签不会删除文件。</p>
          <p className="muted">已支持本地项目、文件管理和保存冲突处理。打开项目每 5 秒自动刷新一次，也可手动刷新；磁盘变化不会覆盖并发编辑。删除文件保存在项目内 .lightoverleaf-trash。目录管理、TeX 编译、PDF 预览和 SyncTeX 仍待完成。</p>
          <div className="modal-actions"><button className="primary" onClick={() => setModal(null)}>开始写作</button></div>
        </>}
      </section>
    </div>}
  </main>;
}
function App() {
  const workspace = useDraftWorkspace();
  if (!workspace.session) return <main className="startup"><Icon name="leaf" size={40} /><h1>LightOverLeaf</h1>
    <p role="status">{workspace.error || "正在恢复草稿工作台…"}</p>
    {workspace.error && <button onClick={() => location.reload()}>重试读取缓存</button>}</main>;
  return <Workbench {...workspace} session={workspace.session} />;
}
const root = document.getElementById("root");
if (!root) throw new Error("Missing application root");
createRoot(root).render(<App />);
