import { validateSnapshot, type DraftRepository, type DraftSnapshot } from "./checkpoint";

/** Origin-scoped recovery cache. This is not a native project-file store. */
export class BrowserDraftRepository implements DraftRepository {
  async acquire(): Promise<() => void> {
    return new Promise((resolve, reject) => {
      if (!navigator.locks) { reject(new Error("DRAFT_LOCK_UNAVAILABLE")); return; }
      void navigator.locks.request("lightoverleaf-draft-writer", { ifAvailable: true }, async lock => {
        if (!lock) { reject(new Error("DRAFT_ALREADY_OPEN")); return; }
        await new Promise<void>(release => { resolve(release); });
      }).catch(reject);
    });
  }
  private async open(): Promise<IDBDatabase> {
    return new Promise((resolve, reject) => {
      const request = indexedDB.open("lightoverleaf-drafts", 1);
      let abandoned = false;
      const timer = setTimeout(() => { abandoned = true; reject(new Error("DRAFT_STORAGE_TIMEOUT")); }, 5000);
      request.onupgradeneeded = () => { request.result.createObjectStore("checkpoints"); };
      request.onsuccess = () => {
        clearTimeout(timer);
        if (abandoned) request.result.close();
        else resolve(request.result);
      };
      request.onerror = () => { clearTimeout(timer); reject(new Error("DRAFT_STORAGE_UNAVAILABLE")); };
      request.onblocked = () => { clearTimeout(timer); abandoned = true; reject(new Error("DRAFT_STORAGE_BLOCKED")); };
    });
  }
  async load(): Promise<DraftSnapshot | null> {
    const database = await this.open();
    try {
      return await new Promise((resolve, reject) => {
        const transaction = database.transaction("checkpoints", "readonly");
        const request = transaction.objectStore("checkpoints").get("current");
        transaction.oncomplete = () => {
          const value: unknown = request.result;
          if (value === undefined) resolve(null);
          else if (validateSnapshot(value)) resolve(value);
          else reject(new Error("DRAFT_CACHE_INVALID"));
        };
        transaction.onabort = () => reject(new Error("DRAFT_READ_FAILED"));
      });
    } finally { database.close(); }
  }
  async save(snapshot: DraftSnapshot): Promise<void> {
    if (!validateSnapshot(snapshot)) throw new Error("INVALID_DRAFT");
    const database = await this.open();
    try {
      await new Promise<void>((resolve, reject) => {
        const transaction = database.transaction("checkpoints", "readwrite");
        transaction.objectStore("checkpoints").put(snapshot, "current");
        transaction.oncomplete = () => resolve();
        transaction.onabort = () => reject(new Error("DRAFT_WRITE_FAILED"));
      });
    } finally { database.close(); }
  }
}

