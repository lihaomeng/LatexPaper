"""M0 schema compiler: a deliberately small, fail-closed JSON Schema subset.

Generated files are disposable. C++ validates a transport-neutral value tree;
JSON parsing/byte limits remain the responsibility of the future RPC transport.
"""
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCHEMA = ROOT / 'contracts/rpc/v1/envelope.schema.json'
ALLOWED = {'$schema', '$id', 'title', 'type', 'const', 'required', 'properties',
           'additionalProperties', 'minimum', 'maximum', 'minLength', 'maxLength', 'pattern'}


def verify(schema):
    unknown = set(schema) - ALLOWED
    if unknown or schema['type'] not in {'object', 'string', 'integer'}:
        raise ValueError(f'Unsupported schema: {unknown or schema["type"]}')
    if schema.get('pattern') not in {None, '^[a-zA-Z0-9-]+$'}:
        raise ValueError('Unsupported pattern')
    if schema['type'] == 'object' and schema.get('additionalProperties') is not False:
        raise ValueError('Objects must reject unknown fields')
    for child in schema.get('properties', {}).values():
        verify(child)


def cpp_literal(value):
    if value is None:
        return 'KValue{nullptr}'
    if isinstance(value, bool):
        return 'KValue{' + str(value).lower() + '}'
    if isinstance(value, (float, int)):
        return 'KValue{double{' + repr(value) + '}}'
    if isinstance(value, str):
        # Fixture strings are encoded as UTF-8; escaped newlines remain literal C++ escapes.
        return 'KValue{std::string{' + json.dumps(value, ensure_ascii=False) + '}}'
    if isinstance(value, list):
        return 'KValue{KValue::KArray{' + ','.join(map(cpp_literal, value)) + '}}'
    return 'KValue{KValue::KObject{' + ','.join(
        '{' + json.dumps(key) + ',' + cpp_literal(item) + '}' for key, item in value.items()) + '}}'


def cpp_dto(schema):
    structs = []
    def emit(node, name):
        members = []
        for key, child in node['properties'].items():
            kind = {'integer': 'std::int64_t', 'string': 'std::string', 'object': 'KPing' + key[0].upper() + key[1:]}[child['type']]
            if child['type'] == 'object':
                emit(child, kind)
            if key not in node['required']:
                kind = 'std::optional<' + kind + '>'
            default = json.dumps(child['const']) if 'const' in child else ''
            members.append('    ' + kind + ' m_' + key + '{' + default + '};')
        structs.append('struct ' + name + '\n{\n' + '\n'.join(members) + '\n};')
    emit(schema, 'KPingRequest')
    return '\n'.join(structs) + '\ninline constexpr char kPingMethod[] = ' + json.dumps(schema['properties']['method']['const']) + ';\n'


def generate_cpp(schema):
    functions = []

    def emit(node):
        children = {key: emit(child) for key, child in node.get('properties', {}).items()}
        name = 'validateNode' + str(len(functions))
        kind = {'object': 'KValue::KObject', 'string': 'std::string', 'integer': 'double'}[node['type']]
        body = [f'inline bool {name}(const KValue& input)\n{{',
                f'    const auto* value = std::get_if<{kind}>(&input.m_value);',
                '    if (!value) return false;']
        if node['type'] == 'integer':
            body.append('    if (!std::isfinite(*value) || std::floor(*value) != *value) return false;')
        if 'const' in node:
            body.append('    if (*value != ' + json.dumps(node['const']) + ') return false;')
        for key, op in [('minimum', '<'), ('maximum', '>')]:
            if key in node:
                body.append(f'    if (*value {op} {node[key]}.0) return false;')
        for key, op in [('minLength', '<'), ('maxLength', '>')]:
            if key in node:
                body.append(f'    if (value->size() {op} {node[key]}) return false;')
        if 'pattern' in node:
            body.extend(['    for (const unsigned char character : *value)', '    {',
                         "        if (!((character >= 'a' && character <= 'z') ||",
                         "              (character >= 'A' && character <= 'Z') ||",
                         "              (character >= '0' && character <= '9') || character == '-')) return false;",
                         '    }'])
        if node['type'] == 'object' and not children:
            body.append('    return value->empty();')
            body.append('}')
            functions.append('\n'.join(body))
            return name
        if node['type'] == 'object':
            for key in node['required']:
                body.append(f'    if (value->find("{key}") == value->end()) return false;')
            body.extend(['    for (const auto& [key, child] : *value)', '    {'])
            for index, (key, child) in enumerate(children.items()):
                body.append(f'        {"if" if index == 0 else "else if"} (key == "{key}")')
                body.append(f'        {{ if (!{child}(child)) return false; }}')
            body.append('        ' + ('else ' if children else '') + 'return false;')
            body.append('    }')
        body.extend(['    return true;', '}'])
        functions.append('\n'.join(body))
        return name
    entry = emit(schema)
    return '''// Generated from contracts/rpc/v1/envelope.schema.json. Do not edit.
#pragma once
#include <cmath>
#include <cstdint>
#include <optional>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace lightoverleaf::rpc
{
struct KValue
{
    using KObject = std::map<std::string, KValue>;
    using KArray = std::vector<KValue>;
    using KStorage = std::variant<std::nullptr_t, bool, double, std::string, KObject, KArray>;
    KStorage m_value = nullptr;
};
''' + cpp_dto(schema) + '\n\n'.join(functions) + f'''
inline bool validatePing(const KValue& input)
{{
    return {entry}(input);
}}
}}
'''


