#!/usr/bin/env python3
# Copyright (C) 2026 Steve Cheng.
#
# This file is part of SquirrelVNC, a modified version of the TightVNC
# software (https://www.tightvnc.com/). It is free software under the GNU
# General Public License, version 2 or later; see LICENSE.txt.

# Summarizes a Very Sleepy profile (.sleepy) by CPU time: see
# notes/cpu-usage.md. Samples whose innermost frame is a blocking system
# call (waiting for an object, a message, an I/O completion, a sleep) are
# dropped, since Very Sleepy counts wall time, and a thread blocked in the
# kernel is sampled just like a thread that runs.
#
# Limits: a sampler in user mode cannot tell whether a thread inside any
# other system call (NtGdiBitBlt, NtUserInvalidateRect...) was running in
# the kernel or blocked there; such samples are kept, and shown as they
# are. Very Sleepy does not always walk the stack to the thread's start
# (code without unwind information, such as libjpeg-turbo's assembly), so
# a few stacks end at garbage addresses.
#
# Usage: sleepy-cpu.py capture.sleepy [options]   (see --help)
# Needs only Python 3's standard library.

import argparse
import collections
import io
import re
import sys
import zipfile

# Innermost frames that mean the thread was blocked, not running.
DEFAULT_WAIT = (r'^(Nt|Zw)(Wait|RemoveIoCompletion|DelayExecution|'
                r'SignalAndWait|UserGetMessage|UserWaitMessage|'
                r'UserMsgWaitForMultipleObjects|ReplyWaitReceivePort|'
                r'AlpcSendWaitReceivePort)')


class Profile:
    def __init__(self, path):
        with zipfile.ZipFile(path) as z:
            def lines(name):
                with z.open(name) as f:
                    return [l.rstrip('\r\n')
                            for l in io.TextIOWrapper(f, 'utf-8', 'replace')]
            # address "module" "function" "file" line
            self.symbols = {}
            pattern = re.compile(r'(\S+) "(.*?)" "(.*?)" "(.*?)" (\d+)$')
            for line in lines('Symbols.txt'):
                m = pattern.match(line.strip())
                if m:
                    self.symbols[int(m[1], 16)] = (m[2], m[3], m[4], int(m[5]))
            # Pairs of lines: the stack, innermost first; then pairs of
            # thread ID and seconds spent in that stack.
            self.samples = []
            stacks = lines('Callstacks.txt')
            for i in range(0, len(stacks) - 1, 2):
                frames = tuple(int(a, 16) for a in stacks[i].split())
                fields = stacks[i + 1].split()
                for j in range(0, len(fields) - 1, 2):
                    self.samples.append((frames, fields[j], float(fields[j + 1])))
            self.stats = dict(l.split(': ', 1) for l in lines('Stats.txt')
                              if ': ' in l)

    def function(self, address):
        symbol = self.symbols.get(address)
        return symbol[1] if symbol else '[%X]' % address

    def location(self, address):
        symbol = self.symbols.get(address)
        if not symbol:
            return '[%X]' % address
        file = symbol[2].replace('\\', '/').rsplit('/', 1)[-1]
        return '%s  %s:%d' % (symbol[1], file, symbol[3]) if file else symbol[1]


def table(title, rows, total, limit):
    print('\n== %s' % title)
    for name, seconds in sorted(rows.items(), key=lambda r: -r[1])[:limit]:
        print('%9.3f s %6.2f%%  %s' % (seconds, 100 * seconds / total if total else 0,
                                       name))


