import type { useConflictRecovery } from "../workflows/useConflictRecovery";
export function ConflictForm({ recovery, conflictFile, onClose }: {
  recovery: ReturnType<typeof useConflictRecovery>; conflictFile: string | null; onClose(): void;
}) {
  const { comparison, recoveryBusy, recoveryError, conflictCopyPath, setConflictCopyPath, mergeDraft, setMergeDraft, mergeConflicts, setRecoveryError, inspectConflict, acceptDisk, applyMerge, saveConflictAs } = recovery;
  return <>
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
          <div className="modal-actions"><button onClick={() => onClose()}>保留当前编辑</button>
            <button disabled={recoveryBusy} onClick={() => void inspectConflict()}>重新读取对比</button>
            <button disabled={!comparison || recoveryBusy} onClick={() => void saveConflictAs()}>另存副本</button>
            <button disabled={!comparison || recoveryBusy} onClick={applyMerge}>应用合并</button>
            <button className="primary" disabled={!comparison || recoveryBusy} onClick={acceptDisk}>采用磁盘版本</button></div>
        </>;
}
