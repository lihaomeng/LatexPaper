import { useState, type Dispatch, type SetStateAction } from "react";
import type { Preferences } from "../../native-api";
export function SettingsForm({ preferencesDraft, setPreferencesDraft, settingsBusy, settingsError,
  onSave, onClose }: {
  preferencesDraft: Preferences; setPreferencesDraft: Dispatch<SetStateAction<Preferences>>;
  settingsBusy: boolean; settingsError: string; onSave(): void; onClose(): void;
}) {
  const [seconds, setSeconds] = useState(String(preferencesDraft.timeoutMs / 1000));
  return <form className="settings-form" onSubmit={event => { event.preventDefault(); onSave(); }}>
    <div className="settings-section-title">编译</div>
    <div className="settings-field">
      <label htmlFor="compile-mode">编译方式</label>
      <select id="compile-mode" value={preferencesDraft.compileMode} disabled={settingsBusy}
        aria-describedby="compile-mode-hint"
        onChange={event => setPreferencesDraft(current => ({ ...current,
          compileMode: event.target.value as Preferences['compileMode'] }))}>
        <option value="live">自动编译</option><option value="onSave">保存后编译</option>
        <option value="manual">手动编译</option>
      </select>
      <p id="compile-mode-hint" className="field-hint">{preferencesDraft.compileMode === "live"
        ? "停止输入约 0.9 秒后更新预览。" : preferencesDraft.compileMode === "onSave"
        ? "保存修改后更新预览。" : "点击顶部编译按钮，或按 Ctrl + Enter。"}</p>
    </div>
    <div className="settings-field">
      <label htmlFor="tex-timeout">最长编译时间</label>
      <div className="input-unit"><input id="tex-timeout" type="number" required min={1} max={300} step={0.001}
        value={seconds} disabled={settingsBusy} aria-describedby="timeout-hint"
        onChange={event => { setSeconds(event.target.value);
          setPreferencesDraft(current => ({ ...current, timeoutMs: Math.round(Number(event.target.value) * 1000) })); }} />
        <span>秒</span></div>
      <p id="timeout-hint" className="field-hint">超过此时间将停止编译。可设置 1–300 秒。</p>
    </div>
    <details className="settings-details"><summary>运行环境</summary>
      <dl><div><dt>引擎</dt><dd>pdfLaTeX</dd></div><div><dt>工具链</dt><dd>内置 MiKTeX</dd></div></dl>
    </details>
    {settingsError && <p className="error" role="alert">{settingsError}</p>}
    <div className="modal-actions"><button type="button" onClick={onClose}>取消</button>
      <button className="primary" type="submit" disabled={settingsBusy}>{settingsBusy ? "保存中…" : "保存设置"}</button></div>
  </form>;
}
