#!/usr/bin/env python3
"""Generate include/descnum.h from the bundled descriptor.proto.

The compiler is self-hosting: it parses its own bundled
google/protobuf/descriptor.proto at startup and uses the resulting descriptors
for everything afterwards. But the code that emits the *first*
FileDescriptorProto runs before any descriptor exists, so it needs the field
numbers of descriptor.proto as constants. Generating them from the bundled
source keeps them honest: change the bundled protoc release and the constants
follow.

Usage: gen_descnum.py <descriptor.proto> <out.h>
"""
import re
import sys

MESSAGES = [
    'FileDescriptorSet', 'FileDescriptorProto', 'DescriptorProto',
    'DescriptorProto.ExtensionRange', 'DescriptorProto.ReservedRange',
    'FieldDescriptorProto', 'OneofDescriptorProto', 'EnumDescriptorProto',
    'EnumDescriptorProto.EnumReservedRange', 'EnumValueDescriptorProto',
    'ServiceDescriptorProto', 'MethodDescriptorProto', 'FileOptions',
    'MessageOptions', 'FieldOptions', 'OneofOptions', 'EnumOptions',
    'EnumValueOptions', 'ServiceOptions', 'MethodOptions', 'SourceCodeInfo',
    'SourceCodeInfo.Location', 'UninterpretedOption',
    'UninterpretedOption.NamePart', 'FeatureSet', 'ExtensionRangeOptions',
]


def parse(path):
    src = open(path, encoding='utf-8').read()
    src = re.sub(r'//[^\n]*', '', src)
    src = re.sub(r'/\*.*?\*/', '', src, flags=re.S)
    msg_re = re.compile(r'^\s*(?:message|enum)\s+(\w+)\s*\{')
    field_re = re.compile(r'^\s*(?:optional|required|repeated)\s+([\w.]+)\s+(\w+)\s*=\s*(\d+)')
    stack, out = [], {}
    for ln in src.split('\n'):
        m = msg_re.match(ln)
        if m:
            stack.append(m.group(1))
            out.setdefault('.'.join(stack), [])
        else:
            f = field_re.match(ln)
            if f and stack:
                out.setdefault('.'.join(stack), []).append(
                    (f.group(2), int(f.group(3)), f.group(1)))
        depth = ln.count('{') - ln.count('}')
        for _ in range(max(0, -depth)):
            if stack:
                stack.pop()
    return out


def main():
    parsed = parse(sys.argv[1])
    lines = [
        '/* descnum.h -- field numbers of descriptor.proto. GENERATED, do not edit.',
        ' * Regenerate with: tools/gen_descnum.py wkt/google/protobuf/descriptor.proto include/descnum.h',
        ' */',
        '#ifndef PB_DESCNUM_H', '#define PB_DESCNUM_H', '',
    ]
    total = 0
    for name in MESSAGES:
        fields = parsed.get(name)
        if not fields:
            lines.append(f'/* WARNING: {name} not found in descriptor.proto */')
            continue
        prefix = 'F_' + name.replace('.', '_').upper()
        lines.append(f'/* {name} */')
        for fname, num, ftype in fields:
            lines.append(f'#define {prefix}_{fname.upper():<28} {num:<4} /* {ftype} */')
            total += 1
        lines.append('')
    lines.append('#endif /* PB_DESCNUM_H */')
    open(sys.argv[2], 'w').write('\n'.join(lines) + '\n')
    print(f'wrote {sys.argv[2]}: {total} constants across {len(MESSAGES)} messages')


if __name__ == '__main__':
    main()
