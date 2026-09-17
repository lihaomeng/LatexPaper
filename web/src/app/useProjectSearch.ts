import { useEffect, useRef, useState } from "react";
import type { AuthoringRpcClient } from "../native-api";
import type { EditorSession } from "../features/editor";

type Hit = Awaited<ReturnType<AuthoringRpcClient["search"]>>["hits"][number];
export function useProjectSearch(client: AuthoringRpcClient, session: EditorSession | null) {
  const [query, setQuery] = useState("");
  const [hits, setHits] = useState<Hit[]>([]);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");
  const request = useRef<AbortController | null>(null);
  const owner = useRef(session);
  owner.current = session;
  const reset = () => {
    request.current?.abort(); request.current = null;
    setQuery(""); setHits([]); setBusy(false); setError("");
  };
  useEffect(() => {
    reset();
    return () => { request.current?.abort(); request.current = null; };
  }, [session]);
  const run = async () => {
    if (!session || request.current || !query.trim()) return;
    const pending = new AbortController();
    request.current = pending;
    const current = () => request.current === pending && owner.current === session;
    setBusy(true); setError("");
    try {
      const result = await client.search(query, false, 200, pending.signal);
      if (!current()) return;
      setHits(result.hits);
      if (result.truncated) setError("结果已达到 200 条上限，请缩小查询范围。");
    } catch (failure) {
      if (current() && !pending.signal.aborted) setError("项目搜索失败：" + (failure as Error).message);
    } finally {
      if (current()) { request.current = null; setBusy(false); }
    }
  };
  return { query, setQuery, hits, busy, error, run, reset };
}
