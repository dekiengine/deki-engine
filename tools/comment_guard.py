"""Checks that a change to C++ files touched comments only.

Each file is compared with its committed version after comments and all
whitespace are removed; anything else that differs is reported. Markers that
tools read from comments (NOLINT, clang-format off/on, deki-alloc-ok) must
keep their count.

Usage: python comment_guard.py <file>... | @<list file>   (paths inside git repos)
Exit status 1 when a file changed more than its comments.
"""
import os
import re
import subprocess
import sys

MARKERS = ('NOLINT', 'clang-format off', 'clang-format on', 'deki-alloc-ok')


def strip_comments(text):
    """The code without comments. Strings, characters and raw strings keep
    their contents, so a '//' inside a string is not taken for a comment."""
    out = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c == '/' and i + 1 < n and text[i + 1] == '/':
            j = text.find('\n', i)
            i = n if j < 0 else j
        elif c == '/' and i + 1 < n and text[i + 1] == '*':
            j = text.find('*/', i + 2)
            i = n if j < 0 else j + 2
            out.append(' ')
        elif c == 'R' and i + 1 < n and text[i + 1] == '"' and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == '_')):
            m = re.match(r'R"([^(\s]{0,16})\(', text[i:])
            if m:
                end = text.find(')' + m.group(1) + '"', i + m.end())
                end = n if end < 0 else end + len(m.group(1)) + 2
                out.append(text[i:end])
                i = end
            else:
                out.append(c)
                i += 1
        elif c in '"\'':
            j = i + 1
            while j < n and text[j] != c:
                if text[j] == '\\':
                    j += 1
                if text[j] == '\n':
                    break
                j += 1
            out.append(text[i:j + 1])
            i = j + 1
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def joined_comments(text):
    """// comments ending in a backslash: the next line joins the comment, so
    the code on it disappears without the stripped text showing it."""
    count = 0
    for line in text.splitlines():
        s = line.rstrip()
        if '//' in s and s.endswith(chr(92)) and not s.split('//', 1)[0].strip().startswith('#'):
            count += 1
    return count


def squeeze(text):
    return re.sub(r'\s+', '', strip_comments(text))


def committed(path):
    folder = os.path.dirname(os.path.abspath(path)) or '.'
    top = subprocess.run(['git', '-C', folder, 'rev-parse', '--show-toplevel'], capture_output=True, text=True).stdout.strip()
    rel = os.path.relpath(os.path.abspath(path), top).replace('\\', '/')
    r = subprocess.run(['git', '-C', top, 'show', 'HEAD:' + rel], capture_output=True)
    return r.stdout.decode('utf-8', 'replace') if r.returncode == 0 else None


paths = []
for arg in sys.argv[1:]:
    if arg.startswith('@'):
        paths += [line.strip() for line in open(arg[1:]) if line.strip()]
    else:
        paths.append(arg)

bad = 0
for path in paths:
    before = committed(path)
    if before is None:
        continue  # a new file: nothing to compare against
    after = open(path, encoding='utf-8', errors='replace').read()
    problems = []
    if squeeze(before) != squeeze(after):
        a, b = squeeze(before), squeeze(after)
        k = next((i for i in range(min(len(a), len(b))) if a[i] != b[i]), min(len(a), len(b)))
        problems.append('code changed near: ...%s...  ->  ...%s...' % (a[max(0, k - 40):k + 40], b[max(0, k - 40):k + 40]))
    if joined_comments(after) > joined_comments(before):
        problems.append('a // comment now ends in a backslash, which swallows the next line')
    for marker in MARKERS:
        if before.count(marker) != after.count(marker):
            problems.append('"%s" count changed (%d -> %d)' % (marker, before.count(marker), after.count(marker)))
    if problems:
        bad += 1
        print('FAIL ' + path)
        for p in problems:
            print('     ' + p)
print('%d file(s) checked, %d failed' % (len(paths), bad))
sys.exit(1 if bad else 0)
