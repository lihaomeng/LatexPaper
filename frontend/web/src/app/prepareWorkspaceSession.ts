import { EditorSession } from '../features/editor';
import type { SessionSnapshot, WorkspaceRpcClient } from '../native-api';
import type { WorkspaceStateResponseResult } from '../../.generated/rpc/protocol';

/** Prepare models without touching the currently displayed editor session. */
export async function prepareWorkspaceSession(project: WorkspaceStateResponseResult,
  documents: Pick<WorkspaceRpcClient, 'openDocument'>, saved: SessionSnapshot | null) {
  const candidates = project.entries.filter(entry => !entry.directory && /\.(tex|bib|md|txt)$/i.test(entry.fileId));
  const first = candidates.find(entry => entry.fileId.toLowerCase() === 'main.tex') ?? candidates[0];
  const available = new Set(candidates.map(entry => entry.fileId));
  const previous = saved?.workspaceRoot === project.workspaceId ? saved : null;
  const requested = previous?.openFiles.filter(file => available.has(file)) ?? [];
  const files = requested.length ? requested : first ? [first.fileId] : [];
  const session = new EditorSession({ files: [], open: [], active: null });
  const revisions = new Map<string, string>();
  try {
    for (const fileId of files) {
      const document = await documents.openDocument(fileId);
      session.load(document.fileId, document.content);
      revisions.set(document.fileId, document.revision);
    }
    const restoreCursor = previous?.activeFile && session.model(previous.activeFile)
      ? { line: previous.activeLine, column: previous.activeColumn } : null;
    if (restoreCursor && previous) session.activate(previous.activeFile);
    return { session, revisions, restoreCursor };
  } catch (error) {
    session.dispose();
    throw error;
  }
}
