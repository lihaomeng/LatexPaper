import { Icon } from "../../shared/Icon";
import { previewPresentation } from "./presentation";
import { PdfCanvas } from "./PdfCanvas";
import type { BuildView, PdfTarget, PdfZoomMode } from "./model";
export type { BuildView, PdfTarget } from "./model";
export function PreviewPanel({ build, onConfigure,
  onReverse, onPreviewCommit, onPreviewReject, target, zoom = 100, onZoomChange,
  zoomMode = "width", onZoomModeChange, logsOpen, onToggleLogs }: {
  build: BuildView; onConfigure?(): void;
  onReverse?(page: number, x: number, y: number): void;
  onPreviewCommit?(generation: number): void; onPreviewReject?(generation: number, message: string): void;
  target?: PdfTarget | null; zoom?: number; onZoomChange?(zoom: number): void;
  zoomMode?: PdfZoomMode; onZoomModeChange?(mode: PdfZoomMode): void;
  logsOpen?: boolean; onToggleLogs?(): void;
}) {
  const { summary } = previewPresentation(build.state);
  return <section className="preview-panel" aria-label="PDF 预览面板">
    <div className="preview-toolbar">
      <span className="preview-title"><Icon name="pdf" size={14} />PDF 预览</span>
      <div className="preview-actions">
        <button className="tool-button" aria-pressed={logsOpen} onClick={onToggleLogs}
          title="编译日志与诊断" aria-label="编译日志与诊断"><Icon name="terminal" size={16} /></button>
      </div>
    </div>
    {build.pdf && ["failed", "timedOut", "cancelled", "unavailable"].includes(build.state) &&
      <div className="pdf-render-status" role="status">本次未更新 · 显示上次成功的 PDF</div>}
    <div className="preview-stage">
      {(build.candidate?.pdf ?? build.pdf) ? <PdfCanvas data={(build.candidate?.pdf ?? build.pdf)!}
        onReverse={(build.candidate?.syncTexAvailable ?? build.syncTexAvailable) ? onReverse : undefined}
        onCommit={build.candidate ? () => onPreviewCommit?.(build.candidate!.generation) : undefined}
        onReject={build.candidate ? message => onPreviewReject?.(build.candidate!.generation, message) : undefined}
        target={target} zoom={zoom} onZoomChange={onZoomChange}
        zoomMode={zoomMode} onZoomModeChange={onZoomModeChange} /> : <div className="preview-empty">
        <div className="paper-icon"><Icon name="pdf" size={34} /></div>
        <span className="small-label">PDF PREVIEW</span>
        <h2>让想法，成为文档。</h2>
        <p>{summary}。<br />编译后在这里查看文档，与源码对照编辑。</p>
        {build.state === "unavailable" && onConfigure &&
          <button className="preview-action" onClick={onConfigure}>配置 TeX 工具链</button>}
        <div className="preview-note"><Icon name="info" size={14} /><span>本地编译 · 内容不会上传</span></div>
      </div>}
    </div>
  </section>;
}
