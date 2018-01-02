#!/usr/bin/python
from __future__ import print_function
import argparse
import datetime
import errno
import os
import re
import subprocess
from collections import namedtuple, OrderedDict

INTERNAL_REFERENCE_PATTERN = r'\[\[((?:(?:encyclopedia|(?:building|field|meter)type|predefinedshipdesign|ship(?:hull|part)|special|species|tech) )?)({})\]\]'

class StringTableEntry(object):
    def __init__(self, key, keyline, value, notes, value_times):
        if not re.match('^[A-Z0-9_]+$', key):
            raise ValueError("Key doesn't match expected format [A-Z0-9_]+, was '{}'".format(key))
        self.key = key
        self.keyline = keyline
        self.value = value
        self.value_times = value_times
        self.notes = notes

    def __str__(self):
        result = ""
        for note in self.notes:
            result += '# ' + note + "\n"
        if self.value is not None:
            result += self.key + "\n"
            if not self.value or '\n' in self.value or re.search(r'^\s', self.value) or re.search(r'\s$', self.value):
                result += "'''" + self.value + "'''" + "\n"
            else:
                result += self.value + "\n"
        else:
            result += "#*" + self.key + "\n"
            for e in self.value_times:
                result += "#* <not translated>\n"

        return result

    def __repr__(self):
        return "StringTableEntry(key={}, keyline={}, value={})".format(self.key, self.keyline, self.value)


