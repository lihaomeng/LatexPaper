import { createRoot } from "react-dom/client";
import { useCallback, useEffect, useRef, useState, useSyncExternalStore } from "react";
import { AuthoringRpcClient, createNativeConnection, createSystemConnection, createStartupEvents,
  PreferencesSessionRpcClient, WorkspaceRpcClient, type Preferences } from "../native-api";
import type { SessionSnapshot } from "../native-api";
import type { WorkspaceStateResponseResult, WorkspaceTrashListResponseResultEntriesItem } from "../../.generated/rpc/protocol";
import { EditorSession, EditorSurface, type EditorCommands } from "../features/editor";
import { Explorer } from "../features/explorer";
import { PreviewPanel, type BuildView } from "../features/preview";
import { validDraftPath } from "../features/session";
import { Icon, type IconName } from "../shared/Icon";
import { Splitter } from "../shared/Splitter";
import { useDraftWorkspace, type CacheStatus } from "./useDraftWorkspace";
import { LocalDocumentSaveError, saveLocalDocuments } from "./saveLocalDocuments";
import { adoptDiskVersion, compareDocument, type DocumentComparison } from "./documentRecovery";
import { saveConflictCopy } from "./saveConflictCopy";
import { reconcileWorkspaceRefresh } from "./refreshLocalWorkspace";
import { containsMergeMarkers, mergeDocumentText } from "./threeWayMerge";
import { validWorkspaceDirectoryId } from "./workspacePaths";
import "./style.css";

