import { useCallback, useEffect, type RefObject } from "react";
import type { WorkspaceRpcClient } from "../../native-api";
import { LocalDocumentSaveError, saveLocalDocuments } from "../saveLocalDocuments";
import { reconcileWorkspaceRefresh } from "../refreshLocalWorkspace";
import type { LocalWorkspace } from "./useLocalWorkspace";
export function useDocumentPersistence(workspace: LocalWorkspace, workspaceConnection: WorkspaceRpcClient,
  { saveDraft, revision, lastSaveRequestedRevision }: {
    saveDraft(): void; revision: number; lastSaveRequestedRevision: RefObject<number>;
  }) {
  const { localSession, localSessionRef, project, setProject, revisions, setLocalStatus, setLocalError,
    workspaceBusy, saveInFlight, saveDeferred, conflictRef, setConflictFile, refreshInFlight, refreshSequence,
    refreshDeferred, mutationBusy, setSelectedDirectory } = workspace;
  const save = useCallback(() => {
    lastSaveRequestedRevision.current = revision;
    if (!localSession) { saveDraft(); return; }
    if (localSessionRef.current !== localSession) return;
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
    }).finally(() => { if (localSessionRef.current === localSession) saveInFlight.current = false; });
  }, [localSession, saveDraft, revision]);
  const refreshWorkspace = useCallback(async () => {
    if (!localSession || localSessionRef.current !== localSession || !project || workspaceBusy || refreshInFlight.current || mutationBusy.current ||
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
    let active = true;
    const timer = window.setInterval(() => {
      if (polling || document.visibilityState !== "visible") return;
      polling = true;
      void workspaceConnection.pollChanges(project.workspaceId)
        .then(changed => { if (active && localSessionRef.current === localSession && changed) void refreshWorkspace(); })
        .catch(() => { if (active && localSessionRef.current === localSession) refreshDeferred.current = true; })
        .finally(() => { polling = false; });
    }, 1000);
    return () => { active = false; window.clearInterval(timer); };
  }, [localSession, project, refreshWorkspace]);
  return { save, refreshWorkspace };
}