class StringTable(object):
    def __init__(self, fpath, language, notes, includes, entries):
        self.fpath = fpath
        self.language = language
        self.notes = notes
        self.includes = includes
        self._entries = entries
        self._ientries = OrderedDict()

        for entry in self._entries:
            if isinstance(entry, StringTableEntry):
                if entry.key in self._ientries:
                    msg = "{fpath}:{fline}: Duplicate key '{key}', previously defined on line {pline}"
                    msg = msg.format(**{
                        'fpath': self.fpath,
                        'fline': entry.keyline,
                        'key': entry.key,
                        'pline': self._ientries[entry.key].keyline,
                    })
                    raise ValueError(msg)
                self._ientries[entry.key] = entry

    def __getitem__(self, key):
        return self._ientries[key]

    def __contains__(self, key):
        return key in self._ientries

    def __iter__(self):
        return self._ientries.iteritems()

    def __len__(self):
        return len(self._ientries)

    def __str__(self):
        result = ""
        result += self.language + "\n"

        if self.notes:
            result += "\n"
        for note in self.notes:
            if note:
                result += "# " + note + "\n"
            else:
                result += "#\n"

        for entry in self._entries:
            if isinstance(entry, StringTableEntry):
                result += "\n"
                result += str(entry)
            elif isinstance(entry, list):
                result += "\n"
                result += "\n"
                result += "##\n"
                for line in entry:
                    if line:
                        result += "## " + line + "\n"
                    else:
                        result += "##\n"
                result += "##\n"

        if self.includes:
            result += "\n"

        for include in self.includes:
            result += "#include \"" + include + "\"\n"

        return result

    def __repr__(self):
        return "StringTable(fpath={}, language={}, entries={})".format(
            self.fpath, self.language, self.entries)

    def items(self):
        return self._entries

    def keys(self):
        return self._ientries.keys()

    @staticmethod
    def from_file(fpath):
        committer_time = None
        fline = 0
        vline = 0
        is_quoted = False

        language = None
        fnotes = []
        section = []
        entries = []
        includes = []

        key = None
        keyline = None
        value = None
        value_times = []
        notes = []
        untranslated_key = None
        untranslated_keyline = None
        untranslated_lines = []

        if not os.path.isfile(fpath):
            raise EnvironmentError(
                errno.ENOENT, os.strerror(errno.ENOENT), fpath)

        body = subprocess.check_output(['git', 'blame', '--line-porcelain', fpath])
        body = body.splitlines(True)

        for line in body:
            if line.startswith('committer-time'):
                committer_time = int(line.strip().split(' ')[1])
                continue
            if not line.startswith('\t'):
                # discard other blame metadata
                continue

            line = line[1:]
            fline = fline + 1

            if not is_quoted and not line.strip():
                # discard empty lines when not in quoted value
                if section:
                    while not section[0]:
                        section.pop(0)
                    while not section[-1]:
                        section.pop(-1)
                    entries.append(section)
                    section = []
                if language and notes and not entries:
                    fnotes = notes
                    notes = []
                if untranslated_key:
                    try:
                        entries.append(StringTableEntry(untranslated_key, untranslated_keyline, None, notes, untranslated_lines))
                    except ValueError, e:
                        raise ValueError("{}:{}: {}".format(fpath, keyline, str(e)))
                    untranslated_key = None
                    untranslated_keyline = None
                    notes = []
                    untranslated_lines = []
                vline = fline
                continue

            if not is_quoted and line.startswith('#'):
                # discard comments when not in quoted value
                if line.startswith('#include'):
                    includes.append(line[8:].strip()[1:-1].strip())
                if line.startswith('##'):
                    section.append(line[3:].rstrip())
                elif line.startswith('#*') and not untranslated_key:
                    untranslated_key = line[2:].strip()
                    untranslated_keyline = fline
                elif line.startswith('#*'):
                    untranslated_lines.append(0)
                elif line.startswith('#'):
                    notes.append(line[2:].rstrip())
                vline = fline
                continue

            if not language:
                # read first non comment and non empty line as language name
                language = line.strip()
                vline = fline
                continue

            if not key:
                if section:
                    while not section[0]:
                        section.pop(0)
                    while not section[-1]:
                        section.pop(-1)
                    entries.append(section)
                    section = []
                if language and notes and not entries:
                    fnotes = notes
                    notes = []
                if untranslated_key:
                    try:
                        entries.append(StringTableEntry(untranslated_key, untranslated_keyline, None, notes, untranslated_lines))
                    except ValueError, e:
                        raise ValueError("{}:{}: {}".format(fpath, keyline, str(e)))
                    untranslated_key = None
                    untranslated_keyline = None
                    notes = []
                    untranslated_lines = []
                # read non comment and non empty line after language and after
                # completing an stringtable language
                key = line.strip().split()[0]
                keyline = fline
                vline = fline
                continue

            if not is_quoted:
                if line.startswith("'''"):
                    # prepare begin of quoted value
                    line = line[3:]
                    is_quoted = True
                else:
                    # prepare unquoted value
                    line = line.strip()

            if is_quoted and line.rstrip().endswith("'''"):
                # prepare end of quoted value
                line = line.rstrip()[:-3]
                is_quoted = False

            # extract value or concatenate quoted value
            value = (value if value else '') + line
            value_times.append(committer_time)

            if not is_quoted and key is not None and value is not None:
                # consume collected string table entry
                try:
                    entries.append(StringTableEntry(key, keyline, value, notes, value_times))
                except ValueError, e:
                    raise ValueError("{}:{}: {}".format(fpath, keyline, str(e)))

                key = None
                keyline = None
                value = None
                notes = []
                value_times = []
                vline = fline
                continue

            if is_quoted:
                # continue consuming quoted value lines
                continue

            raise ValueError("{}:{}: Impossible parser state; last valid line: {}".format(fpath, fline, line))

        if key:
            raise ValueError("{}:{}: Key '{}' without value".format(fpath, vline, key))

        if is_quoted:
            raise ValueError("{}:{}: Quotes not closed for key '{}'".format(fpath, vline, key))

        return StringTable(fpath, language, fnotes, includes, entries)
   
    @staticmethod
    def statistic(left, right):
        STStatistic = namedtuple('STStatistic', [
            'left', 'left_only', 'left_older', 'right', 'right_only', 'right_older', 'untranslated'])

        all_keys = set(left.keys()).union(set(right.keys()))

        untranslated = set()
        left_only = set()
        right_only = set()
        left_older = set()
        right_older = set()

        for key in all_keys:
            if key in left and key not in right:
                left_only.add(key)
            if key not in left and key in right:
                right_only.add(key)
            if key in left and key in right:
                if left[key].value == right[key].value:
                    untranslated.add(key)
                if min(left[key].value_times) > max(right[key].value_times):
                    right_older.add(key)
                if max(left[key].value_times) < min(right[key].value_times):
                    left_older.add(key)

        return STStatistic(
            left=left, left_only=left_only, left_older=left_older,
            right=right, right_only=right_only, right_older=right_older,
            untranslated=untranslated)


def format_action(args):
    source_st = StringTable.from_file(args.source)

    print(source_st, end='')


def sync_action(args):
    reference_st = StringTable.from_file(args.reference)
    source_st = StringTable.from_file(args.source)

    entries = []

    for item in reference_st.items():
        if isinstance(item, StringTableEntry):
            if item.key in source_st:
                source_item = source_st[item.key]
                entries.append(StringTableEntry(
                    item.key,
                    item.keyline,
                    source_item.value,
                    item.notes,
                    source_item.value_times
                ))
            else:
                entries.append(StringTableEntry(
                    item.key,
                    item.keyline,
                    None,
                    item.notes,
                    item.value_times
                ))
        else:
            entries.append(item)

    out_st = StringTable(source_st.fpath, source_st.language, source_st.notes, source_st.includes, entries)

    print(out_st, end='')


