import { Icon } from "../../shared/Icon";
import { previewPresentation } from "./presentation";
import type { BuildView } from "./model";

export function BuildLogPanel({ build, open, onClose, onDiagnostic }: {
  build: BuildView; open: boolean; onClose(): void; onDiagnostic(fileId: string, line: number): void;
}) {
  if (!open) return null;
  const errors = build.diagnostics.filter(item => item.severity !== "info" && item.severity !== "warning").length;
  const warnings = build.diagnostics.filter(item => item.severity === "warning").length;
  return <section className="build-console" aria-label="编译日志与诊断">
    <header className="console-heading"><Icon name="terminal" size={15} /><strong>编译日志</strong>
      {errors > 0 && <span className="diagnostic-count error">{errors} 条错误</span>}
      {warnings > 0 && <span className="diagnostic-count">{warnings} 条警告</span>}
      <span className="console-summary">{previewPresentation(build.state).summary}</span>
      <button className="tool-button" onClick={onClose} title="收起日志" aria-label="收起日志"><Icon name="close" size={14} /></button>
    </header>
    <div className="build-log">
      {build.diagnostics.map((item, index) => {
        const message = item.message === "No citations requested; the bibliography is intentionally empty."
          ? "正文尚未引用文献，参考文献列表暂为空（仅显示已引用条目）。" : item.message;
        return item.line > 0 ? <button className={"diagnostic-row " + (item.severity ?? "error")}
          key={index} onClick={() => onDiagnostic(item.fileId, item.line)}>
          <span>{item.fileId}:{item.line}</span> {message}</button>
          : <p className="diagnostic-message" key={index}>{message}</p>;
      })}
      <pre>{build.output || "暂无编译输出。点击「编译」后，日志会显示在这里。"}</pre>
      <details className="build-details"><summary>任务详情</summary>
        <p>Generation：{build.generation ?? "—"} · 阶段：{build.phase ?? "—"}</p>
      </details>
    </div>
  </section>;
}
