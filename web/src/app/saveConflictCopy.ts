import type { DocumentComparison } from './documentRecovery';

interface CopySession {
  recoverySnapshot(path: string): { content: string; version: number } | null;
}
interface CopyWriter {
  saveDocumentAs(path: string, content: string, utf8Bom: boolean):
    Promise<{ fileId: string; revision: string }>;
}
export interface ConflictCopyResult {
  sourceFileId: string;
  destinationFileId: string;
  content: string;
  revision: string;
  sourceVersion: number;
  canRetireSource: boolean;
}

export async function saveConflictCopy(session: CopySession, comparison: DocumentComparison,
    destination: string, writer: CopyWriter, isCurrent: () => boolean): Promise<ConflictCopyResult> {
  const source = session.recoverySnapshot(comparison.fileId);
  if (!source || source.version !== comparison.localVersion || !isCurrent())
    throw new Error('STALE_DOCUMENT');
  const saved = await writer.saveDocumentAs(destination, source.content, comparison.diskUtf8Bom);
  if (saved.fileId !== destination) throw new Error('UNCONFIRMED_COMMIT');
  const latest = session.recoverySnapshot(comparison.fileId);
  return {
    sourceFileId: comparison.fileId,
    destinationFileId: destination,
    content: source.content,
    revision: saved.revision,
    sourceVersion: source.version,
    canRetireSource: Boolean(isCurrent() && latest && latest.version === source.version),
  };
}
