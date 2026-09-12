import { useCallback, useEffect, useRef, useState } from "react";
import { EditorSession } from "../features/editor";
import { BrowserDraftRepository, CheckpointWriter } from "../features/session";
import { starter } from "./starter";
export type CacheStatus = "loading" | "pending" | "saving" | "saved" | "error";
export function useDraftWorkspace() {
  const [session, setSession] = useState<EditorSession | null>(null);
  const [status, setStatus] = useState<CacheStatus>("loading");
  const [error, setError] = useState("");
  const writer = useRef<CheckpointWriter | null>(null);
  const saveAction = useRef<() => void>(() => {});
  useEffect(() => {
    let disposed = false;
    let workspace: EditorSession | null = null;
    let release: (() => void) | undefined;
    let unsubscribe: (() => void) | undefined;
    let timer: ReturnType<typeof setTimeout> | undefined;
    const repository = new BrowserDraftRepository();
    writer.current = new CheckpointWriter(repository);
    let queuedRevision = -1;
    const save = () => {
      clearTimeout(timer);
      if (disposed || !workspace) return;
      const capture = workspace.capture();
      queuedRevision = capture.revision;
      setStatus("saving");
      void writer.current!.save(capture.snapshot).then(() => {
        if (disposed || !workspace) return;
        workspace.acknowledge(capture.versions);
        if (workspace.getView().revision === capture.revision) { setStatus("saved"); setError(""); }
      }).catch(() => {
        if (!disposed) { setStatus("error"); setError("草稿缓存失败。可能已达 100 万字符上限，或存储空间不足；请勿关闭应用。"); }
      });
    };
    saveAction.current = save;
    void repository.acquire().then(async unlock => {
      release = unlock;
      if (disposed) { unlock(); return null; }
      return repository.load();
    }).then(snapshot => {
      if (disposed) return;
      workspace = new EditorSession(snapshot ?? structuredClone(starter));
      setSession(workspace);
      unsubscribe = workspace.subscribe(() => {
        if (!workspace || workspace.getView().revision === queuedRevision) return;
        setStatus("pending");
        clearTimeout(timer);
        timer = setTimeout(save, 600);
      });
      save();
    }).catch(() => {
      if (!disposed) {
        setStatus("error");
        setError("无法读取草稿缓存。为保护已有内容，未自动覆盖；请关闭其他窗口后重试。");
      }
    });
    return () => { disposed = true; clearTimeout(timer); unsubscribe?.(); workspace?.dispose(); release?.(); };
  }, []);
  const save = useCallback(() => saveAction.current(), []);
  return { session, status, error, save };
}

