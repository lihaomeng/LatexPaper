import { useEffect, useRef, useState } from "react";
import { Icon } from "../../shared/Icon";
import { getDocument, GlobalWorkerOptions, type PDFDocumentProxy } from "pdfjs-dist";
import workerUrl from "pdfjs-dist/build/pdf.worker.min.mjs?url";
GlobalWorkerOptions.workerSrc = workerUrl;
export interface BuildView {
  state: "idle" | "detecting" | "running" | "succeeded" | "failed" | "cancelled" | "timedOut" | "unavailable";
  output: string;
  diagnostics: { fileId: string; line: number; message: string }[];
  artifactId?: string;
  pdf?: Uint8Array;
  syncTexAvailable?: boolean;
}
export interface PdfTarget { page: number; x: number; y: number; revision: number }
export function PreviewPanel({ build, canCompile, onCompile, onCancel, onDiagnostic, onReverse, target,
  zoom = 125, onZoomChange }: {
  build: BuildView; canCompile: boolean; onCompile(): void; onCancel(): void;
  onDiagnostic(fileId: string, line: number): void;
  onReverse?(page: number, x: number, y: number): void;
  target?: PdfTarget | null;
  zoom?: number;
  onZoomChange?(zoom: number): void;
}) {
  const [tab, setTab] = useState<"preview" | "log">("preview");
  const running = build.state === "detecting" || build.state === "running";
  const summary = build.state === "idle" ? "尚未创建编译任务" :
    build.state === "detecting" ? "正在检测 TeX 工具链" : build.state === "running" ? "正在本地编译" :
    build.state === "succeeded" ? "编译成功" : build.state === "unavailable" ? "未检测到 TeX 编译器" :
    build.state === "cancelled" ? "编译已取消" : build.state === "timedOut" ? "编译超时" : "编译失败";
  return <section className="preview-panel" aria-label="PDF 预览面板">
    <div className="preview-toolbar">
      <button className="compile-button" disabled={!canCompile && !running}
        onClick={running ? onCancel : onCompile}
        title={running ? "取消当前编译" : canCompile ? "保存修改并编译当前主文件" : "请先打开本地项目"}>
        <Icon name="play" size={12} /> {running ? "取消编译" : "重新编译"} <span>⌄</span></button>
      <div className="preview-tabs" role="tablist" aria-label="预览视图">
        <button role="tab" aria-selected={tab === "preview"} onClick={() => setTab("preview")}>PDF 预览</button>
        <button role="tab" aria-selected={tab === "log"} onClick={() => setTab("log")}>日志</button>
      </div>
    </div>
    {tab === "preview" ? <div className="preview-stage">
      {build.pdf ? <PdfCanvas data={build.pdf} onReverse={build.syncTexAvailable ? onReverse : undefined}
        target={target} zoom={zoom} onZoomChange={onZoomChange} /> : <div className="preview-empty">
        <div className="paper-icon"><Icon name="pdf" size={34} /></div>
        <span className="small-label">PDF PREVIEW</span>
        <h2>让想法，成为文档。</h2>
        <p>{summary}。<br />M5 已在隔离 Snapshot 中执行；PDF 显示由 M6 Artifact 服务接管。</p>
        <div className="pipeline"><span><i />编辑源码</span><span className="pipeline-line" /><span className={build.state === "idle" ? "inactive" : ""}><i />本地编译</span><span className="pipeline-line" /><span className="inactive"><i />PDF 预览</span></div>
        <div className="preview-note"><Icon name="info" size={14} /><span>不会上传内容，也不会自动下载 TeX。</span></div>
      </div>}
      <div className="preview-bottom"><span>{summary}</span><span>— / —</span><span>100%</span></div>
    </div> : <div className="build-log" role="tabpanel"><span className="small-label">编译日志</span><p>{summary}</p>
      {build.diagnostics.map((item, index) => <button className="diagnostic-row" key={`${item.fileId}:${item.line}:${index}`}
        onClick={() => onDiagnostic(item.fileId, item.line)}>{item.fileId}:{item.line} {item.message}</button>)}
      <pre>{build.output || "尚无日志。"}</pre>
      <p className="muted">编译使用不可变项目 Snapshot；不启用 Shell Escape，也不会自动联网下载宏包。</p></div>}
  </section>;
}

function PdfCanvas({ data, onReverse, target, zoom, onZoomChange }: {
  data: Uint8Array;
  onReverse?: (page: number, x: number, y: number) => void;
  target?: PdfTarget | null;
  zoom: number;
  onZoomChange?(zoom: number): void;
}) {
  const canvas = useRef<HTMLCanvasElement>(null);
  const marker = useRef<HTMLSpanElement>(null);
  const [document, setDocument] = useState<PDFDocumentProxy | null>(null);
  const [page, setPage] = useState(1);
  const scale = zoom / 100;
  const [rendered, setRendered] = useState(0);
  const [error, setError] = useState("");
  useEffect(() => {
    const task = getDocument({ data: data.slice() }); let active = true;
    void task.promise.then(value => { if (active) { setDocument(value); setPage(1); setError(""); } },
      failure => { if (active) setError((failure as Error).message); });
    return () => { active = false; void task.destroy(); };
  }, [data]);
  useEffect(() => {
    if (target && document) setPage(Math.min(document.numPages, Math.max(1, target.page)));
  }, [document, target]);
  useEffect(() => {
    if (!document || !canvas.current) return;
    let cancelled = false; let render: { cancel(): void; promise: Promise<void> } | undefined;
    void document.getPage(page).then(pdfPage => {
      if (cancelled || !canvas.current) return;
      const viewport = pdfPage.getViewport({ scale });
      const context = canvas.current.getContext("2d"); if (!context) return;
      canvas.current.width = viewport.width; canvas.current.height = viewport.height;
      render = pdfPage.render({ canvas: canvas.current, canvasContext: context, viewport });
      return render.promise.then(() => { if (!cancelled) setRendered(value => value + 1); });
    }).catch(failure => { if (!cancelled && (failure as Error).name !== "RenderingCancelledException") setError((failure as Error).message); });
    return () => { cancelled = true; render?.cancel(); };
  }, [document, page, scale]);
  useEffect(() => {
    if (target?.page === page) marker.current?.scrollIntoView({ block: "center", inline: "center" });
  }, [page, rendered, scale, target]);
  if (error) return <div className="preview-empty"><p role="alert">PDF.js 无法显示此产物：{error}</p></div>;
  return <div className="pdf-viewer"><div className="pdf-controls">
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
