const generationKey = 'lightoverleaf.buildRequestGeneration.v1';
let latestGeneration = 0;

// Native Latest-wins state can outlive the renderer. Never use document revision.
export function nextBuildGeneration(): number {
  const stored = Number(sessionStorage.getItem(generationKey) ?? '0');
  if (!Number.isSafeInteger(stored) || stored < 0) throw new Error('BUILD_SEQUENCE_INVALID');
  const previous = Math.max(stored, latestGeneration, Date.now());
  if (previous >= Number.MAX_SAFE_INTEGER) throw new Error('RESOURCE_EXHAUSTED');
  const next = previous + 1;
  sessionStorage.setItem(generationKey, String(next));
  latestGeneration = next;
  return next;
}

export function buildErrorMessage(code: string): string {
  const messages: Record<string, string> = {
    RPC_TIMEOUT: '原生编译请求超时（不是 LaTeX 语法错误）。请检查任务状态和运行时日志后重试。',
    BUILD_STALE_GENERATION: '编译请求已过期，请重新编译；项目文件未被修改。',
    BUILD_JOB_EXISTS: '编译任务标识重复，请重新编译。',
    BUILD_SNAPSHOT_EXISTS: '隔离快照标识已存在，请重新编译。',
    BUILD_OVERLAY_CONFLICT: '编译快照中文件与目录路径冲突，请检查项目路径。',
    BUILD_DUPLICATE_OVERLAY: '编译内容包含重复文件路径（包括仅大小写不同），请检查文件列表。',
    BUILD_SEQUENCE_INVALID: '编译请求序号无效，请关闭窗口后重新启动应用。',
  };
  return messages[code] ? `${messages[code]} (${code})` : code;
}