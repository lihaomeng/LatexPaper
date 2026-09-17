import { useState } from "react";
import { Icon } from "../../shared/Icon";
import { previewPresentation } from "./presentation";
import { PdfCanvas } from "./PdfCanvas";
import type { BuildView, PdfTarget } from "./model";
export type { BuildView, PdfTarget } from "./model";
export function PreviewPanel({ build, canCompile, onCompile, onCancel, onConfigure,
  onDiagnostic, onReverse, onPreviewCommit, onPreviewReject, target,
  zoom = 125, onZoomChange }: {
  build: BuildView; canCompile: boolean; onCompile(): void; onCancel(): void;
  onConfigure?(): void;
  onDiagnostic(fileId: string, line: number): void;
  onReverse?(page: number, x: number, y: number): void;
  onPreviewCommit?(generation: number): void;
  onPreviewReject?(generation: number, message: string): void;
  target?: PdfTarget | null;
  zoom?: number;
  onZoomChange?(zoom: number): void;
}) {
  const [tab, setTab] = useState<"preview" | "log">("preview");
  const presentation = previewPresentation(build.state);
  const { preparing, running } = presentation;
  const summary = build.state === "failed" && build.phase === "render"
    ? "PDF 已生成，但预览失败" : presentation.summary;
  return <section className="preview-panel" aria-label="PDF 预览面板">
    <div className="preview-toolbar">
      <button className="compile-button" disabled={preparing || (!canCompile && !running)}
        onClick={running ? onCancel : onCompile}
        title={preparing ? "正在创建隔离编译快照" : running ? "取消当前编译" : canCompile ?
          "编译当前 Monaco 内容；仅导出项目时选择目录" : "请先打开一个 LaTeX 文件"}>
        <Icon name="play" size={12} /> {presentation.action} <span>⌄</span></button>
      <div className="preview-tabs" role="tablist" aria-label="预览视图">
        <button role="tab" aria-selected={tab === "preview"} onClick={() => setTab("preview")}>PDF 预览</button>
        <button role="tab" aria-selected={tab === "log"} onClick={() => setTab("log")}>日志</button>
      </div>
    </div>
    {tab === "preview" ? <div className="preview-stage">
      {(build.candidate?.pdf ?? build.pdf) ? <PdfCanvas data={(build.candidate?.pdf ?? build.pdf)!} onReverse={(build.candidate?.syncTexAvailable ?? build.syncTexAvailable) ? onReverse : undefined}
        onCommit={build.candidate ? () => onPreviewCommit?.(build.candidate!.generation) : undefined}
        onReject={build.candidate ? message => onPreviewReject?.(build.candidate!.generation, message) : undefined}
        target={target} zoom={zoom} onZoomChange={onZoomChange} /> : <div className="preview-empty">
        <div className="paper-icon"><Icon name="pdf" size={34} /></div>
        <span className="small-label">PDF PREVIEW</span>
        <h2>让想法，成为文档。</h2>
        <p>{summary}。<br />草稿直接在应用隔离 Snapshot 中编译；只有导出项目时才选择目录。</p>
        {build.state === "unavailable" && onConfigure &&
          <button className="preview-action" onClick={onConfigure}>配置 TeX 工具链</button>}
        <div className="pipeline"><span><i />编辑源码</span><span className="pipeline-line" /><span className={build.state === "idle" ? "inactive" : ""}><i />本地编译</span><span className="pipeline-line" /><span className="inactive"><i />PDF 预览</span></div>
        <div className="preview-note"><Icon name="info" size={14} /><span>不会上传内容，也不会自动下载 TeX。</span></div>
      </div>}
      <div className="preview-bottom"><span>{summary}{build.phase ? ` · ${build.phase}` : ""}</span><span>{build.generation ? `Generation ${build.generation}` : "— / —"}</span><span>{zoom}%</span></div>
    </div> : <div className="build-log" role="tabpanel"><span className="small-label">编译日志</span><p>{summary}{build.phase ? ` · 阶段 ${build.phase}` : ""}{build.pdf && build.state !== "succeeded" ? " · 继续显示上一份成功预览" : ""}</p>
      {build.diagnostics.map((item, index) => item.line > 0
        ? <button className="diagnostic-row" key={`${item.fileId}:${item.line}:${index}`}
          onClick={() => onDiagnostic(item.fileId, item.line)}>{item.fileId}:{item.line} {item.message}</button>
        : <p key={`${item.fileId}:${index}`}>
          {item.severity === "info" ? "说明" : item.severity === "warning" ? "警告" : "诊断"}：
          {item.message === "No citations requested; the bibliography is intentionally empty."
            ? "正文尚未引用文献，参考文献列表暂为空（仅显示已引用条目）。" : item.message}
        </p>)}
      <pre>{build.output || "尚无日志。"}</pre>
      <p className="muted">编译使用不可变项目 Snapshot；不启用 Shell Escape，也不会自动联网下载宏包。</p></div>}
  </section>;
}
