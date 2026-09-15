export type PreviewState = "idle" | "preparing" | "detecting" | "running" | "succeeded" |
  "failed" | "rendering" | "cancelled" | "timedOut" | "unavailable";

export interface PreviewPresentation {
  summary: string;
  action: string;
  preparing: boolean;
  running: boolean;
}

export function previewPresentation(state: PreviewState): PreviewPresentation {
  const preparing = state === "preparing";
  const running = state === "detecting" || state === "running";
  const summary = state === "idle" ? "尚未创建编译任务" :
    state === "preparing" ? "正在准备隔离编译快照" : state === "detecting" ? "正在检测 TeX 工具链" :
      state === "running" ? "正在本地编译" : state === "rendering" ? "正在验证并渲染 PDF" : state === "succeeded" ? "编译成功" :
        state === "unavailable" ? "未检测到 TeX 编译器" : state === "cancelled" ? "编译已取消" :
          state === "timedOut" ? "编译超时" : "编译失败";
  const action = preparing ? "准备编译" : running ? "取消编译" : "编译";
  return { summary, action, preparing, running };
}