import { useEffect, useRef, useState } from "react";
import { Icon } from "../../shared/Icon";
import { getDocument, GlobalWorkerOptions, type PDFDocumentProxy, type PDFDocumentLoadingTask } from "pdfjs-dist";
import workerUrl from "pdfjs-dist/build/pdf.worker.min.mjs?url";
import { previewPresentation, type PreviewState } from "./presentation";
GlobalWorkerOptions.workerSrc = workerUrl;
export interface BuildView {
  state: PreviewState;
  output: string;
  diagnostics: { fileId: string; line: number; message: string; severity?: string }[];
  artifactId?: string;
  pdf?: Uint8Array;
  syncTexAvailable?: boolean;
  generation?: number;
  phase?: "snapshot" | "detect" | "compile" | "artifact" | "render" | "complete";
  candidate?: { generation: number; artifactId: string; pdf: Uint8Array; syncTexAvailable: boolean };
}
export interface PdfTarget { page: number; x: number; y: number; revision: number }
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

function PdfCanvas({ data, onReverse, onCommit, onReject, target, zoom, onZoomChange }: {
  data: Uint8Array;
  onReverse?: (page: number, x: number, y: number) => void;
  onCommit?: () => void;
  onReject?: (message: string) => void;
  target?: PdfTarget | null;
  zoom: number;
  onZoomChange?(zoom: number): void;
}) {
  const canvas = useRef<HTMLCanvasElement>(null);
  const marker = useRef<HTMLSpanElement>(null);
  const committedDocument = useRef<PDFDocumentProxy | null>(null);
  const committedTask = useRef<PDFDocumentLoadingTask | null>(null);
  const commitCallback = useRef(onCommit);
  const rejectCallback = useRef(onReject);
  commitCallback.current = onCommit;
  rejectCallback.current = onReject;
  const [document, setDocument] = useState<PDFDocumentProxy | null>(null);
  const [page, setPage] = useState(1);
  const [rendered, setRendered] = useState(0);
  const [error, setError] = useState("");
  const [loading, setLoading] = useState(false);
  const scale = zoom / 100;

  useEffect(() => {
    const task = getDocument({ data: data.slice() });
    let active = true;
    let committed = false;
    let candidateRender: { cancel(): void; promise: Promise<void> } | undefined;
    setLoading(true);
    void task.promise.then(async candidate => {
      const firstPage = await candidate.getPage(1);
      const viewport = firstPage.getViewport({ scale: 1 });
      const offscreen = globalThis.document.createElement("canvas");
      offscreen.width = Math.ceil(viewport.width);
      offscreen.height = Math.ceil(viewport.height);
      const context = offscreen.getContext("2d");
      if (!context) throw new Error("PDF_CANVAS_UNAVAILABLE");
      candidateRender = firstPage.render({ canvas: offscreen, canvasContext: context, viewport });
      await candidateRender.promise;
      if (!active || !canvas.current) { await task.destroy(); return; }
      const visible = canvas.current;
      const visibleContext = visible.getContext("2d");
      if (!visibleContext) { await task.destroy(); throw new Error("PDF_CANVAS_UNAVAILABLE"); }
      visible.width = offscreen.width;
      visible.height = offscreen.height;
      visibleContext.drawImage(offscreen, 0, 0);
      const previousTask = committedTask.current;
      committedDocument.current = candidate;
      committedTask.current = task;
      committed = true;
      setDocument(candidate);
      setPage(1);
      setError("");
      setLoading(false);
      setRendered(value => value + 1);
      commitCallback.current?.();
      if (previousTask && previousTask !== task) void previousTask.destroy();
    }).catch(failure => {
      if (!active || (failure as Error).name === "RenderingCancelledException") return;
      const message = (failure as Error).message || "PDF_RENDER_FAILED";
      setLoading(false);
      setError(message);
      rejectCallback.current?.(message);
    });
    return () => {
      active = false;
      candidateRender?.cancel();
      if (!committed) void task.destroy();
    };
  }, [data]);

  useEffect(() => () => { if (committedTask.current) void committedTask.current.destroy(); }, []);
  useEffect(() => {
    if (target && document) setPage(Math.min(document.numPages, Math.max(1, target.page)));
  }, [document, target]);
  useEffect(() => {
    if (!document || !canvas.current) return;
    let cancelled = false;
    let render: { cancel(): void; promise: Promise<void> } | undefined;
    void document.getPage(page).then(async pdfPage => {
      if (cancelled || committedDocument.current !== document || !canvas.current) return;
      const viewport = pdfPage.getViewport({ scale });
      const offscreen = globalThis.document.createElement("canvas");
      offscreen.width = Math.ceil(viewport.width);
      offscreen.height = Math.ceil(viewport.height);
      const context = offscreen.getContext("2d");
      if (!context) throw new Error("PDF_CANVAS_UNAVAILABLE");
      render = pdfPage.render({ canvas: offscreen, canvasContext: context, viewport });
      await render.promise;
      if (cancelled || committedDocument.current !== document || !canvas.current) return;
      const visibleContext = canvas.current.getContext("2d");
      if (!visibleContext) throw new Error("PDF_CANVAS_UNAVAILABLE");
      canvas.current.width = offscreen.width;
      canvas.current.height = offscreen.height;
      visibleContext.drawImage(offscreen, 0, 0);
      setError("");
      setRendered(value => value + 1);
    }).catch(failure => {
      if (!cancelled && (failure as Error).name !== "RenderingCancelledException")
        setError((failure as Error).message || "PDF_RENDER_FAILED");
    });
    return () => { cancelled = true; render?.cancel(); };
  }, [document, page, scale]);
  useEffect(() => {
    if (target?.page === page) marker.current?.scrollIntoView({ block: "center", inline: "center" });
  }, [page, rendered, scale, target]);

  if (!document && error) return <div className="preview-empty"><p role="alert">PDF.js 无法显示此产物：{error}</p></div>;
  return <div className="pdf-viewer">
    {(loading || error) && <div className={"pdf-render-status " + (error ? "error" : "loading")} role="status">
      {error ? `新预览渲染失败，继续显示上一份成功 PDF：${error}` : "正在验证并渲染新 PDF，当前预览保持不变…"}
    </div>}
    <div className="pdf-controls">
      <button disabled={page <= 1} onClick={() => setPage(value => value - 1)}>上一页</button>
      <span>{page} / {document?.numPages ?? "—"}</span>
      <button disabled={!document || page >= document.numPages} onClick={() => setPage(value => value + 1)}>下一页</button>
      <span className="zoom-controls"><button disabled={zoom <= 50} onClick={() => onZoomChange?.(Math.max(50, zoom - 25))}>−</button>
        <button title="恢复 100%" onClick={() => onZoomChange?.(100)}>{zoom}%</button>
        <button disabled={zoom >= 300} onClick={() => onZoomChange?.(Math.min(300, zoom + 25))}>＋</button></span>
    </div><div className="pdf-scroll"><div className="pdf-page"><canvas ref={canvas} aria-label={"PDF 第 " + page + " 页"}
      title={onReverse ? "双击跳转到源码" : undefined}
      onDoubleClick={event => { const rect = event.currentTarget.getBoundingClientRect();
        const ratioX = rect.width > 0 ? event.currentTarget.width / rect.width / scale : 1;
        const ratioY = rect.height > 0 ? event.currentTarget.height / rect.height / scale : 1;
        onReverse?.(page, (event.clientX - rect.left) * ratioX, (event.clientY - rect.top) * ratioY); }} />
      {target?.page === page && <span ref={marker} className="synctex-marker" aria-label="源码定位位置"
        style={{ left: target.x * scale, top: target.y * scale }} />}</div></div></div>;
}