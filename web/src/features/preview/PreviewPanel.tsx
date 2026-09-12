import { useState } from "react";
import { Icon } from "../../shared/Icon";
export function PreviewPanel() {
  const [tab, setTab] = useState<"preview" | "log">("preview");
  return <section className="preview-panel" aria-label="PDF 预览面板">
    <div className="preview-toolbar">
      <button className="compile-button" disabled title="TeX 编译服务尚未接入"><Icon name="play" size={12} /> 重新编译 <span>⌄</span></button>
      <div className="preview-tabs" role="tablist" aria-label="预览视图">
        <button role="tab" aria-selected={tab === "preview"} onClick={() => setTab("preview")}>PDF 预览</button>
        <button role="tab" aria-selected={tab === "log"} onClick={() => setTab("log")}>日志</button>
      </div>
    </div>
    {tab === "preview" ? <div className="preview-stage">
      <div className="preview-empty">
        <div className="paper-icon"><Icon name="pdf" size={34} /></div>
        <span className="small-label">PDF PREVIEW</span>
        <h2>让想法，成为文档。</h2>
        <p>编译后的 PDF 将显示在这里。<br />当前可以编辑与缓存草稿，编译服务尚未接入。</p>
        <div className="pipeline"><span><i />编辑源码</span><span className="pipeline-line" /><span className="inactive"><i />本地编译</span><span className="pipeline-line" /><span className="inactive"><i />PDF 预览</span></div>
        <div className="preview-note"><Icon name="info" size={14} /><span>不会上传内容，也不会自动下载 TeX。</span></div>
      </div>
      <div className="preview-bottom"><span>尚无编译产物</span><span>— / —</span><span>100%</span></div>
    </div> : <div className="build-log" role="tabpanel"><span className="small-label">编译日志</span><p>尚未创建编译任务。</p>
      <p className="muted">当前版本未接入 Build 服务。草稿缓存成功不表示源文件已保存，也不表示编译成功。</p></div>}
  </section>;
}

