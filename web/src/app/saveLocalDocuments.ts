interface SaveSession {
  getView(): { files: { path: string; dirty: boolean }[] };
  capture(): { snapshot: { files: { path: string; content: string }[] }; versions: Map<string, number> };
  acknowledge(versions: Map<string, number>, contents?: Map<string, string>): void;
}
interface DocumentWriter {
  saveDocument(path: string, content: string, revision: string): Promise<{ revision: string }>;
}
export class LocalDocumentSaveError extends Error {
  constructor(readonly fileId: string, cause: unknown) {
    super(cause instanceof Error ? cause.message : 'SAVE_FAILED', { cause });
    this.name = 'LocalDocumentSaveError';
  }
}

// Capture one bounded batch. Edits made during I/O remain dirty for the next batch.
export async function saveLocalDocuments(session: SaveSession, revisions: Map<string, string>,
    writer: DocumentWriter, isCurrent: () => boolean): Promise<void> {
  const dirty = new Set(session.getView().files.filter(file => file.dirty).map(file => file.path));
  const capture = session.capture();
  for (const file of capture.snapshot.files) {
    if (!isCurrent()) return;
    if (!dirty.has(file.path)) continue;
    const revision = revisions.get(file.path);
    const version = capture.versions.get(file.path);
    if (!revision || version === undefined) throw new Error('MISSING_REVISION');
    const saved = await writer.saveDocument(file.path, file.content, revision)
      .catch(cause => { throw new LocalDocumentSaveError(file.path, cause); });
    if (!isCurrent()) return;
    revisions.set(file.path, saved.revision);
    session.acknowledge(new Map([[file.path, version]]), new Map([[file.path, file.content]]));
  }
}
