#!/usr/bin/python2

from __future__ import print_function
from distutils.spawn import find_executable
from subprocess import Popen, PIPE
import argparse
import difflib
import json
import os
import yaml


def exec_read(cmd):
    proc = Popen(cmd, stdout=PIPE)
    result = []
    while True:
        line = proc.stdout.readline()
        if line:
            result.append(line)
        if not line and not proc.poll():
            break
    return result


def collect_format_differences(file_path, args):
    original = None
    with open(file_path, 'r') as f:
        original = f.readlines()
    formatted = exec_read([args.clang_format_executable, '-style='+args.clang_format, '-sort-includes=false', file_path])
    original_lno = 0
    formatted_lno = 0
    hunks = []
    current_hunk = None
    for line in difflib.ndiff(original, formatted):
        if line.startswith(' ') or line.startswith('-'):
            original_lno += 1
        if line.startswith(' ') or line.startswith('+'):
            formatted_lno += 1
        if line.startswith(' '):
            if current_hunk:
                hunks.append(current_hunk)
                current_hunk = None
            continue
        if line.startswith('?'):
            continue
        if not current_hunk:
            current_hunk = {
                'from': original_lno,
                'to': original_lno,
                'added': '',
                'removed': ''
            }
        if line.startswith('+'):
            current_hunk['added'] += line[2:]
        if line.startswith('-'):
            current_hunk['removed'] += line[2:]
            current_hunk['to'] = original_lno
    return hunks


def print_gcc_report(file_path, differences):
    for hunk in differences:
        print('{}:{}: Formatting difference, clang-format expected:\n{}'.format(
            file_path,
            hunk['from'],
            hunk['added']
        ))


def main():
    arg_parser = argparse.ArgumentParser(
        description='Report differences between current and expected C++ format'
    )
    arg_parser.add_argument(
        '--config', '-c', nargs='?', type=argparse.FileType('r'),
        default='.clang-config', help='A clang-format configuration'
    )
    arg_parser.add_argument(
        '--clang-format-executable', default='clang-format',
        help='Name or path to the clang-format executable'
    )
    arg_parser.add_argument(
        'files', nargs='+', help='A list of files to scan for mismatches'
    )

    args = arg_parser.parse_args()

    if 'clang-format' == args.clang_format_executable:
        args.clang_format_executable = find_executable('clang-format')
    if not args.clang_format_executable:
        print("Unable to locate `clang-format` executable.", file=sys.stderr)
        exit(1)

    args.clang_format = json.dumps(yaml.load(args.config))
    report = print_gcc_report

    for file_path in args.files:
        differences = collect_format_differences(file_path, args)
        report(file_path, differences)


if __name__ == "__main__":
    main()
