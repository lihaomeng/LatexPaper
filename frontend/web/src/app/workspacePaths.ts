const reservedStem = /^(con|prn|aux|nul|clock\$|conin\$|conout\$|com(?:[1-9¹²³])|lpt(?:[1-9¹²³]))$/i;

export function validWorkspaceDirectoryId(path: string): boolean {
  const bytes = new TextEncoder().encode(path).length;
  if (!path || bytes > 1024 || /[\\:<>"|?*]/.test(path) ||
      [...path].some(character => {
        const code = character.codePointAt(0) ?? 0;
        return code < 32 || code === 127;
      })) return false;
  const segments = path.split('/');
  if (!segments.every(segment => {
    if (!segment || segment === '.' || segment === '..' || /[. ]$/.test(segment)) return false;
    return !reservedStem.test(segment.split('.')[0]);
  })) return false;
  return segments[0].toLowerCase() !== '.lightoverleaf-trash';
}
