import { forwardRef, useEffect, useImperativeHandle, useRef } from "react";
import { monaco } from "./monaco";
import { EditorSession } from "./EditorSession";
export type EditorCommand = "undo" | "redo" | "find" | "bold" | "italic" | "section" | "subsection" | "equation" | "itemize" | "enumerate" | "table" | "reference";
export interface EditorCommands { run(command: EditorCommand): void; reveal(line: number, column?: number): void }
export const EditorSurface = forwardRef<EditorCommands, { session: EditorSession; wrap: boolean; readOnly?: boolean; onReady(): void }>(
  function EditorSurface({ session, wrap, readOnly = false, onReady }, ref) {
    const host = useRef<HTMLDivElement>(null);
    const instance = useRef<monaco.editor.IStandaloneCodeEditor | null>(null);
    const ready = useRef(onReady);
    ready.current = onReady;
    useImperativeHandle(ref, () => ({
      run(command) {
        const editor = instance.current;
        if (!editor || !editor.getModel() || editor.getOption(monaco.editor.EditorOption.readOnly)) return;
        editor.focus();
        if (command === "bold" || command === "italic") {
          const selection = editor.getSelection()!;
          const text = editor.getModel()!.getValueInRange(selection);
          editor.pushUndoStop();
          editor.executeEdits("toolbar", [{ range: selection, text: "\\" + (command === "bold" ? "textbf" : "textit") + "{" + (text || "文字") + "}" }]);
          editor.pushUndoStop();
        } else if (["section", "subsection", "equation", "itemize", "enumerate", "table", "reference"].includes(command)) {
          const selection = editor.getSelection()!;
          const selected = editor.getModel()!.getValueInRange(selection);
          const templates: Record<string, string> = {
            section: "\\section{" + (selected || "章节标题") + "}",
            subsection: "\\subsection{" + (selected || "小节标题") + "}",
            equation: "\\begin{equation}\n" + (selected || "E = mc^2") + "\n\\end{equation}",
            itemize: "\\begin{itemize}\n  \\item " + (selected || "列表内容") + "\n\\end{itemize}",
            enumerate: "\\begin{enumerate}\n  \\item " + (selected || "列表内容") + "\n\\end{enumerate}",
            table: "\\begin{tabular}{ll}\n  列一 & 列二 \\\\ \n  内容 & 内容\n\\end{tabular}",
            reference: "\\ref{" + (selected || "标签名称") + "}",
          };
          editor.pushUndoStop();
          editor.executeEdits("insert-template", [{ range: selection, text: templates[command] }]);
          editor.pushUndoStop();
        } else editor.trigger("toolbar", command === "find" ? "actions.find" : command, null);
      },
      reveal(line, column = 1) {
        instance.current?.setPosition({ lineNumber: line, column });
        instance.current?.revealLineInCenter(line);
        instance.current?.focus();
      },
    }), []);
    useEffect(() => {
      if (!host.current) return;
      const editor = monaco.editor.create(host.current, {
        model: null, theme: "lightoverleaf-dark", automaticLayout: true,
        fontFamily: 'Consolas, "Cascadia Code", "Microsoft YaHei", monospace',
        fontSize: 13, lineHeight: 24, minimap: { enabled: false },
        padding: { top: 18, bottom: 20 }, scrollBeyondLastLine: false,
        lineNumbersMinChars: 4, renderLineHighlight: "line",
        wordWrap: "on", wrappingIndent: "same", tabSize: 2,
        bracketPairColorization: { enabled: false }, overviewRulerLanes: 0,
        unicodeHighlight: { ambiguousCharacters: false, invisibleCharacters: false },
        ariaLabel: "LaTeX 源码编辑器", contextmenu: true,
      });
      instance.current = editor;
      let previous: string | null = null;
      const views = new Map<string, monaco.editor.ICodeEditorViewState>();
      const update = () => {
        const active = session.getView().active;
        if (previous === active) return;
        if (previous) {
          const view = editor.saveViewState();
          if (view) views.set(previous, view);
        }
        previous = active;
        editor.setModel(active ? session.model(active) : null);
        if (active && views.has(active)) editor.restoreViewState(views.get(active)!);
      };
      update();
      const unsubscribe = session.subscribe(update);
      const cursor = editor.onDidChangeCursorPosition(event => session.setCursor(event.position.lineNumber, event.position.column));
      ready.current();
      return () => { unsubscribe(); cursor.dispose(); editor.dispose(); instance.current = null; };
    }, [session]);
    useEffect(() => { instance.current?.updateOptions({ wordWrap: wrap ? "on" : "off" }); }, [wrap]);
    useEffect(() => { instance.current?.updateOptions({ readOnly }); }, [readOnly]);
    return <div className="monaco-host" ref={host} />;
  });
