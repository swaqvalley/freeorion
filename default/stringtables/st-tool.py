#!/usr/bin/python
from __future__ import print_function
import argparse
import cgi
import datetime
import errno
import json
import os
import re
import subprocess
import time
import urlparse
import urllib2
import webbrowser
from BaseHTTPServer import HTTPServer, BaseHTTPRequestHandler
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

    @staticmethod 
    def sync(reference, source):
        entries = []

        for item in reference.items():
            if isinstance(item, StringTableEntry):
                if item.key in source:
                    source_item = source[item.key]
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

        return StringTable(source.fpath, source.language, source.notes, source.includes, entries)


class EditServerHandler(BaseHTTPRequestHandler):
    body = '''
<html>
    <head>
        <title>Editing {source}</title>
        <style>
            @import url("/mootools-inlineeditor.css");
            table {{
                table-layout: fixed;
                width: 100%;
            }}
            td {{
                vertical-align: top;
                white-space: pre-wrap;
            }}
            .status-recent td.translation {{
                background-color: lime;
                color: black;
            }}
            .status-stale td.translation {{
                background-color: yellow;
                color: black;
            }}
            .status-untranslated td.translation {{
                background-color: red;
                color: white;
            }}
            .entry td.source {{
                background-color: lightgrey;
                color: black;
            }}
        </style>
        <script type="text/javascript" src="/mootools-core.js"></script>
        <script type="text/javascript" src="/mootools-more.js"></script>
        <script type="text/javascript" src="/mootools-inlineeditor.js"></script>
        <script type="text/javascript" src="/mootools-inlineeditor-textarea.js"></script>
        <script type="text/javascript">
            function toggle_entries_by_status(status, visible) {{
                toggle = $$('.toggle-' + status + ' span')[0];
                visible = toggle.retrieve('visible', false);

                $$('.entry.status-' + status).each(function(entry) {{
                    if(entry.isVisible() != visible) {{
                        entry.hide();
                    }} else {{
                        entry.show();
                    }}
                }})

                if(!visible) {{
                    toggle.set('text', 'Show ' + status + ' translations');
                }} else {{
                    toggle.set('text', 'Hide ' + status + ' translations');
                }}

                toggle.store('visible', visible);
            }}

            function after_entry_save(label, response, editor) {{
                element = $(response.id);
                element.removeClass('status-recent');
                element.removeClass('status-stale');
                element.removeClass('status-untranslated');
                element.addClass('status-'+response.status);
            }}

            window.addEvent('domready', function() {{
                $$('.translation').each(function(entry){{ new InlineEditor.Textarea(entry, {{url: 'save', method: 'JSONPOST', empty_msg: '<i>untranslated</i>', onSuccess: after_entry_save}}); }});
            }})
        </script>
    </head>
    <body>
        <h1>Translate stringtable for {language}</h1>
        <fieldset>
        <legend>Display settings</legend>
            <button type="button" class="toggle-recent" onclick="javascript:toggle_entries_by_status('recent')"><span>Hide recent translations</span></button>
            <button type="button" class="toggle-stale" onclick="javascript:toggle_entries_by_status('stale')"><span>Hide stale translations</span></button>
            <button type="button" class="toggle-untranslated" onclick="javascript:toggle_entries_by_status('untranslated')"><span>Hide untranslated translations</span></button>
        </fieldset>
        <table>
            {entries}
        </table>
    </body>
    <script>
    </script>
</html>
    '''

    entry = '''
<tbody class="entry status-{status}" id="{key}">
    <tr><th colspan="2">{key}</th></tr>
    <tr class="notes"><td colspan="2">{notes}</td></tr>
    <tr><td class="source">{source}</td><td class="translation" data-id="{key}">{translation}</td></tr>
</tbody>
    '''

    def do_GET(self):
        parsed_path = urlparse.urlparse(self.path)

        if parsed_path.path == '/':
            return self.GET_stringtable()
        elif parsed_path.path == '/mootools-core.js':
            return self.GET_url('https://cdnjs.cloudflare.com/ajax/libs/mootools/1.6.0/mootools-core.min.js')
        elif parsed_path.path == '/mootools-more.js':
            return self.GET_url('https://cdnjs.cloudflare.com/ajax/libs/mootools-more/1.6.0/mootools-more.js')
        elif parsed_path.path == '/mootools-inlineeditor.js':
            return self.GET_url('https://raw.githubusercontent.com/reednj/InlineEditor/455a4afbe65b4c5b445115929c073d1455d3b91d/Source/InlineEditor.js')
        elif parsed_path.path == '/mootools-inlineeditor.css':
            return self.GET_url('https://raw.githubusercontent.com/reednj/InlineEditor/455a4afbe65b4c5b445115929c073d1455d3b91d/Source/InlineEditor.css')
        elif parsed_path.path == '/mootools-inlineeditor-textarea.js':
            return self.GET_url('https://raw.githubusercontent.com/reednj/InlineEditor/455a4afbe65b4c5b445115929c073d1455d3b91d/Source/InlineEditor.Textarea.js')
        else:
            self.send_response(404)
            self.end_headers()

    def GET_url(self, url):
        response = urllib2.urlopen(url)
        self.send_response(response.getcode())
        self.send_header('Content-Type', response.info().gettype())
        self.end_headers()

        for chunk in response:
            if '/mootools-inlineeditor-textarea.js' in url:
                chunk = chunk.replace('this.edit_link.innerHTML = this.current_text;', '')
            self.wfile.write(chunk)

    def GET_stringtable(self):
        self.send_response(200)
        self.send_header('Content-Type', 'text/html ; charset=utf-8')
        self.end_headers()

        entries = []

        stat = StringTable.statistic(self.server.reference_st, self.server.source_st)

        for key in self.server.reference_st.keys():
            source = self.server.reference_st[key].value
            source = cgi.escape(source)

            translation = self.server.source_st[key].value if key in self.server.source_st else ''

            status = 'recent'
            if key not in stat.left_older:
                status = 'stale'
            if not translation:
                status = 'untranslated'

            translation = translation if translation else ''
            translation = cgi.escape(translation)

            notes = self.server.reference_st[key].notes
            notes = '\n'.join(notes)
            notes = cgi.escape(notes)

            entries.append(EditServerHandler.entry.format(
                key=key,
                source=source,
                translation=translation,
                notes=notes,
                status=status
            ))

        self.wfile.write(EditServerHandler.body.format(
            source=self.server.source_st.fpath,
            language=self.server.source_st.language,
            entries=''.join(entries)
        ))

    def POST_error(self, message):
        self.send_response(500)
        self.send_header('Content-Type', 'application/json')
        self.end_headers()

        self.wfile.write(json.dumps(
            { 'code': 500, 'message': message }))

    def do_POST(self):
        try:
            content_length = int(self.headers.get('Content-Length', '0'))
            body = self.rfile.read(content_length)
            request = json.loads(body)
            if 'id' not in request or 'value' not in request:
                raise ValueError("Some of the expected json keys are missing")
        except ValueError:
            self.POST_error("Browser didn't send a save request")
            raise

        if request['id'] not in self.server.reference_st:
            self.POST_error("No entry with key {}".format(request['id']))

        entry = self.server.source_st[request['id']]
        rentry = self.server.reference_st[request['id']]

        if entry.value != request['value']:
            if request['value'] and rentry.value != request['value']:
                entry.value = request['value']
            else:
                entry.value = None
            if entry.value:
                entry.value_times = [int(time.time())] * len(entry.value.split('\n'))
            else:
                entry.value_times = rentry.value_times

        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.end_headers()

        status = 'recent'
        if max(self.server.reference_st[request['id']].value_times) < min(entry.value_times):
            status = 'stale'
        if not entry.value:
            status = 'untranslated'

        response = {
            'id': request['id'],
            'status': status,
        }

        self.wfile.write(json.dumps(response))


def format_action(args):
    source_st = StringTable.from_file(args.source)

    print(source_st, end='')


def sync_action(args):
    reference_st = StringTable.from_file(args.reference)
    source_st = StringTable.from_file(args.source)

    out_st = StringTable.sync(reference_st, source_st)

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


def edit_action(args):
    reference_st = StringTable.from_file(args.reference)
    source_st = StringTable.from_file(args.source)

    source_st = StringTable.sync(reference_st, source_st)

    server = HTTPServer(('localhost', 8080), EditServerHandler)
    server.reference_st = reference_st
    server.source_st = source_st
    webbrowser.open('http://localhost:8080/')
    server.serve_forever()


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

    edit_parser = verb_parsers.add_parser('edit', help="edit a stringtable and exit")
    edit_parser.set_defaults(action=edit_action)
    edit_parser.add_argument('reference', metavar='REFERENCE', help="reference string table")
    edit_parser.add_argument('source', metavar='SOURCE', help="string table to edit")

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
