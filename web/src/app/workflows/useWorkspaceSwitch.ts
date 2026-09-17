import { useEffect, useRef, type RefObject } from "react";
import type { SessionSnapshot, WorkspaceRpcClient } from "../../native-api";
import type { EditorSession } from "../../features/editor";
import { prepareWorkspaceSession } from "../prepareWorkspaceSession";
import type { LocalWorkspace } from "./useLocalWorkspace";
export function useWorkspaceSwitch(workspace: LocalWorkspace,
  workspaceConnection: WorkspaceRpcClient, workspaceOpenConnection: WorkspaceRpcClient,
  { nativeFiles, restoredSession, beforeOpen, onOpened, onClosed, onReveal }: {
    nativeFiles: boolean; restoredSession: RefObject<SessionSnapshot | null>;
    beforeOpen(): Promise<void>;
    onOpened(project: NonNullable<LocalWorkspace["project"]>, session: EditorSession): void;
    onClosed(): void; onReveal(line: number, column: number): void;
  }) {
  const { localSession, setLocalSession, localSessionRef, project, setProject, revisions, setLocalStatus,
    setLocalError, workspaceBusy, setWorkspaceBusy, documentOpenSequence, saveInFlight, conflictRef,
    refreshInFlight, refreshSequence, refreshDeferred, mutationBusy, switchingWorkspace, setSelectedDirectory } =
    workspace;
  const request = useRef<AbortController | null>(null);
  useEffect(() => () => { request.current?.abort(); request.current = null; }, []);
  const openWorkspace = async (workspaceId?: string) => {
    if (!nativeFiles || workspaceBusy || switchingWorkspace.current || mutationBusy.current || refreshInFlight.current) return;
    ++refreshSequence.current;
    if (localSession && (conflictRef.current || saveInFlight.current || localSession.getView().files.some(file => file.dirty))) {
      setLocalError("当前项目仍有未保存修改，请保存成功后再切换项目。");
      return;
    }
    const pending = new AbortController();
    request.current = pending;
    const isCurrent = () => request.current === pending && !pending.signal.aborted;
    ++documentOpenSequence.current;
    switchingWorkspace.current = true;
    setWorkspaceBusy(true); setLocalError("");
    let openedWorkspace = false;
    let pendingSession: EditorSession | null = null;
    try {
      await beforeOpen();
      if (!isCurrent()) return;
      const openedProject = workspaceId ? await workspaceOpenConnection.reopen(workspaceId, pending.signal) : await workspaceOpenConnection.open(pending.signal);
      if (!isCurrent()) return;
      openedWorkspace = true;
      const prepared = await prepareWorkspaceSession(openedProject, {
        openDocument: fileId => workspaceConnection.openDocument(fileId, pending.signal),
      }, restoredSession.current);
      const next = prepared.session;
      pendingSession = next;
      if (!isCurrent()) return;
      const nextRevisions = prepared.revisions;
      if (prepared.restoreCursor)
        window.setTimeout(() => {
          if (localSessionRef.current === next)
            onReveal(prepared.restoreCursor!.line, prepared.restoreCursor!.column);
        });
      restoredSession.current = null;
      localSessionRef.current?.dispose();
      localSessionRef.current = next;
      revisions.current = nextRevisions;
      refreshDeferred.current = false;
      setProject(openedProject); setLocalSession(next); setLocalStatus("saved");
      pendingSession = null;
      onOpened(openedProject, next);
    } catch (failure) {
      if (!isCurrent()) return;
      const code = (failure as Error).message;
      if (!openedWorkspace && code !== "USER_CANCELLED" && project) {
        try { openedWorkspace = (await workspaceConnection.getState()).workspaceId !== project.workspaceId; }
        catch { openedWorkspace = true; }
      }
      if (!isCurrent()) return;
      if (openedWorkspace) {
        await workspaceConnection.close().catch(() => {});
        if (!isCurrent()) return;
        onClosed();
        localSessionRef.current?.dispose(); localSessionRef.current = null;
        setLocalSession(null); setProject(null); setSelectedDirectory(null); revisions.current.clear();
      }
      if (code !== "USER_CANCELLED") setLocalError("打开本地项目失败：" + code + "。请点击打开项目重新选择文件夹；旧版本记录需重新选择一次。");
    } finally {
      pendingSession?.dispose();
      if (isCurrent()) {
        request.current = null;
        switchingWorkspace.current = false; setWorkspaceBusy(false);
      }
    }
  };
  return openWorkspace;
}
