import { ActionMenu } from "../../shared/ActionMenu";
import { Icon } from "../../shared/Icon";
import type { LayoutPreset } from "./useWorkbenchLayout";

export function WorkbenchHeader(props: {
  projectName: string; local: boolean; busy: boolean; canOpen: boolean; canExport: boolean;
  recent: { id: string; name: string }[]; preset: LayoutPreset; preview: boolean;
  onOpen(id?: string): void; onSave(): void; onExport(): void; onRefresh(): void;
  onSettings(): void; onHelp(): void; onPreset(value: LayoutPreset): void; onPreview(value: boolean): void;
}) {
  return <header className="topbar">
    <button className="brand" onClick={props.onHelp} title="关于 LightOverLeaf">
      <Icon name="leaf" size={23} /><span>LightOverLeaf</span>
    </button>
    <ActionMenu title="项目菜单" label={<>项目 <span className="menu-chevron">⌄</span></>}>
      <button disabled={!props.canOpen} onClick={() => props.onOpen()}><Icon name="folder" />打开项目…</button>
      <div className="menu-label">最近项目</div>
      {props.recent.length ? props.recent.map(item => <button key={item.id} disabled={!props.canOpen}
        title={item.name} onClick={() => props.onOpen(item.id)}><span className="truncate">{item.name}</span></button>)
        : <span className="menu-empty">暂无最近项目</span>}
      <hr />
      <button disabled={!props.canExport} onClick={props.onExport}>导出草稿…</button>
      <button disabled={!props.local || props.busy} onClick={props.onRefresh}>刷新项目文件</button>
    </ActionMenu>
    <div className="project-title" title={props.projectName}>
      <span className="truncate">{props.projectName}</span><span className="draft-badge">{props.local ? "本地" : "草稿"}</span>
    </div>
    <div className="header-primary">
      <button className="header-action open-project" disabled={!props.canOpen} onClick={() => props.onOpen()}>
        <Icon name="folder" size={15} /><span>{props.busy ? "正在打开…" : "打开项目"}</span>
      </button>
      <button className="header-action" onClick={props.onSave} title={props.local ? "保存文件 (Ctrl+S)" : "缓存草稿 (Ctrl+S)"}>
        <Icon name="save" size={15} /><span>保存</span>
      </button>
    </div>
    <div className="topbar-right">
      <ActionMenu className="align-right" title="工作区布局" label={<><Icon name="split" size={15} /><span>布局</span><span className="menu-chevron">⌄</span></>}>
        <div className="menu-label">工作区布局</div>
        {([["edit", "编辑优先"], ["split", "对照编辑"], ["read", "阅读优先"]] as const).map(([value, label]) =>
          <button key={value} aria-pressed={props.preset === value && props.preview} onClick={() => props.onPreset(value)}>
            <Icon name={value === "edit" ? "code" : value === "read" ? "pdf" : "split"} />{label}
            {props.preset === value && props.preview && <Icon name="check" size={13} />}
          </button>)}
        <hr />
        <button onClick={() => props.onPreview(!props.preview)}>{props.preview ? "隐藏 PDF 面板" : "显示 PDF 面板"}</button>
      </ActionMenu>
      <button className="tool-button" onClick={props.onSettings} title="设置" aria-label="设置"><Icon name="settings" /></button>
      <button className="tool-button header-help" onClick={props.onHelp} title="帮助与快捷键" aria-label="帮助与快捷键"><Icon name="info" /></button>
    </div>
  </header>;
}
