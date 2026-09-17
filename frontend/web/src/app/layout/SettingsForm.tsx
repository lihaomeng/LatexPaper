import type { Dispatch, SetStateAction } from "react";
import type { Preferences } from "../../native-api";
export function SettingsForm({ preferencesDraft, setPreferencesDraft, settingsBusy, settingsError,
  recentWorkspaces, projectNames, workspaceBusy, sessionReady, onSave, onClose, onOpenWorkspace }: {
  preferencesDraft: Preferences; setPreferencesDraft: Dispatch<SetStateAction<Preferences>>;
  settingsBusy: boolean; settingsError: string; recentWorkspaces: string[];
  projectNames: Record<string, string>; workspaceBusy: boolean; sessionReady: boolean;
  onSave(): void; onClose(): void; onOpenWorkspace(id: string): void;
}) {
  return <form onSubmit={event => { event.preventDefault(); onSave(); }}>
          <p><b>编译工具链：</b>内置 MiKTeX Runtime（Full 版固定使用，不读取系统 TeX）。</p>
          <p>编译引擎：pdfLaTeX（内置）。旧版引擎设置已统一迁移；不自动处理 BibTeX/Biber。</p>
          <label htmlFor="tex-timeout">编译超时（毫秒）</label>
          <input id="tex-timeout" type="number" min={1000} max={300000} value={preferencesDraft.timeoutMs}
            disabled={settingsBusy} onChange={event => setPreferencesDraft(current => ({ ...current,
              timeoutMs: Number(event.target.value) }))} />
          <label htmlFor="compile-mode">编译触发方式</label>
          <select id="compile-mode" value={preferencesDraft.compileMode} disabled={settingsBusy}
            onChange={event => setPreferencesDraft(current => ({ ...current,
              compileMode: event.target.value as Preferences['compileMode'] }))}>
            <option value="live">实时预览（停止输入 900 ms）</option>
            <option value="onSave">仅保存后编译</option>
            <option value="manual">仅手动编译</option>
          </select>
          <p>重启后自动恢复上次项目及标签；也可以通过“项目 → 最近项目”打开其他已授权目录。路径保存在本机 SQLite，不向前端开放任意路径访问。</p>
          <div className="recent-workspaces"><b>最近项目</b>
            {recentWorkspaces.map(root => <button type="button" key={root} disabled={workspaceBusy || !sessionReady} onClick={() => onOpenWorkspace(root)}>{projectNames[root] ?? root}</button>)}
            {!recentWorkspaces.length && <span className="muted">暂无记录</span>}
          </div>
          {settingsError && <p className="error" role="alert">{settingsError}</p>}
          <div className="modal-actions"><button type="button" onClick={onClose}>取消</button>
            <button className="primary" type="submit" disabled={settingsBusy}>{settingsBusy ? "保存中…" : "保存设置"}</button></div>
        </form>;
}