def main():
    parser = argparse.ArgumentParser(
        description='CPU-time summary of a Very Sleepy .sleepy profile')
    parser.add_argument('profile')
    parser.add_argument('-n', '--top', type=int, default=30,
                        help='rows per table (default 30)')
    parser.add_argument('-t', '--thread', action='append',
                        help='only this thread ID (repeatable)')
    parser.add_argument('--wait', default=DEFAULT_WAIT,
                        help='regex of innermost functions counted as waiting')
    parser.add_argument('--keep-waits', action='store_true',
                        help='do not drop waiting samples')
    parser.add_argument('-f', '--function', action='append', default=[],
                        help='also show callers, callees and source lines of '
                             'functions matching this regex (repeatable)')
    parser.add_argument('--lines', action='store_true',
                        help='also show self time by source line')
    args = parser.parse_args()

    profile = Profile(args.profile)
    wait = re.compile(args.wait)

    total_by_thread = collections.defaultdict(float)
    cpu_by_thread = collections.defaultdict(float)
    samples = []
    for frames, thread, seconds in profile.samples:
        if args.thread and thread not in args.thread:
            continue
        total_by_thread[thread] += seconds
        if not args.keep_waits and frames and wait.search(profile.function(frames[0])):
            continue
        cpu_by_thread[thread] += seconds
        samples.append((frames, thread, seconds))

    cpu = sum(cpu_by_thread.values())
    print('Duration %s s, %s samples; %.3f s sampled, %.3f s not waiting'
          % (profile.stats.get('Duration', '?'), profile.stats.get('Samples', '?'),
             sum(total_by_thread.values()), cpu))

    # A thread is named by the outermost function of the program's own
    # code: the outermost frame not in a system library, the C runtime's
    # start-up code, a thread wrapper or Asio.
    plumbing = re.compile(r'^(asio::|thread_start|__scrt_|invoke_main|w?WinMain|'
                          r'w?main$|Thread::threadProc|std::)')
    def thread_name(thread):
        names = collections.Counter()
        for frames, t, seconds in samples:
            if t == thread:
                for a in reversed(frames):
                    symbol = profile.symbols.get(a)
                    if (symbol and symbol[2] and not symbol[2].startswith('[')
                            and not plumbing.search(symbol[1])):
                        names[symbol[1]] += seconds
                        break
        return names.most_common(1)[0][0] if names else ''

    print('\n== Threads (seconds not waiting / sampled)')
    for thread, seconds in sorted(cpu_by_thread.items(), key=lambda r: -r[1]):
        if seconds > 0:
            print('%9.3f s / %7.3f s  thread %-6s %s'
                  % (seconds, total_by_thread[thread], thread, thread_name(thread)))

    exclusive = collections.defaultdict(float)
    inclusive = collections.defaultdict(float)
    by_line = collections.defaultdict(float)
    for frames, thread, seconds in samples:
        if not frames:
            continue
        exclusive[profile.function(frames[0])] += seconds
        by_line[profile.location(frames[0])] += seconds
        for name in set(profile.function(a) for a in frames):
            inclusive[name] += seconds

    table('Exclusive (self) time', exclusive, cpu, args.top)
    table('Inclusive time', inclusive, cpu, args.top)
    if args.lines:
        table('Self time by source line', by_line, cpu, args.top)

    for pattern in args.function:
        match = re.compile(pattern)
        callers = collections.defaultdict(float)
        callees = collections.defaultdict(float)
        lines = collections.defaultdict(float)
        for frames, thread, seconds in samples:
            names = [profile.function(a) for a in frames]
            for i, name in enumerate(names):
                if match.search(name):
                    # Outermost matching frame only, so recursion counts once.
                    if any(match.search(n) for n in names[i + 1:]):
                        continue
                    callers[names[i + 1] if i + 1 < len(names) else '(top)'] += seconds
                    callees[names[i - 1] if i > 0 else '(self)'] += seconds
                    lines[profile.location(frames[i])] += seconds
                    break
        table('Callers of /%s/' % pattern, callers, cpu, args.top)
        table('Callees of /%s/ ("(self)" = time in it)' % pattern, callees, cpu, args.top)
        table('Lines of /%s/ (self time, or the call it was in)' % pattern,
              lines, cpu, args.top)


if __name__ == '__main__':
    sys.exit(main())
