export interface DraftFile { path: string; content: string }
export interface DraftSnapshot { version: 1; files: DraftFile[]; open: string[]; active: string | null }
export interface DraftRepository {
  load(): Promise<DraftSnapshot | null>;
  save(snapshot: DraftSnapshot): Promise<void>;
}
export const MAX_FILES = 32;
export const MAX_CHARACTERS = 1_000_000;
export function validDraftPath(path: string): boolean {
  return path.length > 0 && path.length <= 160 && !/[\\:<>"|?*]/.test(path) &&
    ![...path].some(character => character.charCodeAt(0) < 32) &&
    path.split("/").every(part => part.length > 0 && part !== "." && part !== ".." && !/[. ]$/.test(part)) &&
    /\.(tex|bib|md|txt)$/i.test(path);
}
export function validateSnapshot(value: unknown): value is DraftSnapshot {
  if (!value || typeof value !== "object") return false;
  const data = value as Record<string, unknown>;
  if (Object.keys(data).sort().join(",") !== "active,files,open,version" || data.version !== 1 ||
      !Array.isArray(data.files) || data.files.length < 1 || data.files.length > MAX_FILES ||
      !Array.isArray(data.open) || data.open.length > MAX_FILES) return false;
  const paths = new Set<string>();
  let total = 0;
  for (const file of data.files) {
    if (!file || typeof file !== "object" || Object.keys(file).sort().join(",") !== "content,path" ||
        typeof file.path !== "string" || !validDraftPath(file.path) || paths.has(file.path.toLowerCase()) ||
        typeof file.content !== "string") return false;
    total += file.content.length;
    if (total > MAX_CHARACTERS) return false;
    paths.add(file.path.toLowerCase());
  }
  for (const path of paths) {
    if (path.split("/").slice(0, -1).some((_, index) => paths.has(path.split("/").slice(0, index + 1).join("/")))) return false;
  }
  const exactPaths = new Set(data.files.map(file => file.path));
  if (!data.open.every(path => typeof path === "string" && exactPaths.has(path)) ||
      new Set(data.open).size !== data.open.length) return false;
  return data.active === null ? data.open.length === 0 : typeof data.active === "string" && data.open.includes(data.active);
}

/** Serial writes prevent an old acknowledgement from overwriting a newer checkpoint. */
export class CheckpointWriter {
  private tail: Promise<void> = Promise.resolve();
  constructor(private repository: DraftRepository) {}
  save(snapshot: DraftSnapshot): Promise<void> {
    const captured = structuredClone(snapshot);
    if (!validateSnapshot(captured)) return Promise.reject(new Error("INVALID_DRAFT"));
    const next = this.tail.then(() => this.repository.save(captured));
    this.tail = next.catch(() => {});
    return next;
  }
}
