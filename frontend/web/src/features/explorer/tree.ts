export interface TreeNode { path: string; name: string; directory: boolean; children: TreeNode[] }
export function buildTree(paths: string[], directories: string[] = []): TreeNode[] {
  const roots: TreeNode[] = [];
  const insert = (path: string, terminalDirectory: boolean) => {
    let nodes = roots;
    const segments = path.split("/");
    for (let index = 0; index < segments.length; index++) {
      const key = segments.slice(0, index + 1).join("/");
      let node = nodes.find(item => item.path === key);
      const directory = index < segments.length - 1 || terminalDirectory;
      if (!node) { node = { path: key, name: segments[index], directory, children: [] }; nodes.push(node); }
      else if (directory) node.directory = true;
      nodes = node.children;
    }
  };
  directories.forEach(path => insert(path, true));
  paths.forEach(path => insert(path, false));
  const sort = (nodes: TreeNode[]) => {
    nodes.sort((a, b) => Number(b.directory) - Number(a.directory) || a.name.localeCompare(b.name));
    nodes.forEach(node => sort(node.children));
  };
  sort(roots);
  return roots;
}