def ts_type(schema):
    if 'const' in schema:
        return json.dumps(schema['const'])
    if schema['type'] == 'string':
        return 'string'
    if schema['type'] == 'integer':
        return 'number'
    properties = schema.get('properties', {})
    if not properties:
        return 'Record<string, never>'
    fields = [json.dumps(key) + ('' if key in schema['required'] else '?') + ': ' + ts_type(value)
              for key, value in properties.items()]
    return '{ ' + '; '.join(fields) + ' }'


def generate_ts(schema):
    return '''// Generated. Do not edit.
''' + 'export type PingRequest = ' + ts_type(schema) + ';\n' + '''export interface NativeApi { ping(request: PingRequest): Promise<void>; }
export const pingMethod = "system.ping" as const;
type Schema = {
  $schema?: string; $id?: string; title?: string; additionalProperties?: boolean;
  type: string; const?: unknown; minimum?: number; maximum?: number;
  minLength?: number; maxLength?: number; pattern?: string;
  properties?: Record<string, Schema>; required?: string[];
};
const schema: Schema = ''' + json.dumps(schema) + ''';
function validate(value: unknown, rule: Schema): boolean {
  if (rule.type === "object") {
    if (value === null || typeof value !== "object" || Array.isArray(value)) return false;
    const record = value as Record<string, unknown>;
    const properties = rule.properties ?? {};
    return (rule.required ?? []).every(key => Object.hasOwn(record, key))
      && Object.entries(record).every(([key, item]) =>
        Object.hasOwn(properties, key) && validate(item, properties[key]));
  }
  if (rule.type === "integer") {
    if (typeof value !== "number" || !Number.isSafeInteger(value)) return false;
    if (rule.minimum !== undefined && value < rule.minimum) return false;
    if (rule.maximum !== undefined && value > rule.maximum) return false;
  } else if (rule.type === "string") {
    if (typeof value !== "string") return false;
    if (rule.minLength !== undefined && value.length < rule.minLength) return false;
    if (rule.maxLength !== undefined && value.length > rule.maxLength) return false;
    if (rule.pattern && !/^[a-zA-Z0-9-]+$/.test(value)) return false;
    if (rule.pattern && /[^a-zA-Z0-9-]/.test(value)) return false;
  } else return false;
  return !Object.hasOwn(rule, "const") || value === rule.const;
}
export function validatePing(value: unknown): value is PingRequest {
  return validate(value, schema);
}
'''


def write(path, text, check):
    if check:
        if not path.exists() or path.read_text(encoding='utf-8') != text:
            raise SystemExit(f'Generated output differs: {path}')
    else:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='utf-8', newline='\n')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cpp-out', type=Path, default=ROOT / 'out/.generated')
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    schema = json.loads(SCHEMA.read_text(encoding='utf-8'))
    verify(schema)
    # M0's DTO shape is fixed; reject schema edits that require extending the compiler.
    expected = ['version', 'id', 'method', 'params', 'clientSequence']
    if list(schema['properties']) != expected or schema['properties']['method']['const'] != 'system.ping' or schema['properties']['version']['const'] != 1:
        raise ValueError('Extend DTO generation before changing the M0 shape')
    write(args.cpp_out / 'krpccontract.h', generate_cpp(schema), args.check)
    write(ROOT / 'frontend/web/.generated/rpc/contract.ts', generate_ts(schema), args.check)
    tests = ['#include <krpccontract.h>', '#include <iostream>',
             'using namespace lightoverleaf::rpc;', 'int main()', '{', '    int failures = 0;']
    fixtures = json.loads((ROOT / 'tests/fixtures/ping.json').read_text(encoding='utf-8'))
    for fixture in fixtures:
        tests.extend([
            '    if (validatePing(' + cpp_literal(fixture['request']) + ') != ' + str(fixture['accepted']).lower() + ')',
            '    {', '        std::cerr << "' + fixture['name'] + '\\n";',
            '        ++failures;', '    }'])
    tests.extend(['    return failures == 0 ? 0 : 1;', '}', ''])
    write(args.cpp_out / 'kcontracttests.cpp', '\n'.join(tests), args.check)
    print(f'Contract codegen OK ({len(fixtures)} shared fixtures)')
    from protocol import generate_protocol
    generate_protocol(ROOT, args.cpp_out, write, args.check)


if __name__ == '__main__':
    main()

