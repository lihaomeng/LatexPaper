import type { WorkspaceStateResponseResult } from '../../.generated/rpc/protocol';

interface RefreshSession {
  getView(): { files: { path: string; dirty: boolean }[] };
  recoverySnapshot(path: string): { content: string; version: number; dirty: boolean } | null;
  replaceFromDisk(path: string, content: string, expectedVersion: number): boolean;
  forget(path: string): void;
}
interface DocumentReader {
  openDocument(path: string): Promise<{ fileId: string; content: string; revision: string }>;
}
export interface WorkspaceRefreshSummary {
  reloaded: number;
  removed: number;
  deferred: number;
}

export async function reconcileWorkspaceRefresh(session: RefreshSession, revisions: Map<string, string>,
    state: WorkspaceStateResponseResult, reader: DocumentReader, isCurrent: () => boolean): Promise<WorkspaceRefreshSummary> {
  const available = new Set(state.entries.filter(entry => !entry.directory).map(entry => entry.fileId));
  const summary: WorkspaceRefreshSummary = { reloaded: 0, removed: 0, deferred: 0 };
  for (const { path } of session.getView().files) {
    if (!isCurrent()) throw new Error('STALE_WORKSPACE');
    const local = session.recoverySnapshot(path);
    if (!local) continue;
    if (!available.has(path)) {
      if (local.dirty) { summary.deferred++; continue; }
      session.forget(path); revisions.delete(path); summary.removed++;
      continue;
    }
    if (local.dirty) { summary.deferred++; continue; }
    try {
      const disk = await reader.openDocument(path);
      if (!isCurrent()) throw new Error('STALE_WORKSPACE');
      if (disk.fileId !== path) throw new Error('INVALID_RESPONSE');
      if (disk.revision === revisions.get(path)) continue;
      if (!session.replaceFromDisk(path, disk.content, local.version)) { summary.deferred++; continue; }
      revisions.set(path, disk.revision); summary.reloaded++;
    } catch (failure) {
      if ((failure as Error).message !== 'FILE_NOT_FOUND') throw failure;
      const latest = session.recoverySnapshot(path);
      if (!latest || latest.dirty || latest.version !== local.version) { summary.deferred++; continue; }
      session.forget(path); revisions.delete(path); summary.removed++;
    }
  }
  return summary;
}
