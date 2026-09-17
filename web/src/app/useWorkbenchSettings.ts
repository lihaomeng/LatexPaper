import { useEffect, useRef, useState } from "react";
import type { Preferences, PreferencesSessionRpcClient } from "../native-api";
const defaults: Preferences = { texRoot: "", engine: "pdflatex", timeoutMs: 120000, compileMode: "live" };
export function useWorkbenchSettings(client: PreferencesSessionRpcClient) {
  const [preferences, setPreferences] = useState(defaults);
  const [draft, setDraft] = useState(defaults);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");
  const pending = useRef<AbortController | null>(null);
  useEffect(() => () => { pending.current?.abort(); pending.current = null; }, []);
  const restore = (value: Preferences) => { setPreferences(value); setDraft(value); };
  const begin = () => { setDraft(preferences); setError(""); };
  const save = async () => {
    if (pending.current) return null;
    if (!Number.isInteger(draft.timeoutMs) || draft.timeoutMs < 1000 || draft.timeoutMs > 300000) {
      setError("编译超时必须为 1000 至 300000 之间的整数。"); return null;
    }
    const request = new AbortController(); pending.current = request;
    setBusy(true); setError("");
    try {
      const saved = await client.updatePreferences(draft, request.signal);
      if (pending.current !== request) return null;
      restore(saved); return saved;
    } catch (failure) {
      if (pending.current === request) setError("保存设置失败：" + (failure as Error).message);
      return null;
    } finally {
      if (pending.current === request) { pending.current = null; setBusy(false); }
    }
  };
  return { preferences, draft, setDraft, busy, error, setError, restore, begin, save };
}
