import { useEffect, useRef, useState } from "react";
import { previewPresentation } from "./presentation";
import type { BuildView } from "./model";
export function useBuildFeedback(build: BuildView) {
  const busy = ["preparing", "detecting", "running", "rendering"].includes(build.state);
  const started = useRef<number | null>(null);
  const [elapsed, setElapsed] = useState(0);
  useEffect(() => {
    if (!busy) {
      if (started.current !== null) setElapsed(Date.now() - started.current);
      started.current = null;
      return;
    }
    started.current = Date.now();
    setElapsed(0);
    const timer = window.setInterval(() => setElapsed(Date.now() - started.current!), 250);
    return () => window.clearInterval(timer);
  }, [busy]);
  const failed = ["failed", "timedOut", "unavailable"].includes(build.state);
  const summary = build.state === "failed" && build.phase === "render"
    ? "PDF 预览失败" : previewPresentation(build.state).summary;
  return { busy, failed, summary, elapsedLabel: elapsed > 0 ? (elapsed / 1000).toFixed(1) + " 秒" : "" };
}
