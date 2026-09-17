import type { AuthoringRpcClient, Preferences } from '../native-api';
import type { BuildView } from '../features/preview';
import { nextBuildGeneration, buildErrorMessage } from './buildRequest';

type CompileApi = Pick<AuthoringRpcClient, 'detect' | 'build' | 'status' | 'cancel' | 'readPdf'>;
export interface CompileCommand {
  mainFileId: string;
  files: readonly { fileId: string; content: string }[];
  scopeId: string;
  engine: Preferences['engine'];
  timeoutMs: number;
}

/** Owns RPC orchestration and stale-result rejection; no React or editor models. */
export class CompileController {
  private sequence = 0;
  private job: string | null = null;
  constructor(private readonly api: CompileApi,
    private readonly publish: (update: (previous: BuildView) => BuildView) => void) {}

  async cancel(): Promise<void> {
    if (this.job) await this.api.cancel(this.job);
  }

  async invalidate(publishCancellation = true): Promise<void> {
    const sequence = ++this.sequence;
    const job = this.job;
    this.job = null;
    if (job) {
      await this.api.cancel(job);
      if (publishCancellation && sequence === this.sequence)
        this.publish(current => ({ ...current, state: 'cancelled', candidate: undefined }));
    }
  }

  async run(command: CompileCommand, acceptsResults: () => boolean): Promise<void> {
    if (!acceptsResults()) return;
    const sequence = ++this.sequence;
    const current = () => sequence === this.sequence && acceptsResults();
    let timer: ReturnType<typeof setInterval> | undefined;
    let polling = false;
    let terminal = false;
    try {
      const generation = nextBuildGeneration();
      this.publish(previous => ({ ...previous, state: 'detecting', output: '', diagnostics: [],
        candidate: undefined, phase: 'detect', generation }));
      const detected = await this.api.detect();
      if (!current()) return;
      const engine = detected.toolchains.flatMap(item => item.engines)
        .find(item => item === command.engine) ?? detected.toolchains[0]?.engines[0];
      if (!engine) {
        this.publish(previous => ({ ...previous, state: 'unavailable', phase: 'detect',
          output: '未找到可用的 TeX 引擎。请检查内置 runtime/miktex，必要时运行 build.ps1 --dev -RefreshRuntime；不会自动下载宏包。' }));
        return;
      }
      const token = crypto.randomUUID();
      const jobId = 'job-' + token;
      this.job = jobId;
      this.publish(previous => ({ ...previous, state: 'running', phase: 'snapshot', generation }));
      timer = setInterval(() => {
        if (polling || !current()) return;
        polling = true;
        void this.api.status(jobId).then(status => {
          if (!terminal && current() && status.state === 'running')
            this.publish(previous => ({ ...previous, state: 'running', phase: status.phase,
              generation: status.generation, output: status.output + (status.outputTruncated ? '\n[日志已截断]' : '') }));
        }).catch(() => { /* Terminal build response remains authoritative. */ })
          .finally(() => { polling = false; });
      }, 200);
      const result = await this.api.build(jobId, 'snapshot-' + token, command.mainFileId,
        engine, command.timeoutMs, command.files, command.scopeId, generation);
      terminal = true;
      if (timer !== undefined) clearInterval(timer);
      if (!current()) return;
      const output = result.output + (result.outputTruncated ? '\n[日志已截断]' : '');
      const diagnostics = result.diagnostics.map(item => ({ ...item }));
      if (result.artifactId) {
        const pdf = await this.api.readPdf(result.artifactId);
        if (!current()) return;
        this.publish(previous => ({ ...previous, state: 'rendering', output, diagnostics,
          candidate: { generation: result.generation, artifactId: result.artifactId!, pdf,
            syncTexAvailable: result.syncTexAvailable }, generation: result.generation, phase: 'render' }));
      } else {
        this.publish(previous => ({ ...previous,
          state: result.terminal === 'compilerUnavailable' ? 'unavailable' : result.terminal,
          output, diagnostics, candidate: undefined, generation: result.generation, phase: result.phase }));
      }
    } catch (error) {
      if (current()) this.publish(previous => ({ ...previous,
        state: (error as Error).message === 'RPC_CANCELLED' ? 'cancelled' : 'failed',
        output: buildErrorMessage((error as Error).message), diagnostics: [], candidate: undefined }));
    } finally {
      terminal = true;
      if (timer !== undefined) clearInterval(timer);
      if (sequence === this.sequence) this.job = null;
    }
  }
}
