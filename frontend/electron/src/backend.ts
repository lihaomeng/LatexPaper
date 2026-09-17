import { randomUUID } from 'node:crypto';
import { spawn, type ChildProcessWithoutNullStreams } from 'node:child_process';

export const maxWireBytes = 32 * 1024 * 1024;
export type NativeReply = { queryId: number; success: boolean; payload: string };

/** One private child and one RPC session per renderer document. */
export class BackendClient {
  private readonly child: ChildProcessWithoutNullStreams;
  private header = Buffer.alloc(4);
  private headerUsed = 0;
  private payload: Buffer | undefined;
  private payloadUsed = 0;
  private failed = false;
  private closing: Promise<void> | undefined;
  private readonly onExit: Promise<void>;

  constructor(executable: string, private readonly reply: (value: NativeReply) => void,
    private readonly unavailable: () => void) {
    this.child = spawn(executable, [randomUUID()], { windowsHide: true, stdio: ['pipe', 'pipe', 'pipe'] });
    this.onExit = new Promise(resolve => {
      this.child.once('close', () => { this.fail(); resolve(); });
    });
    this.child.on('error', () => this.fail());
    this.child.stdin.on('error', () => this.fail());
    this.child.stdout.on('data', (chunk: Buffer) => this.receive(chunk));
    // Drain diagnostic output; never reflect native paths or document data into the renderer.
    this.child.stderr.on('data', () => {});
  }

  request(queryId: number, request: string, persistent: boolean, selection: string | null): void {
    this.send({ op: 'request', queryId, request, persistent, selection });
  }
  cancel(queryId: number): void { if (!this.failed) this.send({ op: 'cancel', queryId }); }

  private send(message: unknown): void {
    if (this.failed || this.closing) throw new Error('TRANSPORT_UNAVAILABLE');
    const data = Buffer.from(JSON.stringify(message), 'utf8');
    if (!data.length || data.length > maxWireBytes ||
        this.child.stdin.writableLength + data.length > 2 * maxWireBytes)
      throw new Error('RESOURCE_EXHAUSTED');
    const header = Buffer.alloc(4);
    header.writeUInt32LE(data.length);
    this.child.stdin.write(Buffer.concat([header, data]));
  }

  private receive(chunk: Buffer): void {
    if (this.failed) return;
    try {
      let offset = 0;
      while (offset < chunk.length) {
        if (!this.payload) {
          const size = Math.min(4 - this.headerUsed, chunk.length - offset);
          chunk.copy(this.header, this.headerUsed, offset, offset + size);
          this.headerUsed += size; offset += size;
          if (this.headerUsed !== 4) continue;
          const length = this.header.readUInt32LE();
          if (length === 0 || length > maxWireBytes) throw new Error('INVALID_FRAME');
          this.payload = Buffer.allocUnsafe(length);
          this.payloadUsed = 0; this.headerUsed = 0;
        }
        const size = Math.min(this.payload.length - this.payloadUsed, chunk.length - offset);
        chunk.copy(this.payload, this.payloadUsed, offset, offset + size);
        this.payloadUsed += size; offset += size;
        if (this.payloadUsed !== this.payload.length) continue;
        const value: unknown = JSON.parse(this.payload.toString('utf8'));
        this.payload = undefined;
        if (!value || typeof value !== 'object') throw new Error('INVALID_FRAME');
        const item = value as Partial<NativeReply>;
        if (!Number.isSafeInteger(item.queryId) || typeof item.success !== 'boolean' ||
            typeof item.payload !== 'string') throw new Error('INVALID_FRAME');
        this.reply(item as NativeReply);
      }
    } catch { this.fail(); void this.close(); }
  }

  private fail(): void {
    if (this.failed) return;
    this.failed = true;
    this.payload = undefined;
    this.unavailable();
  }

  close(): Promise<void> {
    if (this.closing) return this.closing;
    this.failed = true;
    this.child.stdin.end();
    this.closing = new Promise(resolve => {
      // EOF cancels the C++ session and compiler jobs; force termination only after grace.
      const timer = setTimeout(() => this.child.kill(), 7000);
      void this.onExit.then(() => { clearTimeout(timer); resolve(); });
    });
    return this.closing;
  }
}
