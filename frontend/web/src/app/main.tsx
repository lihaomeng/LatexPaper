import { Dialog } from "../shared/ui/Dialog";
import { previewPresentation } from "../features/preview";
import { useLocalWorkspace } from "./workflows/useLocalWorkspace";
import { useDocumentPersistence } from "./workflows/useDocumentPersistence";
import { useConflictRecovery } from "./workflows/useConflictRecovery";
import { ConflictForm } from "./layout/ConflictForm";
import { useProjectSearch } from "./useProjectSearch";
import { useWorkbenchSettings } from "./useWorkbenchSettings";
import { SettingsForm } from "./layout/SettingsForm";
import { createRoot } from "react-dom/client";
import { useEffect, useRef, useState, useSyncExternalStore } from "react";
import { createStartupEvents, payloadSmokeProbe } from "../native-api";
import type { SessionSnapshot } from "../native-api";
import type { WorkspaceTrashListResponseResultEntriesItem } from "../../.generated/rpc/protocol";
import { EditorSession, EditorSurface, type EditorCommands } from "../features/editor";
import { Explorer } from "../features/explorer";
import { PreviewPanel, BuildLogPanel, useBuildFeedback, type BuildView, type PdfTarget } from "../features/preview";
import { validDraftPath } from "../features/session";
import { Icon, type IconName } from "../shared/Icon";
import { Splitter } from "../shared/Splitter";
import { ActionMenu } from "../shared/ActionMenu";
import { WorkbenchHeader } from "./layout/WorkbenchHeader";
import { OutlinePanel } from "./layout/OutlinePanel";
import { useWorkbenchLayout } from "./layout/useWorkbenchLayout";
import { useDraftWorkspace, type CacheStatus } from "./useDraftWorkspace";
import { validWorkspaceDirectoryId } from "./workspacePaths";
import "../shared/styles/tokens.css";
import "./style.css";
import "./layout/workbench.css";
import { CompileController } from "./compileController";
import { useWorkspaceSwitch } from "./workflows/useWorkspaceSwitch";
import { createNativeServices } from "./nativeServices";

const { connection, systemConnection, workspaceConnection, workspaceOpenConnection,
  authoringConnection, exportConnection, preferencesSessionConnection } = createNativeServices(import.meta.env.DEV);