const connection = createNativeConnection(import.meta.env.DEV);
const systemConnection = createSystemConnection(import.meta.env.DEV);
const workspaceConnection = new WorkspaceRpcClient(systemConnection);
const authoringConnection = new AuthoringRpcClient(createSystemConnection(import.meta.env.DEV, 305000));
const preferencesSessionConnection = new PreferencesSessionRpcClient(createSystemConnection(import.meta.env.DEV));
const defaultPreferences: Preferences = { texRoot: "", engine: "xelatex", timeoutMs: 120000, autoCompile: false };
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
  const [conflictCopyPath, setConflictCopyPath] = useState("");
  const [mergeDraft, setMergeDraft] = useState("");
  const [mergeConflicts, setMergeConflicts] = useState(0);
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
  const [trashEntries, setTrashEntries] = useState<WorkspaceTrashListResponseResultEntriesItem[]>([]);
  const [projectQuery, setProjectQuery] = useState("");
  const [searchHits, setSearchHits] = useState<{ fileId: string; line: number; column: number; preview: string }[]>([]);
  const [searchBusy, setSearchBusy] = useState(false);
  const [searchError, setSearchError] = useState("");
  const [buildView, setBuildView] = useState<BuildView>({ state: "idle", output: "", diagnostics: [] });
  const [preferences, setPreferences] = useState<Preferences>(defaultPreferences);
  const [preferencesDraft, setPreferencesDraft] = useState<Preferences>(defaultPreferences);
  const [recentWorkspaces, setRecentWorkspaces] = useState<string[]>([]);
  const [settingsBusy, setSettingsBusy] = useState(false);
  const [settingsError, setSettingsError] = useState("");
  const sessionRestored = useRef(false);
  const restoredSession = useRef<SessionSnapshot | null>(null);
  const activeBuildJob = useRef<string | null>(null);
  const buildSequence = useRef(0);
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
  const [modal, setModal] = useState<"new" | "help" | "settings" | "conflict" | "manage" | "directory" | "trash" | null>(null);
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
    if (!localSession || !project) return;
    let polling = false;
    const timer = window.setInterval(() => {
      if (polling || document.visibilityState !== "visible") return;
      polling = true;
      void workspaceConnection.pollChanges(project.workspaceId)
        .then(changed => { if (changed) void refreshWorkspace(); })
        .catch(() => { refreshDeferred.current = true; })
        .finally(() => { polling = false; });
    }, 1000);
    return () => window.clearInterval(timer);
  }, [localSession, project, refreshWorkspace]);
  useEffect(() => {
    if (modal !== "conflict") return;
    return () => { ++recoverySequence.current; };
  }, [modal]);
  useEffect(() => () => {
    ++documentOpenSequence.current;
    ++recoverySequence.current;
    ++refreshSequence.current;
    ++buildSequence.current;
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
    if (native !== "ready" || sessionRestored.current) return;
    sessionRestored.current = true;
    let active = true;
    void Promise.all([
      preferencesSessionConnection.getPreferences(),
      preferencesSessionConnection.restoreSession(),
      preferencesSessionConnection.history(),
    ]).then(([savedPreferences, savedSession, history]) => {
      if (!active) return;
      setPreferences(savedPreferences); setPreferencesDraft(savedPreferences);
      setRecentWorkspaces(history);
      if (savedSession.found) {
        restoredSession.current = savedSession.state;
        setSidebarWidth(savedSession.state.sidebarWidth);
        setPreview(savedSession.state.previewOpen);
      }
    }).catch(failure => {
      if (active) setSettingsError("读取设置或会话失败：" + (failure as Error).message);
    });
    return () => { active = false; };
  }, [native]);
  useEffect(() => {
    if (native !== "ready" || !sessionRestored.current) return;
    const timer = window.setTimeout(() => {
      void preferencesSessionConnection.saveSession({
        workspaceRoot: project?.workspaceId ?? "",
        openFiles: localSession ? view.open : [],
        activeFile: localSession ? view.active ?? "" : "",
        sidebarWidth,
        previewOpen: preview,
      }).then(() => {
        if (project?.workspaceId)
          setRecentWorkspaces(current => [project.workspaceId, ...current.filter(item => item !== project.workspaceId)].slice(0, 20));
      }).catch(failure => setSettingsError("保存会话失败：" + (failure as Error).message));
    }, 500);
    return () => window.clearTimeout(timer);
  }, [native, project?.workspaceId, localSession, view.open, view.active, sidebarWidth, preview]);
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
      if (isCurrent()) {
        setComparison(result);
        const merged = mergeDocumentText(result.baseContent, result.localContent, result.diskContent);
        setMergeDraft(merged.content); setMergeConflicts(merged.conflicts);
        const extension = result.fileId.lastIndexOf('.');
        setConflictCopyPath(extension > result.fileId.lastIndexOf('/') ?
          result.fileId.slice(0, extension) + '-副本' + result.fileId.slice(extension) :
          result.fileId + '-副本.tex');
      }
    } catch (failure) {
      if (isCurrent()) setRecoveryError((failure as Error).message === "STALE_DOCUMENT"
        ? "读取期间编辑内容已变化，请重新读取对比。" : "读取磁盘版本失败：" + (failure as Error).message);
    } finally { if (localSessionRef.current === localSession) setRecoveryBusy(false); }
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
  const applyMerge = () => {
    if (!localSession || !comparison || recoveryBusy) return;
    if (containsMergeMarkers(mergeDraft)) {
      setRecoveryError('合并内容仍包含冲突标记，请处理全部标记后再应用。'); return;
    }
    if (!localSession.replaceForMerge(comparison.fileId, mergeDraft, comparison.localVersion)) {
      setComparison(null); setRecoveryError('编辑内容已变化，未应用合并结果。请重新读取对比。'); return;
    }
    revisions.current.set(comparison.fileId, comparison.diskRevision);
    conflictRef.current = null; setConflictFile(null); setComparison(null); setModal(null);
    setLocalError('三方合并已应用到编辑器，正在保存到最新磁盘版本。'); setLocalStatus('pending');
    window.setTimeout(save, 0);
  };
  const saveConflictAs = async () => {
    if (!localSession || !comparison || recoveryBusy || mutationBusy.current) return;
    if (!validDraftPath(conflictCopyPath) ||
        conflictCopyPath.toLowerCase() === comparison.fileId.toLowerCase()) {
      setRecoveryError('请输入不同于原文件的有效 .tex、.bib、.md 或 .txt 相对路径。'); return;
    }
    const sequence = ++recoverySequence.current;
    const isCurrent = () => localSessionRef.current === localSession &&
      conflictRef.current === comparison.fileId && sequence === recoverySequence.current;
    mutationBusy.current = true; setRecoveryBusy(true); setRecoveryError('');
    let committed = false;
    try {
      const result = await saveConflictCopy(
        localSession, comparison, conflictCopyPath, workspaceConnection, isCurrent);
      committed = true;
      if (result.canRetireSource && isCurrent()) {
        if (localSession.renameSaved(result.sourceFileId, result.destinationFileId,
          result.sourceVersion)) {
          revisions.current.set(result.destinationFileId, result.revision);
          revisions.current.delete(result.sourceFileId);
          conflictRef.current = null; setConflictFile(null); setComparison(null); setModal(null);
          setLocalStatus('saved'); setLocalError('编辑内容已原子另存为 ' + result.destinationFileId +
            '；原文件的外部版本保持不变。');
        } else {
          setRecoveryError('副本已保存，但编辑模型已变化；原编辑保持不变。请从文件树打开副本。');
        }
      } else if (localSessionRef.current === localSession) {
        setRecoveryError('副本已保存，但原编辑或会话已变化；原编辑保持不变。请从文件树打开副本。');
      }
      try {
        const next = await workspaceConnection.refresh();
        if (localSessionRef.current === localSession) {
          setProject(next);
          refreshDeferred.current = true;
        }
      } catch (failure) {
        if (localSessionRef.current === localSession)
          setLocalError('副本已保存，但刷新文件树失败：' + (failure as Error).message);
      }
    } catch (failure) {
      if (isCurrent()) {
        const code = (failure as Error).message;
        setRecoveryError(code === 'FILE_CONFLICT' ? '目标文件已经存在，未覆盖任何内容。' :
          code === 'STALE_DOCUMENT' ? '编辑内容已变化，请重新读取对比后再另存。' :
          code === 'UNCONFIRMED_COMMIT' ? '原生返回与目标不一致，提交状态无法确认；请刷新文件树检查。' :
          '另存副本失败：' + code);
      } else if (committed && localSessionRef.current === localSession) {
        setLocalError('副本已提交，但当前会话已经变化；请刷新文件树确认。');
      }
    } finally {
      mutationBusy.current = false;
      if (localSessionRef.current === localSession) setRecoveryBusy(false);
    }
  };
  const openTrash = async () => {
    if (!project || workspaceBusy || mutationBusy.current) return;
    setWorkspaceBusy(true); setFileError('');
    try {
      const result = await workspaceConnection.listTrash(project.workspaceId);
      setTrashEntries(result.entries); setModal('trash');
    } catch (failure) {
      setLocalError('读取回收站失败：' + (failure as Error).message);
    } finally { setWorkspaceBusy(false); }
  };
  const restoreTrash = async (trashId: string) => {
    if (!project || workspaceBusy || mutationBusy.current || saveInFlight.current ||
        localSession?.getView().files.some(file => file.dirty)) {
      setFileError('请先保存当前修改，再恢复回收内容。'); return;
    }
    mutationBusy.current = true; setWorkspaceBusy(true); setFileError('');
    try {
      const next = await workspaceConnection.restoreTrash(project.workspaceId, trashId);
      setProject(next);
      const listed = await workspaceConnection.listTrash(project.workspaceId);
      setTrashEntries(listed.entries);
      setLocalError('回收内容已恢复到原路径；同名目标始终不会被覆盖。');
    } catch (failure) {
      const code = (failure as Error).message;
      setFileError(code === 'FILE_CONFLICT' ? '原路径已有同名内容，未覆盖。请先重命名现有条目。' :
        '恢复失败：' + code);
    } finally { mutationBusy.current = false; setWorkspaceBusy(false); }
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
      const previous = restoredSession.current?.workspaceRoot === openedProject.workspaceId ? restoredSession.current : null;
      const available = new Set(candidates.map(entry => entry.fileId));
      const requested = previous?.openFiles.filter(file => available.has(file)) ?? [];
      const filesToOpen = requested.length ? requested : first ? [first.fileId] : [];
      for (const fileId of filesToOpen) {
        const document = await workspaceConnection.openDocument(fileId);
        next.load(document.fileId, document.content);
        nextRevisions.set(document.fileId, document.revision);
      }
      if (previous?.activeFile && next.model(previous.activeFile)) next.activate(previous.activeFile);
      restoredSession.current = null;
      localSessionRef.current?.dispose();
      localSessionRef.current = next;
      revisions.current = nextRevisions;
      refreshDeferred.current = false;
      setProject(openedProject); setLocalSession(next); setLocalStatus("saved");
      setSearchHits([]); setProjectQuery(""); setSearchError("");
      setBuildView({ state: "idle", output: "", diagnostics: [] });
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
  const runProjectSearch = async () => {
    if (!localSession || !project || searchBusy || !projectQuery.trim()) return;
    setSearchBusy(true); setSearchError("");
    try {
      const result = await authoringConnection.search(projectQuery, false, 200);
      if (localSessionRef.current !== localSession) return;
      setSearchHits(result.hits);
      if (result.truncated) setSearchError("结果已达到 200 条上限，请缩小查询范围。");
    } catch (failure) {
      if (localSessionRef.current === localSession) setSearchError("项目搜索失败：" + (failure as Error).message);
    } finally { if (localSessionRef.current === localSession) setSearchBusy(false); }
  };
  const openSearchHit = async (fileId: string, line: number) => {
    await openLocalFile(fileId);
    if (localSessionRef.current === localSession) {
      setOutlineLine(line);
      editor.current?.reveal(line);
    }
  };
  const compileProject = async () => {
    if (!localSession || !project || activeBuildJob.current || conflictRef.current || refreshInFlight.current) return;
    if (saveInFlight.current) { setLocalError("文件仍在保存，请稍后重新编译。"); return; }
    const mainFileId = project.entries.find(item => !item.directory && item.fileId.toLowerCase() === "main.tex")?.fileId ??
      (view.active?.toLowerCase().endsWith(".tex") ? view.active : undefined);
    if (!mainFileId) { setBuildView({ state: "failed", output: "", diagnostics: [] }); setLocalError("未找到 main.tex 或当前 .tex 主文件。"); return; }
    const sequence = ++buildSequence.current;
    try {
      if (localSession.getView().files.some(file => file.dirty)) {
        saveInFlight.current = true; setLocalStatus("saving");
        await saveLocalDocuments(localSession, revisions.current, workspaceConnection,
          () => localSessionRef.current === localSession && sequence === buildSequence.current);
        if (localSessionRef.current !== localSession || sequence !== buildSequence.current) return;
        setLocalStatus("saved");
      }
      setBuildView({ state: "detecting", output: "", diagnostics: [] });
      const detected = await authoringConnection.detect();
      if (localSessionRef.current !== localSession || sequence !== buildSequence.current) return;
      const engine = detected.toolchains.flatMap(item => item.engines)
        .find(item => item === preferences.engine) ?? detected.toolchains[0]?.engines[0];
      if (!engine) { setBuildView({ state: "unavailable", output: "未检测到 TeX。可配置 LIGHTOVERLEAF_TEX_ROOT 或安装系统 TeX。", diagnostics: [] }); return; }
      const token = crypto.randomUUID();
      const jobId = "job-" + token;
      activeBuildJob.current = jobId;
      setBuildView({ state: "running", output: "", diagnostics: [] });
      const result = await authoringConnection.build(jobId, "snapshot-" + token, mainFileId, engine, preferences.timeoutMs);
      if (localSessionRef.current !== localSession || sequence !== buildSequence.current) return;
      const state: BuildView["state"] = result.terminal === "compilerUnavailable" ? "unavailable" : result.terminal;
      const pdf = result.artifactId ? await authoringConnection.readPdf(result.artifactId) : undefined;
      setBuildView({ state, output: result.output + (result.outputTruncated ? "\n[日志已截断]" : ""),
        diagnostics: result.diagnostics.map(item => ({ fileId: item.fileId, line: item.line, message: item.message })),
        artifactId: result.artifactId || undefined, pdf, syncTexAvailable: result.syncTexAvailable });
    } catch (failure) {
      if (localSessionRef.current === localSession && sequence === buildSequence.current)
        setBuildView({ state: (failure as Error).message === "RPC_CANCELLED" ? "cancelled" : "failed",
          output: (failure as Error).message, diagnostics: [] });
    } finally {
      saveInFlight.current = false;
      if (sequence === buildSequence.current) activeBuildJob.current = null;
    }
  };
  const cancelBuild = async () => {
    const jobId = activeBuildJob.current;
    if (!jobId) return;
    try { await authoringConnection.cancel(jobId); }
    catch (failure) { setLocalError("取消编译失败：" + (failure as Error).message); }
  };
  const openSettings = () => {
    setPreferencesDraft(preferences); setSettingsError(""); setModal("settings");
    void preferencesSessionConnection.history().then(setRecentWorkspaces)
      .catch(failure => setSettingsError("读取最近项目失败：" + (failure as Error).message));
  };
  const savePreferences = async () => {
    if (settingsBusy) return;
    setSettingsBusy(true); setSettingsError("");
    try {
      const saved = await preferencesSessionConnection.updatePreferences(preferencesDraft);
      setPreferences(saved); setPreferencesDraft(saved); setModal(null);
      if (saved.texRoot !== preferences.texRoot)
        setLocalError("TeX 根目录已保存；重启应用后重新发现工具链。引擎和超时已立即生效。");
    } catch (failure) {
      setSettingsError("保存设置失败：" + (failure as Error).message);
    } finally { setSettingsBusy(false); }
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
        {localSession && <button onClick={() => void openTrash()} disabled={workspaceBusy}>回收站</button>}
        {localSession && <><button disabled={!view.active || workspaceBusy} onClick={() => startFileOperation('rename')}>重命名</button>
          <button disabled={!view.active || workspaceBusy} onClick={() => startFileOperation('remove')}>删除</button></>}
        <button onClick={save}>{localSession ? "保存文件" : "缓存草稿"}</button>
        <button onClick={() => editor.current?.run("find")}>查找</button>
        <button onClick={openSettings}>设置</button>
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
        <button className={showSearch ? "active" : ""} aria-label="搜索项目" title="搜索项目内容" onClick={() => { setSidebar(true); setShowSearch(!showSearch); }}><Icon name="search" size={20} /></button>
        <button aria-label="新建文件" title="新建文件" disabled={workspaceBusy} onClick={newFile}><Icon name="plus" size={20} /></button>
        <div className="rail-spacer" /><button aria-label="设置" title="设置" onClick={openSettings}><Icon name="settings" size={19} /></button>
        <button aria-label="工作台帮助" title="帮助" onClick={() => setModal("help")}><Icon name="info" size={19} /></button>
      </aside>
      {sidebar && <><aside className="sidebar" style={{ width: sidebarWidth }} aria-label="文件与大纲">
        <div className="panel-heading"><span><span className="down-chevron">⌄</span> 文件树</span>
          <div className="heading-actions"><Tool icon="plus" label="新建文件" disabled={workspaceBusy} onClick={newFile} />
            {localSession && <Tool icon="folder" label="管理目录" disabled={workspaceBusy} onClick={startDirectoryOperation} />}
            <Tool icon="close" label="隐藏侧栏" onClick={() => setSidebar(false)} /></div></div>
        <div className="workspace-label"><span className="tiny-dot" /> {project?.displayName ?? "应用内草稿"} <span>{explorerFiles.length}</span></div>
        {showSearch && (localSession ? <><div className="file-search"><Icon name="search" size={14} />
          <input ref={search} aria-label="搜索项目内容" placeholder="搜索项目内容…" value={projectQuery}
            onChange={event => setProjectQuery(event.target.value)}
            onKeyDown={event => { if (event.key === "Enter") void runProjectSearch(); }} />
          <button disabled={searchBusy || !projectQuery.trim()} onClick={() => void runProjectSearch()}>{searchBusy ? "…" : "搜"}</button></div>
          {searchError && <p className="search-error">{searchError}</p>}
          <div className="search-results" aria-label="项目搜索结果">{searchHits.map((hit, index) =>
            <button key={`${hit.fileId}:${hit.line}:${hit.column}:${index}`} onClick={() => void openSearchHit(hit.fileId, hit.line)}>
              <strong>{hit.fileId}:{hit.line}:{hit.column}</strong><span>{hit.preview}</span></button>)}</div></> :
          <div className="file-search"><Icon name="search" size={14} /><input ref={search} aria-label="按文件名筛选" placeholder="按文件名筛选…" value={filter} onChange={event => setFilter(event.target.value)} /></div>)}
        <Explorer files={explorerFiles} directories={explorerDirectories} active={view.active}
          activeDirectory={selectedDirectory} filter={showSearch && !localSession ? filter : ""}
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
      {preview && <><Splitter label="调整编辑器宽度" min={300} max={Math.max(400, window.innerWidth - 400)} value={editorWidth} onChange={setEditorWidth} />
        <PreviewPanel build={buildView} canCompile={Boolean(localSession && project && view.active)}
          onCompile={() => void compileProject()} onCancel={() => void cancelBuild()}
          onReverse={(page, x, y) => { if (buildView.artifactId) void authoringConnection.reverse(buildView.artifactId, page, x, y)
            .then(location => openSearchHit(location.fileId, location.line))
            .catch(failure => setLocalError("SyncTeX 反向定位失败：" + (failure as Error).message)); }}
          onDiagnostic={(fileId, line) => void openSearchHit(fileId, line)} /></>}
    </div>
    <footer className="statusbar"><span className="status-brand"><Icon name="leaf" size={12} /> LOCAL</span>
      <span className={activeStatus === "error" ? "error" : ""} role="status"><Icon name={activeStatus === "saved" ? "check" : activeStatus === "error" ? "warning" : "save"} size={13} />
        {statusLabel}</span>
      <span className="status-spacer" /><span>行 {view.line}，列 {view.column}</span><span>UTF-8</span><span>LaTeX</span><span className="version">开发预览</span></footer>
    {modal && <div className="modal-backdrop" onMouseDown={event => { if (event.target === event.currentTarget) setModal(null); }}>
      <section className={"modal" + (modal === "conflict" ? " conflict-modal" : "")} role="dialog" aria-modal="true" aria-labelledby="dialog-title">
        <div className="modal-title"><h2 id="dialog-title">{modal === "settings" ? "本地设置与会话" : modal === "trash" ? "项目回收站" : modal === "directory" ? "目录管理" : modal === "manage" ? ({ create: '新建本地文件', rename: '重命名文件', remove: '删除文件' })[fileOperation] : modal === "conflict" ? "处理文件冲突" : modal === "new" ? "新建草稿文件" : "你的本地写作工作台"}</h2><Tool icon="close" label="关闭对话框" onClick={() => setModal(null)} /></div>
        {modal === "settings" ? <form onSubmit={event => { event.preventDefault(); void savePreferences(); }}>
          <label htmlFor="tex-root">TeX 根目录（可留空使用系统或 Lite 外部工具链）</label>
          <input id="tex-root" value={preferencesDraft.texRoot} maxLength={4096} disabled={settingsBusy}
            onChange={event => setPreferencesDraft(current => ({ ...current, texRoot: event.target.value }))}
            placeholder="例如 D:\\texlive\\2026" />
          <label htmlFor="tex-engine">首选编译引擎</label>
          <select id="tex-engine" value={preferencesDraft.engine} disabled={settingsBusy}
            onChange={event => setPreferencesDraft(current => ({ ...current,
              engine: event.target.value as Preferences['engine'] }))}>
            <option value="xelatex">XeLaTeX</option><option value="pdflatex">pdfLaTeX</option>
            <option value="lualatex">LuaLaTeX</option>
          </select>
          <label htmlFor="tex-timeout">编译超时（毫秒）</label>
          <input id="tex-timeout" type="number" min={1000} max={300000} value={preferencesDraft.timeoutMs}
            disabled={settingsBusy} onChange={event => setPreferencesDraft(current => ({ ...current,
              timeoutMs: Number(event.target.value) }))} />
          <label><input type="checkbox" checked={preferencesDraft.autoCompile} disabled={settingsBusy}
            onChange={event => setPreferencesDraft(current => ({ ...current, autoCompile: event.target.checked }))} /> 自动编译（保留偏好；当前版本仍由用户手动触发）</label>
          <p>布局、打开标签与最近项目由本机 SQLite 保存。重新选择同一项目后恢复标签；不会把绝对 PDF 路径交给前端。</p>
          <div className="recent-workspaces"><b>最近项目</b>
            {recentWorkspaces.map(root => <code key={root}>{root}</code>)}
            {!recentWorkspaces.length && <span className="muted">暂无记录</span>}
          </div>
          {settingsError && <p className="error" role="alert">{settingsError}</p>}
          <div className="modal-actions"><button type="button" onClick={() => setModal(null)}>取消</button>
            <button className="primary" type="submit" disabled={settingsBusy}>{settingsBusy ? "保存中…" : "保存设置"}</button></div>
        </form> : modal === "trash" ? <>
          <p>删除内容保留在项目内，仅能恢复到记录的原路径；不会覆盖同名文件或目录。</p>
          <div className="trash-list">
            {trashEntries.map(entry => <div key={entry.trashId} className="trash-entry">
              <span><b>{entry.directory ? '目录' : '文件'}</b> {entry.originalFileId}<small>{new Date(entry.deletedAtUnixMs).toLocaleString()}</small></span>
              <button disabled={workspaceBusy} onClick={() => void restoreTrash(entry.trashId)}>恢复</button>
            </div>)}
            {!trashEntries.length && <p className="muted">没有带恢复索引的删除内容。</p>}
          </div>
          {fileError && <p role="alert" className="error">{fileError}</p>}
          <div className="modal-actions"><button onClick={() => setModal(null)}>关闭</button></div>
        </> : modal === "directory" ? <form onSubmit={event => { event.preventDefault(); void manageDirectory(); }}>
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
          {comparison && <label className="merge-result">三方合并结果（{mergeConflicts ? `${mergeConflicts} 处待处理` : '可直接应用'}）
            <textarea aria-label="三方合并结果" value={mergeDraft}
              onChange={event => { setMergeDraft(event.target.value); setRecoveryError(''); }} />
          </label>}
          {comparison && Math.max(comparison.localContent.length, comparison.diskContent.length) > 32768 &&
            <p>对比区仅显示前 32768 个字符；采用磁盘版本时使用完整内容。</p>}
          {comparison && <><label htmlFor="conflict-copy-path">将当前编辑另存为</label>
            <input id="conflict-copy-path" value={conflictCopyPath} disabled={recoveryBusy}
              onChange={event => setConflictCopyPath(event.target.value)} maxLength={160} />
            <p>另存采用原子非覆盖创建；目标已存在时不会写入。</p></>}
          <div className="modal-actions"><button onClick={() => setModal(null)}>保留当前编辑</button>
            <button disabled={recoveryBusy} onClick={() => void inspectConflict()}>重新读取对比</button>
            <button disabled={!comparison || recoveryBusy} onClick={() => void saveConflictAs()}>另存副本</button>
            <button disabled={!comparison || recoveryBusy} onClick={applyMerge}>应用合并</button>
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
          <p className="muted">已支持本地项目、文件/目录管理、冲突恢复、搜索、可插拔 TeX 编译、PDF.js 预览和本机会话设置。磁盘变化不会覆盖并发编辑，删除内容保存在项目内 .lightoverleaf-trash。真实 SyncTeX 仍需额外 Backend；未检测到 TeX 时不会模拟编译成功。</p>
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
