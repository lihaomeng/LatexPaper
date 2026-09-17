"""Generate bounded V2 RPC contracts in both languages from one schema set."""
import json
from pathlib import Path


def generate_protocol(root, cpp_out, write, check):
    definitions = {}
    expected_root = {'$schema', '$id', '$defs', 'oneOf'}
    for schema_path in sorted((root / 'contracts/rpc/v2').glob('*.schema.json')):
        schema = json.loads(schema_path.read_text(encoding='utf-8'))
        local = schema['$defs']
        if set(schema) != expected_root or schema['oneOf'] != [{'$ref': '#/$defs/' + name} for name in local]:
            raise ValueError('Unsupported V2 root schema: ' + schema_path.name)
        duplicate = set(definitions).intersection(local)
        if duplicate:
            raise ValueError('Duplicate V2 definitions: ' + ', '.join(sorted(duplicate)))
        definitions.update(local)
    if not definitions:
        raise ValueError('No V2 schemas found')
    cpp = ['// Generated. Do not edit.', '#pragma once', '#include <krpccontract.h>',
           'namespace lightoverleaf::rpc::v2', '{',
           'inline bool validateUtf8Text(const std::string& input)', '{',
           '    for (std::size_t index = 0; index < input.size();)', '    {',
           '        const unsigned char first = static_cast<unsigned char>(input[index]);',
           '        if (first <= 0x7f) { ++index; continue; }',
           '        std::size_t count = 0;',
           '        if (first >= 0xc2 && first <= 0xdf) count = 2;',
           '        else if (first >= 0xe0 && first <= 0xef) count = 3;',
           '        else if (first >= 0xf0 && first <= 0xf4) count = 4;',
           '        else return false;',
           '        if (index + count > input.size()) return false;',
           '        for (std::size_t offset = 1; offset < count; ++offset)', '        {',
           '            const unsigned char next = static_cast<unsigned char>(input[index + offset]);',
           '            if (next < 0x80 || next > 0xbf) return false;', '        }',
           '        const unsigned char second = static_cast<unsigned char>(input[index + 1]);',
           '        if ((first == 0xe0 && second < 0xa0) || (first == 0xed && second > 0x9f) ||',
           '            (first == 0xf0 && second < 0x90) || (first == 0xf4 && second > 0x8f)) return false;',
           '        index += count;', '    }', '    return true;', '}']
    ts = ['// Generated. Do not edit.',
          'function validateUtf8Text(input: string): boolean {',
          '  for (let index = 0; index < input.length; index += 1) {',
          '    const unit = input.charCodeAt(index);',
          '    if (unit >= 0xd800 && unit <= 0xdbff) {',
          '      if (++index >= input.length) return false;',
          '      const low = input.charCodeAt(index);',
          '      if (low < 0xdc00 || low > 0xdfff) return false;',
          '    } else if (unit >= 0xdc00 && unit <= 0xdfff) return false;',
          '  }', '  return true;', '}']
    allowed = {'type', 'const', 'enum', 'minimum', 'maximum', 'minLength', 'maxLength',
               'minItems', 'maxItems', 'items', 'pattern', 'properties', 'required',
               'additionalProperties', 'x-utf8MinBytes', 'x-utf8MaxBytes', 'x-maxProperties'}
    kinds = {'object': 'KValue::KObject', 'array': 'KValue::KArray', 'string': 'std::string',
             'integer': 'double', 'boolean': 'bool'}

    def emit(node, name):
        kind = node.get('type')
        if set(node) - allowed or kind not in kinds:
            raise ValueError('Unsupported V2 node: ' + name)
        if node.get('pattern') not in {None, '^[a-zA-Z0-9-]+$', '^[a-zA-Z0-9.-]+$'}:
            raise ValueError('Unsupported pattern')
        if kind == 'string' and not ('const' in node or 'enum' in node or node.get('pattern') or
                                     'x-utf8MaxBytes' in node):
            raise ValueError('Bounded string required for cross-language length parity')
        if 'enum' in node and (kind != 'string' or not node['enum'] or not all(isinstance(v, str) for v in node['enum'])):
            raise ValueError('Only nonempty string enums supported')
        children = node.get('properties', {})
        open_object = kind == 'object' and node.get('additionalProperties') is True
        if kind == 'object' and open_object and (children or node.get('required')):
            raise ValueError('Open V2 objects cannot declare fields')
        if kind == 'object' and not open_object and (node.get('additionalProperties') is not False or
                                                     node.get('required') != list(children)):
            raise ValueError('Closed V2 objects must require exactly their declared fields')
        child_names = {key: name + key[0].upper() + key[1:] for key in children}
        for key, child in children.items():
            emit(child, child_names[key])
        if kind == 'array':
            if 'items' not in node:
                raise ValueError('V2 arrays require items')
            item_name = name + 'Item'
            emit(node['items'], item_name)
            cpp.append('using K' + name + ' = std::vector<K' + item_name + '>;')
            ts_type = item_name + '[]'
        elif kind == 'object' and open_object:
            cpp.append('using K' + name + ' = KValue::KObject;')
            ts_type = 'Record<string, unknown>'
        elif kind == 'object':
            cpp.extend(['struct K' + name, '{'] + [
                '    K' + child_names[key] + ' m_' + key + '{};' for key in children] + ['};'])
            ts_type = '{ ' + '; '.join(json.dumps(key) + ': ' + child_names[key] for key in children) + ' }' if children else 'Record<string, never>'
        else:
            cpp.append('using K' + name + ' = ' + {'string': 'std::string', 'integer': 'std::int64_t', 'boolean': 'bool'}[kind] + ';')
            ts_type = json.dumps(node['const']) if 'const' in node else ' | '.join(map(json.dumps, node['enum'])) if 'enum' in node else {'string': 'string', 'integer': 'number', 'boolean': 'boolean'}[kind]
        ts.append('export type ' + name + ' = ' + ts_type + ';')
        cpp.extend(['inline bool validate' + name + '(const KValue& input)', '{',
                    '    const auto* value = std::get_if<' + kinds[kind] + '>(&input.m_value);',
                    '    if (!value) return false;'])
        ts.append('export function validate' + name + '(input: unknown): input is ' + name + ' {')
        if kind == 'array':
            ts.append('  if (!Array.isArray(input)) return false;')
            for field, op in [('minItems', '<'), ('maxItems', '>')]:
                if field in node:
                    cpp.append(f'    if (value->size() {op} {node[field]}) return false;')
                    ts.append(f'  if (input.length {op} {node[field]}) return false;')
            cpp.extend(['    for (const KValue& item : *value)', '        if (!validate' + item_name + '(item)) return false;'])
            ts.append('  for (const item of input) if (!validate' + item_name + '(item)) return false;')
        elif kind == 'object':
            if not open_object:
                cpp.append('    if (value->size() != ' + str(len(children)) + ') return false;')
            ts.extend(['  if (input === null || typeof input !== "object" || Array.isArray(input)) return false;',
                       '  const value = input as Record<string, unknown>;',
                       ] + ([] if open_object else ['  if (Object.keys(value).length !== ' + str(len(children)) + ') return false;']))
            if 'x-maxProperties' in node:
                cpp.append('    if (value->size() > ' + str(node['x-maxProperties']) + ') return false;')
                ts.append('  if (Object.keys(value).length > ' + str(node['x-maxProperties']) + ') return false;')
            if not open_object:
                for key, child_name in child_names.items():
                    literal = json.dumps(key)
                    cpp.append('    if (!value->contains(' + literal + ') || !validate' + child_name + '(value->at(' + literal + '))) return false;')
                    ts.append('  if (!Object.hasOwn(value, ' + literal + ') || !validate' + child_name + '(value[' + literal + '])) return false;')
        else:
            ts.append('  if (typeof input !== "' + ('number' if kind == 'integer' else kind) + '") return false;')
            if kind == 'integer':
                cpp.append('    if (!std::isfinite(*value) || std::floor(*value) != *value) return false;')
                ts.append('  if (!Number.isSafeInteger(input)) return false;')
            for field, op in [('minimum', '<'), ('maximum', '>')]:
                if field in node:
                    cpp.append(f'    if (*value {op} {node[field]}.0) return false;')
                    ts.append(f'  if (input {op} {node[field]}) return false;')
            for field, op in [('minLength', '<'), ('maxLength', '>')]:
                if field in node:
                    cpp.append(f'    if (value->size() {op} {node[field]}) return false;')
                    ts.append(f'  if (input.length {op} {node[field]}) return false;')
            if 'x-utf8MaxBytes' in node:
                cpp.append('    if (!validateUtf8Text(*value)) return false;')
                ts.append('  if (!validateUtf8Text(input)) return false;')
                for field, op in [('x-utf8MinBytes', '<'), ('x-utf8MaxBytes', '>')]:
                    if field in node:
                        cpp.append(f'    if (value->size() {op} {node[field]}) return false;')
                        ts.append(f'  if (new TextEncoder().encode(input).length {op} {node[field]}) return false;')
            values = [node['const']] if 'const' in node else node.get('enum')
            if values is not None:
                cpp.append('    if (' + ' && '.join('*value != ' + json.dumps(v) for v in values) + ') return false;')
                ts.append('  if (' + ' && '.join('input !== ' + json.dumps(v) for v in values) + ') return false;')
            if 'pattern' in node:
                characters = 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-' + ('.' if '.' in node['pattern'] else '')
                cpp.append('    if (value->empty() || value->find_first_not_of(' + json.dumps(characters) + ') != std::string::npos) return false;')
                ts.append('  if (input.length === 0 || /[^a-zA-Z0-9' + ('.' if '.' in node['pattern'] else '') + '-]/.test(input)) return false;')
        cpp.extend(['    return true;', '}'])
        ts.extend(['  return true;', '}'])

    for name, definition in definitions.items():
        if not name.isascii() or not name.isalnum():
            raise ValueError('Unsafe type name')
        emit(definition, name)
    cpp.append('}')
    response_names = [name for name in definitions if name.endswith('Response') and name != 'CancellationResponse']
    request_names = [name for name in definitions if name.endswith('Request')]
    if not response_names:
        raise ValueError('No V2 responses found')
    if not request_names:
        raise ValueError('No V2 requests found')
    cpp.insert(-1, 'inline bool validateRpcRequest(const KValue& value) { return ' +
               ' || '.join('validate' + name + '(value)' for name in request_names) + '; }')
    cpp.insert(-1, 'inline bool validateResponse(const KValue& value) { return ' +
               ' || '.join('validate' + name + '(value)' for name in response_names) + '; }')
    ts.append('export type RpcRequest = ' + ' | '.join(request_names) + ';')
    ts.append('export function validateRpcRequest(value: unknown): value is RpcRequest { return ' +
              ' || '.join('validate' + name + '(value)' for name in request_names) + '; }')
    ts.append('export type Response = ' + ' | '.join(response_names) + ';')
    ts.append('export function validateResponse(value: unknown): value is Response { return ' +
              ' || '.join('validate' + name + '(value)' for name in response_names) + '; }')
    write(cpp_out / 'krpcprotocol.h', '\n'.join(cpp) + '\n', check)
    write(root / 'frontend/web/.generated/rpc/protocol.ts', '\n'.join(ts) + '\n', check)

    fixtures = []
    for fixture_path in sorted((root / 'tests/fixtures').glob('*-v2.json')):
        fixtures += json.loads(fixture_path.read_text(encoding='utf-8'))
    from generate import cpp_literal
    tests = ['#include <krpcprotocol.h>', '#include <iostream>', 'using namespace lightoverleaf::rpc;', 'int main()', '{', '    int failures = 0;']
    for fixture in fixtures:
        if fixture['type'] not in definitions:
            raise ValueError('Unknown fixture type')
        tests.extend(['    if (v2::validate' + fixture['type'] + '(' + cpp_literal(fixture['value']) + ') != ' + str(fixture['accepted']).lower() + ')',
                      '    { std::cerr << ' + json.dumps(fixture['name'] + '\n') + '; ++failures; }'])
    tests.extend(['    return failures == 0 ? 0 : 1;', '}'])
    write(cpp_out / 'kprotocoltests.cpp', '\n'.join(tests) + '\n', check)
    print(f'V2 codegen OK ({len(fixtures)} shared fixtures)')
