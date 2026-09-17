import { useMemo, useState } from "react";
import { Icon } from "../../shared/Icon";
import { buildTree, type TreeNode } from "./tree";
export function Explorer({ files, directories = [], active, activeDirectory, filter, onOpen, onSelectDirectory,
  openFiles = [], onRename, onRemove, operationsDisabled = false }: {
  files: { path: string; dirty: boolean }[]; directories?: string[]; active: string | null;
  activeDirectory?: string | null; filter: string; onOpen(path: string): void;
  onSelectDirectory?(path: string): void; openFiles?: string[];
  onRename?(path: string): void; onRemove?(path: string): void; operationsDisabled?: boolean;
}) {
  const [collapsed, setCollapsed] = useState<Set<string>>(new Set());
  const query = filter.trim().toLowerCase();
  const tree = useMemo(() => buildTree(
    files.filter(file => file.path.toLowerCase().includes(query)).map(file => file.path),
    directories.filter(path => path.toLowerCase().includes(query))), [directories, files, query]);
  const render = (nodes: TreeNode[], depth = 0) => nodes.map(node => <div key={node.path} role="none">
    <div className={"tree-item-line " + (!node.directory && (onRename || onRemove) ? "has-actions" : "")}>
      <button role="treeitem" aria-selected={node.directory ? activeDirectory === node.path : active === node.path}
        aria-expanded={node.directory ? !collapsed.has(node.path) : undefined}
        className={"tree-row " + ((node.directory ? activeDirectory === node.path : active === node.path) ? "selected " : "") +
          (openFiles.includes(node.path) ? "opened" : "")}
        style={{ paddingLeft: 10 + depth * 12 }} title={node.path}
        onClick={() => {
          if (node.directory) {
            onSelectDirectory?.(node.path);
            setCollapsed(previous => {
              const next = new Set(previous);
              if (next.has(node.path)) next.delete(node.path); else next.add(node.path);
              return next;
            });
          } else onOpen(node.path);
        }}>
        {node.directory && <span className={!collapsed.has(node.path) ? "rotate" : ""}><Icon name="chevron" size={11} /></span>}
        <Icon name={node.directory ? "folder" : "file"} size={14} /><span className="truncate">{node.name}</span>
        {files.find(file => file.path === node.path)?.dirty
          ? <span className="dirty-dot" aria-label="未保存修改" />
          : openFiles.includes(node.path) && <span className="open-file-marker" aria-label="已打开" />}
      </button>
      {!node.directory && <div className="tree-inline-actions">
        {onRename && <button aria-label={"重命名 " + node.path} title="重命名文件" disabled={operationsDisabled}
          onClick={() => onRename(node.path)}><Icon name="edit" size={12} /></button>}
        {onRemove && <button aria-label={"删除 " + node.path} title="移入回收站" disabled={operationsDisabled}
          onClick={() => onRemove(node.path)}><Icon name="trash" size={12} /></button>}
      </div>}
    </div>
    {node.directory && !collapsed.has(node.path) && <div role="group">{render(node.children, depth + 1)}</div>}
  </div>);
  return <div className="file-tree" role="tree" aria-label="项目文件树">
    {tree.length ? render(tree) : <p className="muted empty-small">{query ? "没有匹配的文件，请调整筛选条件。" : "项目中暂无文件，可点击上方 + 新建。"}</p>}
  </div>;
}
