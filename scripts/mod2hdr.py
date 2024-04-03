#!/usr/bin/env python

import re
import os

os.chdir(os.path.join(os.path.abspath(os.path.dirname(__file__)), '..'))

modules = {}
with open('cmake/clang_std_modules_source/std.hpp', 'r') as f:
    modules['std'] = {'dependencies': [], 'source': f.read().removeprefix('#pragma once\n')}

GLOBAL_MODULE_FRAGMENT = re.compile(r'^\s*module;$')
IMPORT_STD = re.compile(r'^\s*import\s+std;$')
EXPORT_MODULE = re.compile(r'^\s*export\s+module\s+([a-zA-Z0-9_.]+)(:[a-zA-Z0-9_.]+)?;$')
IMPORT = re.compile(r'^\s*(export\s+)?import\s+(:)?([a-zA-Z0-9_.]+);$')
EXPORT = re.compile(r'^(\s*export\s+)')

def process(dir):
    for dirpath, dirnames, filenames in os.walk(dir):
        for filename in filenames:
            filepath = os.path.join(dirpath, filename)
            if filepath.endswith('.cppm'):
                with open(filepath, 'r') as f:
                    current = None
                    parent = None
                    this_module = {'dependencies': [], 'source': ''}
                    this_module['source'] += '// #line 1 "' + filepath + '"\n'
                    for line in f:
                        line = line.removesuffix('\n')
                        if 0: pass
                        elif m := re.match(GLOBAL_MODULE_FRAGMENT, line):
                            line = ''
                        elif m := re.match(EXPORT_MODULE, line):
                            parent, partition = m.groups()
                            if partition:
                                current = parent + partition
                            else:
                                current = parent
                            line = ''
                        elif m := re.match(IMPORT_STD, line):
                            this_module['dependencies'].append('std')
                            line = ''
                        elif m := re.match(IMPORT, line):
                            export, colon, partition = m.groups()
                            if colon:
                                assert parent
                                dependency = parent + ':' + partition
                            else:
                                dependency = partition
                            this_module['dependencies'].append(dependency)
                            line = ''
                        elif m := re.match(EXPORT, line):
                            export = m.group(0)
                            line = (' ' * len(export)) + line[len(export):]
                        this_module['source'] += line + '\n'
                    print(current)
                    if current:
                        modules[current] = this_module

def generate(out):
    visited = set()
    result = []

    def dump(current):
        if current in visited:
            return
        visited.add(current)
        module = modules[current]
        for dependency in module['dependencies']:
            dump(dependency)
        result.append(module['source'])

    for m in modules:
        dump(m)

    source = '\n'.join(result)
    source = '''#pragma once
    /// Automatically Generated (DO NOT EDIT THIS FILE)
    /// Source: https://github.com/archibate/co_async
    {}
    '''.format(source)
    with open(out, 'w') as f:
        f.write(source)

process('co_async')
generate('co_async.hpp')
