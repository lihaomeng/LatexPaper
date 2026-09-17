import { useEffect, useRef, useState } from "react";
import type { WorkspaceRpcClient } from "../../native-api";
import { validDraftPath } from "../../features/session";
import { adoptDiskVersion, compareDocument, type DocumentComparison } from "../documentRecovery";
import { saveConflictCopy } from "../saveConflictCopy";
import { containsMergeMarkers, mergeDocumentText } from "../threeWayMerge";
import type { LocalWorkspace } from "./useLocalWorkspace";
export function useConflictRecovery(workspace: LocalWorkspace, workspaceConnection: WorkspaceRpcClient,
  { open, onOpen, onClose, save }: { open: boolean; onOpen(): void; onClose(): void; save(): void }) {
  const { localSession, localSessionRef, setProject, revisions, setLocalStatus, setLocalError, saveInFlight,
    conflictRef, setConflictFile, refreshDeferred, mutationBusy } = workspace;
  const [comparison, setComparison] = useState<DocumentComparison | null>(null);
  const [recoveryBusy, setRecoveryBusy] = useState(false);
  const [recoveryError, setRecoveryError] = useState("");
  const [conflictCopyPath, setConflictCopyPath] = useState("");
  const [mergeDraft, setMergeDraft] = useState("");
  const [mergeConflicts, setMergeConflicts] = useState(0);
  const recoverySequence = useRef(0);

  useEffect(() => {
    if (!open) return;
    return () => { ++recoverySequence.current; };
  }, [open]);
  useEffect(() => () => { ++recoverySequence.current; }, []);
  const inspectConflict = async () => {
    if (!localSession || !conflictRef.current || saveInFlight.current) return;
    const sequence = ++recoverySequence.current;
    const fileId = conflictRef.current;
    onOpen(); setComparison(null); setRecoveryError(""); setRecoveryBusy(true);
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
    } finally { if (isCurrent()) setRecoveryBusy(false); }
  };
  const acceptDisk = () => {
    if (!localSession || !comparison || recoveryBusy || saveInFlight.current) return;
    try {
      adoptDiskVersion(localSession, comparison, revisions.current,
        () => localSessionRef.current === localSession && conflictRef.current === comparison.fileId);
      conflictRef.current = null; setConflictFile(null); setComparison(null); onClose(); setLocalError("");
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
    conflictRef.current = null; setConflictFile(null); setComparison(null); onClose();
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
          conflictRef.current = null; setConflictFile(null); setComparison(null); onClose();
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
  return { comparison, recoveryBusy, recoveryError, conflictCopyPath, setConflictCopyPath,
    mergeDraft, setMergeDraft, mergeConflicts, setRecoveryError,
    inspectConflict, acceptDisk, applyMerge, saveConflictAs };
}
