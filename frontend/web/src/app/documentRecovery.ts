interface RecoverySession {
  recoverySnapshot(path: string): { content: string; baseContent?: string; version: number } | null;
  replaceFromDisk(path: string, content: string, expectedVersion: number): boolean;
}
interface DocumentReader {
  openDocument(path: string): Promise<{ fileId: string; content: string; revision: string; utf8Bom?: boolean }>;
}
export interface DocumentComparison {
  fileId: string;
  localContent: string;
  baseContent: string;
  localVersion: number;
  diskContent: string;
  diskRevision: string;
  diskUtf8Bom: boolean;
}
export async function compareDocument(session: RecoverySession, fileId: string,
    reader: DocumentReader, isCurrent: () => boolean): Promise<DocumentComparison> {
  const local = session.recoverySnapshot(fileId);
  if (!local || !isCurrent()) throw new Error('STALE_DOCUMENT');
  const disk = await reader.openDocument(fileId);
  if (!isCurrent() || session.recoverySnapshot(fileId)?.version !== local.version)
    throw new Error('STALE_DOCUMENT');
  if (disk.fileId !== fileId) throw new Error('INVALID_RESPONSE');
  return { fileId, localContent: local.content, baseContent: local.baseContent ?? local.content,
    localVersion: local.version,
    diskContent: disk.content, diskRevision: disk.revision, diskUtf8Bom: disk.utf8Bom ?? false };
}
export function adoptDiskVersion(session: RecoverySession, comparison: DocumentComparison,
    revisions: Map<string, string>, isCurrent: () => boolean): void {
  if (!isCurrent() || !session.replaceFromDisk(comparison.fileId, comparison.diskContent, comparison.localVersion))
    throw new Error('STALE_DOCUMENT');
  revisions.set(comparison.fileId, comparison.diskRevision);
}