const labels: Record<CacheStatus, string> = { loading: "正在读取草稿", pending: "有待缓存修改", saving: "正在缓存草稿", saved: "草稿已缓存", error: "草稿缓存失败" };
function Tool({ icon, label, onClick, pressed, disabled }: { icon: IconName; label: string; onClick(): void; pressed?: boolean; disabled?: boolean }) {
  return <button className="tool-button" title={label} aria-label={label} aria-pressed={pressed} onClick={onClick} disabled={disabled}><Icon name={icon} /></button>;
}
function Workbench({ session: draftSession, status, error, save: saveDraft, draftId }: {
  session: EditorSession; status: CacheStatus; error: string; save(): void; draftId: string;
}) {
  const workspace = useLocalWorkspace();
  const { localSession, localSessionRef, project, setProject, revisions, localStatus, setLocalStatus, localError,
    setLocalError, workspaceBusy, setWorkspaceBusy, documentOpenSequence, saveInFlight, conflictRef, conflictFile,
    refreshInFlight, refreshDeferred, mutationBusy, switchingWorkspace, selectedDirectory, setSelectedDirectory } =
    workspace;
  const [nativeFiles, setNativeFiles] = useState(false);
  const [nativeBuild, setNativeBuild] = useState(false);
  const [fileOperation, setFileOperation] = useState<'create' | 'rename' | 'remove'>('create');
  const [operationSource, setOperationSource] = useState('');
  const [directoryOperation, setDirectoryOperation] = useState<'create' | 'rename' | 'remove'>('create');
  const [directorySource, setDirectorySource] = useState('');
  const [trashEntries, setTrashEntries] = useState<WorkspaceTrashListResponseResultEntriesItem[]>([]);
  const { query: projectQuery, setQuery: setProjectQuery, hits: searchHits,
    busy: searchBusy, error: searchError, run: runProjectSearch, reset: resetProjectSearch } =
    useProjectSearch(authoringConnection, localSession);
  const [buildView, setBuildView] = useState<BuildView>({ state: "idle", output: "", diagnostics: [] });
  const [compileController] = useState(() => new CompileController(authoringConnection, setBuildView));
  const settings = useWorkbenchSettings(preferencesSessionConnection);
  const { preferences, draft: preferencesDraft, setDraft: setPreferencesDraft,
    busy: settingsBusy, error: settingsError, setError: setSettingsError } = settings;
  const [recentWorkspaces, setRecentWorkspaces] = useState<string[]>([]);
  const [sessionReady, setSessionReady] = useState(false);
  const [projectNames, setProjectNames] = useState<Record<string, string>>(() => {
    try {
      const parsed: unknown = JSON.parse(localStorage.getItem('lightoverleaf.projectNames') ?? '{}');
      if (!parsed || typeof parsed !== 'object' || Array.isArray(parsed)) return {};
      return Object.fromEntries(Object.entries(parsed).filter(([id, name]) =>
        /^workspace-[a-zA-Z0-9-]+$/.test(id) && typeof name === 'string').slice(0, 100));
    } catch { return {}; }
  });
  const restoredLayout = useRef({ sidebarWidth: 260, editorWidth: 600, previewOpen: true, previewZoom: 100 });
  const restoredSession = useRef<SessionSnapshot | null>(null);


  const lastAutoCompileRevision = useRef(0);
  const lastSaveRequestedRevision = useRef(0);
  const [pdfTarget, setPdfTarget] = useState<PdfTarget | null>(null);
  const session = localSession ?? draftSession;
  const view = useSyncExternalStore(session.subscribe, session.getView);
  const editor = useRef<EditorCommands>(null);
  const [native, setNative] = useState<"pending" | "ready" | "error">("pending");
  const [editorReady, setEditorReady] = useState(false);
  const layout = useWorkbenchLayout();
  const { sidebar, setSidebar, preview, setPreview, sidebarWidth, setSidebarWidth,
    editorWidth, setEditorWidth, previewZoom, setPreviewZoom } = layout;
  const [wrap, setWrap] = useState(true);
  const [logsOpen, setLogsOpen] = useState(false);
  const buildFeedback = useBuildFeedback(buildView);
  useEffect(() => {
    if (buildFeedback.failed) setLogsOpen(true);
  }, [buildFeedback.failed, buildView.generation]);
  const [filter, setFilter] = useState("");
  const [showSearch, setShowSearch] = useState(false);
  const [filesExpanded, setFilesExpanded] = useState(true);
  const [modal, setModal] = useState<"new" | "help" | "settings" | "conflict" | "manage" | "directory" | "trash" | null>(null);
  const [filename, setFilename] = useState("");
  const [fileError, setFileError] = useState("");
  const search = useRef<HTMLInputElement>(null);
  const activeStatus = conflictFile ? "error" : localSession ? localStatus : status;
  const activeError = conflictFile ? `${conflictFile} 已被外部修改，自动保存已暂停。请对比版本后处理。` : localError || (localSession ? "" : error);
  const hasChanges = localSession ? view.files.some(file => file.dirty) || saveInFlight.current : activeStatus !== "saved";
  const { save, refreshWorkspace } = useDocumentPersistence(workspace, workspaceConnection, {
    saveDraft, revision: view.revision, lastSaveRequestedRevision,
  });
  const recovery = useConflictRecovery(workspace, workspaceConnection, {
    open: modal === "conflict", onOpen: () => setModal("conflict"), onClose: () => setModal(null), save,
  });
  const { inspectConflict } = recovery;
  useEffect(() => () => { void compileController.invalidate(false).catch(() => {}); }, []);
  const openWorkspace = useWorkspaceSwitch(workspace, workspaceConnection, workspaceOpenConnection, {
    nativeFiles, restoredSession, beforeOpen: () => compileController.invalidate(),
    onReveal: (line, column) => editor.current?.reveal(line, column),
    onClosed: () => { setBuildView({ state: 'idle', output: '', diagnostics: [] }); setPdfTarget(null); },
    onOpened: (openedProject, next) => {
      lastAutoCompileRevision.current = next.getView().revision;
      setFilter(""); setModal(null);
      setRecentWorkspaces(current => [openedProject.workspaceId, ...current.filter(id => id !== openedProject.workspaceId)].slice(0, 20));
      setProjectNames(current => {
        const names = { ...current, [openedProject.workspaceId]: openedProject.displayName };
        try { localStorage.setItem('lightoverleaf.projectNames', JSON.stringify(names)); } catch { /* Optional display cache. */ }
        return names;
      });
      resetProjectSearch();
      setBuildView({ state: "idle", output: "", diagnostics: [] });
      setPdfTarget(null); setSelectedDirectory(null);
    },
  });
  useEffect(() => {
    let active = true;
    let connectionFailed = false;
    const events = createStartupEvents(import.meta.env.DEV, () => {
      connectionFailed = true;
      if (active) setNative("error");
    }, window.location.hash === "#rpc-smoke");
    events.ready.then(async () => {
      if (window.location.hash === "#payload-smoke") await payloadSmokeProbe(workspaceConnection);
      return connection.api.ping({ version: 1, id: "workbench-startup", method: "system.ping", params: {}, clientSequence: 0 });
    })
      .then(() => systemConnection.ping())
      .then(() => systemConnection.getCapabilities())
      .then(capabilities => { if (active && !connectionFailed) { setNativeFiles(capabilities.nativeFiles); setNativeBuild(capabilities.build); setNative("ready"); } })
      .catch(() => { if (active) setNative("error"); });
    return () => { active = false; events.close(); };
  }, []);
  useEffect(() => {
    if (native !== "ready") return;
    let active = true;
    void Promise.all([
      preferencesSessionConnection.getPreferences(),
      preferencesSessionConnection.restoreSession(),
      preferencesSessionConnection.history(),
    ]).then(async ([savedPreferences, savedSession, history]) => {
      if (!active) return;
      settings.restore(savedPreferences);
      setRecentWorkspaces(history);
      if (savedSession.found) {
        restoredSession.current = savedSession.state;
        const { sidebarWidth, editorWidth, previewOpen, previewZoom } = savedSession.state;
        restoredLayout.current = { sidebarWidth, editorWidth, previewOpen, previewZoom };
        // Layout v2 is stored with the renderer profile; old pixel widths must not
        // override responsive ratios or turn off the new fit-width default.
        if (savedSession.state.workspaceRoot && nativeFiles)
          await openWorkspace(savedSession.state.workspaceRoot);
      }
    }).catch(failure => {
      if (active) setSettingsError("读取设置或会话失败：" + (failure as Error).message);
    }).finally(() => { if (active) setSessionReady(true); });
    return () => { active = false; };
  }, [native]);
  useEffect(() => {
    if (native !== "ready" || !sessionReady || workspaceBusy || !project) return;
    const timer = window.setTimeout(() => {
      void preferencesSessionConnection.saveSession({
        workspaceRoot: project?.workspaceId ?? "",
        openFiles: localSession ? view.open : [],
        activeFile: localSession ? view.active ?? "" : "",
        // Legacy protocol fields are round-tripped only; layout v2 belongs to the renderer profile.
        sidebarWidth: restoredLayout.current.sidebarWidth,
        editorWidth: restoredLayout.current.editorWidth,
        previewOpen: restoredLayout.current.previewOpen,
        activeLine: localSession ? view.line : 1,
        activeColumn: localSession ? view.column : 1,
        previewZoom: restoredLayout.current.previewZoom,
      }).then(() => {
        if (project?.workspaceId)
          setRecentWorkspaces(current => [project.workspaceId, ...current.filter(item => item !== project.workspaceId)].slice(0, 20));
      }).catch(failure => setSettingsError("保存会话失败：" + (failure as Error).message));
    }, 500);
    return () => window.clearTimeout(timer);
  }, [native, sessionReady, workspaceBusy, project?.workspaceId, localSession, view.open, view.active, view.line, view.column]);
  useEffect(() => {
    document.title = `${view.active?.split("/").at(-1) ?? "写作工作台"} · ${project?.displayName ?? "草稿"} · LightOverLeaf`;
  }, [view.active, project?.displayName]);
  useEffect(() => {
    const keydown = (event: KeyboardEvent) => {
      if (!modal && !event.isComposing && (event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "s") { event.preventDefault(); save(); }

    };
    window.addEventListener("keydown", keydown);
    return () => window.removeEventListener("keydown", keydown);
  }, [save, modal]);
  useEffect(() => {
    const unload = (event: BeforeUnloadEvent) => { if (hasChanges) { event.preventDefault(); event.returnValue = ""; } };
    window.addEventListener("beforeunload", unload);
    return () => window.removeEventListener("beforeunload", unload);
  }, [hasChanges]);
  useEffect(() => { if (showSearch) search.current?.focus(); }, [showSearch]);
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
  const exportDraft = async () => {
    if (!nativeFiles || workspaceBusy) return;
    setWorkspaceBusy(true); setLocalError("");
    try {
      const destination = await exportConnection.selectDestination();
      const capture = draftSession.capture();
      const result = await exportConnection.exportProject(destination.destinationToken,
        capture.revision + 1, capture.snapshot.files.map(file => ({ fileId: file.path, content: file.content })));
      if (!result.complete) {
        const details = result.failures.map(item => `${item.fileId || "项目"}: ${item.code}`).join("；");
        throw new Error(details || "EXPORT_INCOMPLETE");
      }
      setLocalError(`草稿已原子导出到 ${destination.displayName}（${result.writtenFiles.length} 个文件）。`);
    } catch (failure) {
      const code = (failure as Error).message;
      if (code !== "USER_CANCELLED") setLocalError("导出草稿失败；目标目录保持原状：" + code);
    } finally { setWorkspaceBusy(false); }
  };  const openLocalFile = async (path: string) => {
    if (workspaceBusy) return;
    if (!localSession) { session.activate(path); return; }
    if (localSession.model(path)) { localSession.activate(path); return; }
    const sequence = ++documentOpenSequence.current;
    try {
      const document = await workspaceConnection.openDocument(path);
      if (localSessionRef.current !== localSession || sequence !== documentOpenSequence.current) return;
      localSession.load(document.fileId, document.content);
      revisions.current.set(document.fileId, document.revision);
      setLocalStatus(localSession.getView().files.some(file => file.dirty) ? "pending" : "saved");
      setLocalError("");
    } catch (failure) {
      if (localSessionRef.current !== localSession || sequence !== documentOpenSequence.current) return;
      setLocalStatus("error"); setLocalError("打开文件失败：" + (failure as Error).message);
    }
  };
  const openSearchHit = async (fileId: string, line: number) => {
    await openLocalFile(fileId);
    if (localSessionRef.current === localSession) {
      editor.current?.reveal(line);
    }
  };
  const compileCurrent = async () => {
    const compileSession = localSession ?? draftSession;
    const compileProject = project;
    if (!nativeBuild || workspaceBusy || switchingWorkspace.current || mutationBusy.current || conflictRef.current || refreshInFlight.current) return;

    const generation = compileSession.capture();
    const captured = generation.snapshot;
    const mainFileId = compileProject ?
      compileProject.entries.find(item => !item.directory && item.fileId.toLowerCase() === "main.tex")?.fileId ??
        (compileSession.getView().active?.toLowerCase().endsWith(".tex") ? compileSession.getView().active : undefined) :
      captured.files.find(item => item.path.toLowerCase() === "main.tex")?.path ??
        (captured.active?.toLowerCase().endsWith(".tex") ? captured.active : undefined);
    if (!mainFileId) {
      setBuildView(current => ({ ...current, state: "failed", output: "未找到 main.tex 或当前 .tex 主文件。",
        diagnostics: [] }));
      return;
    }
    const overlayFiles = captured.files.map(file =>
      ({ fileId: file.path, content: file.content }));
    lastAutoCompileRevision.current = Math.max(lastAutoCompileRevision.current, generation.revision);
    await compileController.run({ mainFileId, files: overlayFiles,
      scopeId: compileProject?.workspaceId ?? draftId,
      engine: preferences.engine, timeoutMs: preferences.timeoutMs },
      () => !switchingWorkspace.current &&
        (compileProject ? localSessionRef.current === compileSession : localSessionRef.current === null));
  };
  const requestCompile = () => {
    if (workspaceBusy || refreshInFlight.current) {
      setLocalError("正在处理项目文件，请稍后再编译。");
      return;
    }
    if (!nativeBuild) {
      setLocalError("原生编译服务尚未就绪。");
      return;
    }
    if (!view.active) {
      setLocalError("请先打开一个 LaTeX 文件。");
      return;
    }
    void compileCurrent();
  };
  useEffect(() => {
    const compileShortcut = (event: KeyboardEvent) => {
      if (modal || event.isComposing || event.repeat || event.altKey || event.shiftKey ||
          !(event.ctrlKey || event.metaKey) || event.key !== "Enter") return;
      event.preventDefault();
      const state = previewPresentation(buildView.state);
      if (state.running) void cancelBuild();
      else if (!state.preparing && !conflictFile) requestCompile();
    };
    window.addEventListener("keydown", compileShortcut);
    return () => window.removeEventListener("keydown", compileShortcut);
  });
  useEffect(() => {
    if (workspaceBusy || !nativeBuild || preferences.compileMode !== "live" || localSession || !view.active ||
        view.revision <= lastAutoCompileRevision.current) return;
    const revision = view.revision;
    const timer = window.setTimeout(() => {
      if (localSessionRef.current || draftSession.getView().revision !== revision) return;

      void compileCurrent();
    }, 900);
    return () => window.clearTimeout(timer);
  }, [workspaceBusy, nativeBuild, buildView.state, draftSession, localSession, preferences.compileMode, view.active, view.revision]);
  useEffect(() => {
    if (workspaceBusy || !nativeBuild || preferences.compileMode !== "live" || !localSession || !project ||
        conflictRef.current || view.revision <= lastAutoCompileRevision.current) return;
    const revision = view.revision;
    const timer = window.setTimeout(() => {
      if (localSessionRef.current !== localSession || localSession.getView().revision !== revision) return;

      void compileCurrent();
    }, 900);
    return () => window.clearTimeout(timer);
  }, [workspaceBusy, nativeBuild, activeStatus, buildView.state, localSession, preferences.compileMode, project, view.revision]);
  useEffect(() => {
    const requested = lastSaveRequestedRevision.current;
    if (workspaceBusy || !nativeBuild || preferences.compileMode !== "onSave" || activeStatus !== "saved" || !view.active ||
        !requested || requested > view.revision || requested <= lastAutoCompileRevision.current) return;
    lastSaveRequestedRevision.current = 0;

    void compileCurrent();
  }, [workspaceBusy, nativeBuild, activeStatus, buildView.state, preferences.compileMode, view.active, view.revision]);
  const forwardSync = async () => {
    if (!buildView.artifactId || !buildView.syncTexAvailable || !view.active) return;
    try {
      const location = await authoringConnection.forward(buildView.artifactId, view.active, view.line, view.column);
      setPreview(true);
      setPdfTarget({ ...location, revision: Date.now() });
      setLocalError("");
    } catch (failure) {
      setLocalError("SyncTeX 正向定位失败：" + (failure as Error).message);
    }
  };
  const cancelBuild = async () => {
    try { await compileController.cancel(); }
    catch (failure) { setLocalError("取消编译失败：" + (failure as Error).message); }
  };
  const openSettings = () => {
    settings.begin(); setModal("settings");
    void preferencesSessionConnection.history().then(setRecentWorkspaces)
      .catch(failure => setSettingsError("读取最近项目失败：" + (failure as Error).message));
  };
  const savePreferences = async () => {
    const previousRoot = preferences.texRoot;
    const saved = await settings.save();
    if (!saved) return;
    setModal(null);
    if (saved.texRoot !== previousRoot)
      setLocalError("编译设置已保存；当前项目下次编译会使用内置 MiKTeX Runtime。");
  };
  const newFile = () => {
    if (localSession) { startFileOperation('create'); return; }
    setFilename(""); setFileError(""); setModal("new");
  };
  const startFileOperation = (operation: 'create' | 'rename' | 'remove', target = view.active) => {
    if (!localSession || workspaceBusy || refreshInFlight.current) return;
    if (conflictRef.current || saveInFlight.current || localSession.getView().files.some(file => file.dirty)) {
      setLocalError('请先保存所有修改并处理冲突，再进行文件操作。'); return;
    }
    if (operation !== 'create' && !target) return;
    setFileOperation(operation); setOperationSource(target ?? '');
    setFilename(operation === 'rename' ? target ?? '' : ''); setFileError(''); setModal('manage');
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
      setLocalError(fileOperation === 'remove' ? '文件已移入项目内 .lightoverleaf-trash，可通过“项目回收站”恢复。' : '');
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
        '目录已连同内容移入项目内 .lightoverleaf-trash，可通过“项目回收站”恢复。' : '');
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
  const compilePresentation = previewPresentation(buildView.state);
  const compileReason = !editorReady ? "编辑器正在初始化" : workspaceBusy ? "正在处理项目，请稍候" : conflictFile ? "请先处理文件冲突" :
    !view.active ? "请先打开主文件" : !nativeBuild ? "编译服务尚未就绪，请检查设置" : "";
  return <main className="workbench">
    <WorkbenchHeader projectName={project?.displayName ?? "未命名项目"} local={Boolean(localSession)} busy={workspaceBusy}
      canOpen={nativeFiles && sessionReady && !workspaceBusy} canExport={nativeFiles && !workspaceBusy}
      compileAction={compilePresentation.action} compileRunning={compilePresentation.running}
      compileDisabled={compilePresentation.preparing || (!compilePresentation.running && Boolean(compileReason))}
      compileReason={compilePresentation.preparing ? "正在准备编译" : compileReason}
      onCompile={compilePresentation.running ? () => void cancelBuild() : requestCompile}
      recent={recentWorkspaces.map(id => ({ id, name: projectNames[id] ?? "本地项目" }))}
      onOpen={id => void openWorkspace(id)} onSave={save} onExport={() => void exportDraft()}
      onRefresh={() => void refreshWorkspace()} onHelp={() => setModal("help")}
      preset={layout.preset} onPreset={layout.setPreset} preview={preview} onPreview={setPreview} />
    <div className="workspace-body">
      <aside className="activity-bar" aria-label="工作区工具">
        <button className={sidebar && !showSearch ? "active" : ""} aria-label="切换文件侧栏" title="文件" onClick={() => { setSidebar(!sidebar || showSearch); setShowSearch(false); }}><Icon name="file" size={21} /></button>
        <button className={showSearch ? "active" : ""} aria-label="搜索项目" title="搜索项目内容" onClick={() => { setSidebar(true); setFilesExpanded(true); setShowSearch(!showSearch); }}><Icon name="search" size={20} /></button>
        <button aria-label="新建文件" title="新建文件" disabled={workspaceBusy} onClick={newFile}><Icon name="plus" size={20} /></button>
        <div className="rail-spacer" /><button aria-label="设置" title="设置" onClick={openSettings}><Icon name="settings" size={19} /></button>
        <button aria-label="工作台帮助" title="帮助" onClick={() => setModal("help")}><Icon name="info" size={19} /></button>
      </aside>
      {sidebar && layout.drawer && <button className="sidebar-scrim" aria-label="收起文件侧栏" onClick={() => setSidebar(false)} />}
      {sidebar && <><aside className={"sidebar " + (layout.drawer ? "sidebar-drawer " : "") + (!filesExpanded ? "files-collapsed" : "")} style={{ width: sidebarWidth }} aria-label="文件与大纲">
        <div className="panel-heading explorer-heading">
          <button className="explorer-toggle" aria-expanded={filesExpanded} aria-controls="explorer-content"
            title={filesExpanded ? "收起文件树" : "展开文件树"} onClick={() => setFilesExpanded(value => !value)}>
            <span className={filesExpanded ? "section-chevron expanded" : "section-chevron"}><Icon name="chevron" size={14} /></span>
            <span>文件树</span>
          </button>
          <div className="heading-actions"><Tool icon="plus" label="新建文件" disabled={workspaceBusy} onClick={newFile} />
            {localSession && <Tool icon="folder" label="管理目录" disabled={workspaceBusy} onClick={startDirectoryOperation} />}
            <ActionMenu title="文件操作" className="align-right file-actions" label="⋯">
              <button disabled={!localSession || !view.active || workspaceBusy} onClick={() => startFileOperation("rename")}>重命名当前文件…</button>
              <button disabled={!localSession || !view.active || workspaceBusy} onClick={() => startFileOperation("remove")}>删除当前文件…</button>
              <hr /><button disabled={!localSession || workspaceBusy} onClick={() => void refreshWorkspace()}>刷新文件树</button>
              <button disabled={!localSession || workspaceBusy} onClick={() => void openTrash()}>项目回收站…</button>
            </ActionMenu>
            <Tool icon="close" label="隐藏侧栏" onClick={() => setSidebar(false)} /></div></div>
        <div id="explorer-content" className="explorer-content" hidden={!filesExpanded}>
        <div className="workspace-label"><span className="tiny-dot" /><span className="truncate" title={project?.displayName ?? "应用内草稿"}>{project?.displayName ?? "应用内草稿"}</span><span>{explorerFiles.length}</span></div>
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
          openFiles={view.open} onSelectDirectory={setSelectedDirectory}
          onRename={localSession ? path => startFileOperation("rename", path) : undefined}
          onRemove={localSession ? path => startFileOperation("remove", path) : undefined}
          operationsDisabled={workspaceBusy} onOpen={path => { void openLocalFile(path); if (layout.drawer) setSidebar(false); }} />
        </div>
        <OutlinePanel key={view.active} entries={view.outline} activeLine={view.line}
          onSelect={line => { editor.current?.reveal(line); if (layout.drawer) setSidebar(false); }} />
      </aside>{!layout.drawer && <Splitter label="调整侧栏宽度" min={220} max={380}
        value={sidebarWidth} onChange={setSidebarWidth} />}</>}
      <div className="work-area"><div className="document-panes">
      <section className="editor-panel" aria-label="源码编辑区域" hidden={!layout.editorVisible}
        style={layout.previewVisible ? { flexBasis: editorWidth, flexGrow: 0, flexShrink: 0 } : undefined}>
        <div className="editor-tabs" role="tablist" aria-label="打开的文件" onKeyDown={event => {
          if (!(event.target instanceof HTMLElement) || event.target.getAttribute("role") !== "tab" ||
              !["ArrowLeft", "ArrowRight", "Home", "End"].includes(event.key)) return;
          event.preventDefault();
          const tabs = Array.from(event.currentTarget.querySelectorAll<HTMLButtonElement>('[role="tab"]'));
          const index = tabs.indexOf(event.target as HTMLButtonElement);
          const next = event.key === "Home" ? 0 : event.key === "End" ? tabs.length - 1 :
            (index + (event.key === "ArrowRight" ? 1 : -1) + tabs.length) % tabs.length;
          tabs[next]?.focus(); tabs[next]?.click();
          tabs[next]?.scrollIntoView({ block: "nearest", inline: "nearest" });
        }}>
          {view.open.map(path => <div key={path} className={"editor-tab " + (path === view.active ? "active" : "")}>
            <button role="tab" tabIndex={path === view.active ? 0 : -1} aria-selected={path === view.active} onClick={() => session.activate(path)} title={path}><Icon name="file" size={14} /><span className="tab-filename">{view.open.filter(item => item.split("/").at(-1) === path.split("/").at(-1)).length > 1 ? path : path.split("/").at(-1)}</span>{view.files.find(file => file.path === path)?.dirty && <span className="dirty-dot" />}</button>
            <button className="tab-close" aria-label={"关闭标签 " + path} title="关闭标签，不删除文件" onClick={() => session.close(path)}><Icon name="close" size={12} /></button>
          </div>)}
          <button className="tab-add" title="新建文件" aria-label="添加文件" disabled={workspaceBusy} onClick={newFile}><Icon name="plus" size={14} /></button>
        </div>
        <div className="editor-toolbar">
          <Tool icon="undo" label="撤销 (Ctrl+Z)" disabled={!view.active || workspaceBusy} onClick={() => editor.current?.run("undo")} />
          <Tool icon="redo" label="重做 (Ctrl+Y)" disabled={!view.active || workspaceBusy} onClick={() => editor.current?.run("redo")} />
          <span className="toolbar-divider" />
          <button className="text-tool" title="插入粗体命令" aria-label="插入粗体" disabled={!view.active || workspaceBusy} onClick={() => editor.current?.run("bold")}><b>B</b></button>
          <button className="text-tool" title="插入斜体命令" aria-label="插入斜体" disabled={!view.active || workspaceBusy} onClick={() => editor.current?.run("italic")}><i>I</i></button>
          <ActionMenu title="插入 LaTeX 结构" label={<>插入 <span className="menu-chevron"><Icon name="chevron" size={12} /></span></>}>
            {([["section", "章节"], ["subsection", "小节"], ["equation", "编号公式"],
              ["itemize", "无序列表"], ["enumerate", "有序列表"], ["table", "两列表格"],
              ["reference", "交叉引用"]] as const).map(([command, label]) =>
                <button key={command} disabled={!view.active || workspaceBusy} onClick={() => editor.current?.run(command)}>{label}</button>)}
          </ActionMenu>
          <Tool icon="wrap" label="自动换行" pressed={wrap} onClick={() => setWrap(!wrap)} />
          <div className="editor-path" title={view.active ?? "未打开文件"}>{view.active ?? "未打开文件"}</div>
          <Tool icon="pdf" label="定位到 PDF" disabled={!buildView.artifactId || !buildView.syncTexAvailable || !view.active}
            onClick={() => void forwardSync()} />
          <Tool icon="search" label="查找当前文档 (Ctrl+F)" disabled={!view.active} onClick={() => editor.current?.run("find")} />
        </div>
        <div className="editor-content"><EditorSurface session={session} wrap={wrap} readOnly={workspaceBusy} ref={editor} onReady={() => setEditorReady(true)} />
          {!view.active && <div className="editor-empty"><Icon name="code" size={38} /><p>从左侧选择文件，继续写作。</p>{!localSession && <button onClick={newFile}>新建草稿</button>}</div>}
        </div>
        {activeError && <div className={"draft-notice " + (activeStatus === "error" ? "error" : "")} role={activeError ? "alert" : undefined}><Icon name={activeStatus === "error" ? "warning" : "info"} size={13} /><span>{activeError || (localSession ? "本地保存使用 Revision 冲突保护，不会静默覆盖外部修改。" : "草稿可直接编译预览；只有导出项目时才选择目标目录。")}</span>
          {conflictFile && <button onClick={() => void inspectConflict()}>对比版本</button>}</div>}
      </section>
      {layout.previewVisible && layout.editorVisible && <Splitter label="调整编辑器宽度" min={300}
        max={layout.maxEditorWidth} value={editorWidth} onChange={setEditorWidth} />}
      <div className="preview-pane-shell" hidden={!layout.previewVisible}>
        <PreviewPanel build={buildView}
          onConfigure={openSettings}
          onPreviewCommit={generation => setBuildView(current => {
            if (current.candidate?.generation !== generation) return current;
            const committed = current.candidate;
            return { ...current, state: "succeeded", artifactId: committed.artifactId, pdf: committed.pdf,
              syncTexAvailable: committed.syncTexAvailable, candidate: undefined, phase: "complete" };
          })}
          onPreviewReject={(generation, message) => setBuildView(current =>
            current.candidate?.generation !== generation ? current : ({ ...current, state: "failed",
              candidate: undefined, phase: "render", output: current.output + "\nPDF.js: " + message }))}
          target={pdfTarget} zoom={previewZoom} onZoomChange={setPreviewZoom}
          zoomMode={layout.zoomMode} onZoomModeChange={layout.setZoomMode}
          logsOpen={logsOpen} onToggleLogs={() => setLogsOpen(value => !value)}
          onReverse={(page, x, y) => { if (buildView.artifactId) void authoringConnection.reverse(buildView.artifactId, page, x, y)
            .then(location => openSearchHit(location.fileId, location.line))
            .catch(failure => setLocalError("SyncTeX 反向定位失败：" + (failure as Error).message)); }}
          /></div>
      </div>
      <BuildLogPanel build={buildView} open={logsOpen} onClose={() => setLogsOpen(false)}
        onDiagnostic={(fileId, line) => { if (!layout.editorVisible) layout.setPreset("split"); void openSearchHit(fileId, line); }} />
      </div>
    </div>
    <footer className="statusbar"><span className="status-brand" title={connection.mode}><Icon name="leaf" size={12} />
      {connection.mode.includes("Fake") ? "DEV" : "LOCAL"}</span>
      <span className={activeStatus === "error" ? "error" : ""} role="status"><Icon name={activeStatus === "saved" ? "check" : activeStatus === "error" ? "warning" : "save"} size={13} />
        {statusLabel}</span>
      <button className={"build-status " + (buildFeedback.failed ? "error" : "")} onClick={() => setLogsOpen(value => !value)}
        aria-expanded={logsOpen} title="展开或收起编译日志"><Icon name={buildFeedback.failed ? "warning" : "terminal"} size={13} />
        {buildFeedback.summary}{buildFeedback.elapsedLabel && <span className="elapsed-time">{buildFeedback.elapsedLabel}</span>}</button>
      <span className="status-spacer" />
      <span className={"native-status " + (native === "error" ? "error" : "")} title={connection.mode}>
        <i className="tiny-dot" />{native === "error" ? "原生通信异常" : native === "pending" ? "连接中…" : connection.mode.includes("Fake") ? "浏览器开发模式" : "原生服务已连接"}</span>
      <span>行 {view.line}，列 {view.column}</span><span className="encoding-status">UTF-8</span><span className="encoding-status">LaTeX</span></footer>
    {modal && <Dialog key={modal} className={modal === "conflict" ? "conflict-modal" : ""}
      labelId="dialog-title" onClose={() => setModal(null)}>
        <div className="modal-title"><h2 id="dialog-title">{modal === "settings" ? "设置" : modal === "trash" ? "项目回收站" : modal === "directory" ? "目录管理" : modal === "manage" ? ({ create: '新建本地文件', rename: '重命名文件', remove: '删除文件' })[fileOperation] : modal === "conflict" ? "处理文件冲突" : modal === "new" ? "新建草稿文件" : "快捷键与帮助"}</h2><Tool icon="close" label="关闭对话框" onClick={() => setModal(null)} /></div>
        {modal === "settings" ? <SettingsForm preferencesDraft={preferencesDraft} setPreferencesDraft={setPreferencesDraft}
          settingsBusy={settingsBusy} settingsError={settingsError}
          onSave={() => void savePreferences()} onClose={() => setModal(null)} /> : modal === "trash" ? <>
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
          {fileOperation === 'remove' ? <p>将 {operationSource} 移入项目内 .lightoverleaf-trash。可通过“项目回收站”恢复。</p> : <>
            <label htmlFor="local-filename">{fileOperation === 'rename' ? '新名称或相对路径' : '文件名或相对路径'}</label>
            <input id="local-filename" disabled={workspaceBusy} value={filename} onChange={event => setFilename(event.target.value)} maxLength={160} />
            <p>父文件夹必须已存在；同名文件不会被覆盖。</p></>}
          {fileError && <p role="alert" className="error">{fileError}</p>}
          <div className="modal-actions"><button type="button" onClick={() => setModal(null)}>取消</button>
            <button type="submit" className="primary" disabled={workspaceBusy}>{workspaceBusy ? '处理中…' : '确认'}</button></div>
        </form> : modal === "conflict" ? <ConflictForm recovery={recovery} conflictFile={conflictFile} onClose={() => setModal(null)} /> : modal === "new" ? <form onSubmit={event => {
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
          <p className="muted">常用操作</p>
          <div className="shortcut"><span>编译 / 取消编译</span><kbd>Ctrl + Enter</kbd></div>
          <div className="shortcut"><span>保存文件 / 缓存草稿</span><kbd>Ctrl + S</kbd></div>
          <div className="shortcut"><span>查找当前文档</span><kbd>Ctrl + F</kbd></div>
          <div className="shortcut"><span>撤销 / 重做</span><kbd>Ctrl + Z / Y</kbd></div>
          <p className="help-note">草稿保存在当前应用中。重要内容请通过“项目 → 导出草稿”备份；关闭文件标签不会删除文件。</p>
          <div className="modal-actions"><button className="primary" onClick={() => setModal(null)}>关闭</button></div>
        </>}
    </Dialog>}
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
