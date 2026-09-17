"""Validate the graph exported from actual CMake target link properties."""
import argparse
import copy
import json
from pathlib import Path
import re


def validate(records, source):
    nodes = {n['name']: n for n in records}
    errors = []
    layers = {'kernel', 'domain', 'inbound', 'outbound', 'app', 'adapter', 'workflow', 'contract',
              'rpc', 'transport', 'platform', 'composition', 'test', 'bootstrap'}
    external = {
        'qt': {'Qt5::Core', 'Qt5::Gui', 'Qt5::Widgets'},
        'cef': {'libcef_lib', 'libcef_dll_wrapper'},
    }
    for node in records:
        layer, module = node['layer'], node['module']
        if layer not in layers or not module:
            errors.append(f"Unknown metadata: {node['name']}")
        for raw in node['links']:
            link = raw.strip()
            other = nodes.get(link)
            if not other:
                if node['name'] == 'lol_platform_cef' and link == 'bcrypt':
                    continue  # Windows CSPRNG for per-document CSP nonces, platform-private only.
                if node['name'] == 'lol_document_adapter_localfs' and link == 'bcrypt':
                    continue  # Local adapter hashes revisions and creates unpredictable sibling temp names.
                if node['name'] == 'lol_workspace_adapter_localfs' and link == 'bcrypt':
                    continue  # Local workspace adapter hashes opaque roots and deterministic tree revisions.
                if node['name'] == 'lol_export_adapter_localfs' and link == 'bcrypt':
                    continue  # Export adapter creates unpredictable, session-bound destination tokens.
                if node['name'] in {'lol_preferences_adapter_sqlite', 'lol_session_adapter_sqlite'} and link.lower().replace('\\', '/').endswith('/sqlite3.lib'):
                    continue  # SQLite is private to the two persistence adapters.
                allowed = layer in {'platform', 'bootstrap', 'composition', 'transport'} and link in (external.get(module, set()) if layer != 'composition' else {'libcef_lib', 'libcef_dll_wrapper'})
            else:
                target_layer = other['layer']
                same = module == other['module']
                allowed = (
                    layer in {'composition', 'test'}
                    or layer in {'domain', 'inbound', 'outbound', 'contract'} and target_layer == 'kernel'
                    or layer == 'inbound' and module.endswith('workflow') and target_layer == 'inbound'
                    or layer == 'app' and same and target_layer in {'domain', 'inbound', 'outbound'}
                    or layer == 'adapter' and same and target_layer in {'domain', 'outbound'}
                    or layer == 'workflow' and target_layer == 'inbound'
                    or layer == 'rpc' and target_layer in {'contract', 'inbound'}
                    or layer == 'transport' and (target_layer == 'rpc' or link in {'lol_platform_cef', 'lol_platform_ports'})
                    or layer == 'bootstrap' and target_layer in {'composition', 'platform'}
                    or layer == 'platform' and link in {'lol_platform_ports', 'lol_platform_lifecycle'}
                )
            if not allowed:
                errors.append(f"Forbidden dependency: {node['name']} -> {link}")
    active, done = set(), set()

    def visit(name):
        if name in active:
            errors.append(f'Cycle through {name}')
            return
        if name in done:
            return
        active.add(name)
        for link in nodes[name]['links']:
            if link.strip() in nodes:
                visit(link.strip())
        active.remove(name)
        done.add(name)

    for name in nodes:
        visit(name)
    # Both public and private core source is checked; an implementation include
    # must not provide an escape hatch around the target graph.
    forbidden = re.compile(r'\b(QString|QObject|CefRefPtr|HWND|HANDLE|QVariant|nlohmann|sqlite3)\b|#\s*include\s*[<"](?:Q[A-Z]|windows\.h|filesystem|.*cef_|.*json)')
    service = source / 'backend/latexlocalservice'
    modules = {
        'app/system': 'system', 'workspace/project': 'workspace',
        'workspace/document': 'document', 'workspace/search': 'search',
        'workspace/export': 'export', 'texengine/build': 'build',
        'texengine/preview': 'preview', 'texengine/navigation': 'navigation',
        'persistence/preferences': 'preferences', 'persistence/session': 'session',
    }
    roots = {'app/kernel': None, 'transport/rpc': None, **modules}
    for relative, module in roots.items():
        root = service / relative
        if not root.is_dir():
            errors.append(f'Missing architecture source root: {relative}')
            continue
        for path in root.rglob('*'):
            if path.suffix not in {'.h', '.cpp'} or 'adapters' in path.parts:
                continue
            code = re.sub(r'//[^\n]*|/\*.*?\*/', '', path.read_text(encoding='utf-8'), flags=re.S)
            if forbidden.search(code):
                errors.append(f'Framework leaked into core: {path.relative_to(source)}')
            if module:
                for imported in re.findall(r'#\s*include\s*[<"]lightoverleaf/([^/]+)/([^/]+)/', code):
                    if imported[0] not in {module, 'kernel'} and imported[1] != 'inbound':
                        errors.append(f'Cross-module private include: {path}')
    return errors


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--graph', type=Path, required=True)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    records = json.loads(args.graph.read_text(encoding='utf-8'))
    errors = validate(records, args.source)
    if errors:
        raise SystemExit('\n'.join(errors))
    if args.self_test:
        for origin, target in [('lol_document_app', 'lol_platform_qt'),
                               ('lol_document_domain', 'lol_workspace_domain'),
                               ('lol_rpc_core', 'lol_platform_cef'),
                               ('lol_kernel', 'Qt5::Core'),
                               ('lol_document_adapter_localfs', 'lol_workspace_outbound'),
                               ('lol_document_app', 'lol_document_app')]:
            invalid = copy.deepcopy(records)
            next(n for n in invalid if n['name'] == origin)['links'].append(target)
            if not validate(invalid, args.source):
                raise SystemExit(f'Negative fixture incorrectly accepted: {origin} -> {target}')
        print('6 forbidden dependency/cycle fixtures rejected')
    print(f'Architecture OK: {len(records)} targets')


if __name__ == '__main__':
    main()