def rename_key_action(args):
    if not re.match(r'^[A-Z][A-Z0-9_]*$', args.old_key):
        raise ValueError("The given old key '{}' is not a valid name".format(args.old_key))

    if not re.match(r'^[A-Z][A-Z0-9_]*$', args.new_key):
        raise ValueError("The given new key '{}' is not a valid name".format(args.new_key))

    source_st = StringTable.from_file(args.source)

    for key in source_st.keys():
        entry = source_st[key]
        entry.value = re.sub(
            INTERNAL_REFERENCE_PATTERN.format(re.escape(args.old_key)),
            r'[[\1' + args.new_key + ']]',
            entry.value)
        if entry.key == args.old_key:
            entry.key = args.new_key

    print(source_st, end='')


def check_action(args):
    source_st = StringTable.from_file(args.source)

    for key in source_st.keys():
        entry = source_st[key]

        for match in re.findall(INTERNAL_REFERENCE_PATTERN.format('.*?'), entry.value):
            match = match[1]
            if match not in source_st.keys():
                print("{}:{}: Referenced key '{}' in value of '{}' was not found.".format(source_st.fpath, entry.keyline, match, entry.key))


def compare_action(args):
    reference_st = StringTable.from_file(args.reference)
    source_st = StringTable.from_file(args.source)

    comp = StringTable.statistic(reference_st, source_st)

    if not args.summary_only:
        for key in source_st.keys():
            if key in comp.right_only:
                print("{}:{}: Key '{}' is not in reference file {}".format(comp.right.fpath, comp.right[key].keyline, key, comp.left.fpath))

            if key in comp.right_older:
                print("{}:{}: Value of key '{}' is older than reference file value {}:{}".format(comp.right.fpath, comp.right[key].keyline, key, comp.left.fpath, comp.left[key].keyline))

            if key in comp.untranslated:
                print("{}:{}: Value of key '{}' is same as reference file {}:{}".format(comp.right.fpath, comp.right[key].keyline, key, comp.left.fpath, comp.left[key].keyline))

    print("""
Summary comparing '{}' against '{}':
    Keys translated - {}/{} ({:3.1f}%)
    Keys not in reference - {}
    Values older than reference - {}
    Values same as reference - {}
""".strip().format(
        comp.right.fpath,
        comp.left.fpath,
        len(comp.right) - len(comp.right_only),
        len(comp.left),
        100.0 * (len(comp.right) - len(comp.right_only)) / len(comp.left),
        len(comp.right_only),
        len(comp.right_older),
        len(comp.untranslated)))


if __name__ == "__main__":
    root_parser = argparse.ArgumentParser()
    verb_parsers = root_parser.add_subparsers()

    format_parser = verb_parsers.add_parser('format', help="format a string table and exit")
    format_parser.set_defaults(action=format_action)
    format_parser.add_argument('source', metavar='SOURCE', help="string table to format")

    sync_parser = verb_parsers.add_parser('sync', help="synchronize two string tables and exit")
    sync_parser.set_defaults(action=sync_action)
    sync_parser.add_argument('reference', metavar='REFERENCE', help="reference string table")
    sync_parser.add_argument('source', metavar='SOURCE', help="string table to sync")

    rename_key_parser = verb_parsers.add_parser('rename-key', help="rename all occurences of a key within a stringtable and exit")
    rename_key_parser.set_defaults(action=rename_key_action)
    rename_key_parser.add_argument('source', metavar='SOURCE', help="string table to rename old key within")
    rename_key_parser.add_argument('old_key', metavar='OLD_KEY', help="key to rename")
    rename_key_parser.add_argument('new_key', metavar='NEW_KEY', help="new key name")

    check_parser = verb_parsers.add_parser('check', help="check a stringtable for consistency and exit")
    check_parser.set_defaults(action=check_action)
    check_parser.add_argument('source', metavar='SOURCE', help="string table to check")

    compare_parser = verb_parsers.add_parser('compare', help="compare two string tables and exit")
    compare_parser.set_defaults(action=compare_action)
    compare_parser.add_argument('-s', '--summary-only', help="print only a summary of keys", action='store_true', dest='summary_only')
    compare_parser.add_argument('reference', metavar='REFERENCE', help="reference string table")
    compare_parser.add_argument('source', metavar='SOURCE', help="string table to compare")

    args = root_parser.parse_args()
    args.action(args)
