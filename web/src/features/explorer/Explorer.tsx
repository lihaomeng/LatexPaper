import { useMemo, useState } from "react";
import { Icon } from "../../shared/Icon";
import { buildTree, type TreeNode } from "./tree";
export function Explorer({ files, directories = [], active, activeDirectory, filter, onOpen, onSelectDirectory }: {
  files: { path: string; dirty: boolean }[]; directories?: string[]; active: string | null;
  activeDirectory?: string | null; filter: string; onOpen(path: string): void;
  onSelectDirectory?(path: string): void;
}) {
  const [collapsed, setCollapsed] = useState<Set<string>>(new Set());
  const query = filter.toLowerCase();
  const tree = useMemo(() => buildTree(
    files.filter(file => file.path.toLowerCase().includes(query)).map(file => file.path),
    directories.filter(path => path.toLowerCase().includes(query))), [directories, files, query]);
  const render = (nodes: TreeNode[], depth = 0) => nodes.map(node => <div key={node.path} role="none">
    <button role="treeitem" aria-selected={node.directory ? activeDirectory === node.path : active === node.path}
      aria-expanded={node.directory ? !collapsed.has(node.path) : undefined}
      className={"tree-row " + ((node.directory ? activeDirectory === node.path : active === node.path) ? "selected" : "")}
      style={{ paddingLeft: 12 + depth * 16 }} title={node.path}
      onClick={() => {
        if (node.directory) {
          onSelectDirectory?.(node.path);
          setCollapsed(previous => {
            const next = new Set(previous); if (next.has(node.path)) next.delete(node.path); else next.add(node.path); return next;
          });
        } else onOpen(node.path);
      }}>
      {node.directory && <span className={!collapsed.has(node.path) ? "rotate" : ""}><Icon name="chevron" size={11} /></span>}
      <Icon name={node.directory ? "folder" : "file"} size={15} /><span className="truncate">{node.name}</span>
      {files.find(file => file.path === node.path)?.dirty && <span className="dirty-dot" aria-label="未缓存修改" />}
    </button>
    {node.directory && !collapsed.has(node.path) && <div role="group">{render(node.children, depth + 1)}</div>}
  </div>);
  return <div className="file-tree" role="tree" aria-label="草稿文件树">
    {tree.length ? render(tree) : <p className="muted empty-small">没有匹配的文件</p>}
  </div>;
}
