export interface ThreeWayMergeResult {
  content: string;
  conflicts: number;
}

const marker = /^(<{7} 本地编辑|={7}|>{7} 磁盘版本)$/m;

export function containsMergeMarkers(content: string): boolean {
  return marker.test(content);
}

export function mergeDocumentText(base: string, local: string, disk: string): ThreeWayMergeResult {
  if (local === disk) return { content: local, conflicts: 0 };
  if (local === base) return { content: disk, conflicts: 0 };
  if (disk === base) return { content: local, conflicts: 0 };
  const baseLines = base.split('\n');
  const localLines = local.split('\n');
  const diskLines = disk.split('\n');
  if (baseLines.length !== localLines.length || baseLines.length !== diskLines.length) {
    return { content: ['<<<<<<< 本地编辑', local, '=======', disk,
      '>>>>>>> 磁盘版本'].join('\n'), conflicts: 1 };
  }
  const merged: string[] = [];
  let conflicts = 0;
  for (let index = 0; index < baseLines.length; index++) {
    const original = baseLines[index];
    const ours = localLines[index];
    const theirs = diskLines[index];
    if (ours === theirs) merged.push(ours);
    else if (ours === original) merged.push(theirs);
    else if (theirs === original) merged.push(ours);
    else {
      conflicts++;
      merged.push('<<<<<<< 本地编辑', ours, '=======', theirs, '>>>>>>> 磁盘版本');
    }
  }
  return { content: merged.join('\n'), conflicts };
}
