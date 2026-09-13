export function draftProjectDirectories(paths: readonly string[]): string[] {
  const directories = new Set<string>();
  for (const path of paths) {
    const parts = path.split("/");
    for (let length = 1; length < parts.length; length += 1)
      directories.add(parts.slice(0, length).join("/"));
  }
  return [...directories].sort((left, right) => {
    const depth = left.split("/").length - right.split("/").length;
    return depth || left.localeCompare(right);
  });
}
