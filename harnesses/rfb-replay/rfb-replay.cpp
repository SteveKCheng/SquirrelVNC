// Copyright (C) 2026 Steve Cheng.
//
//-------------------------------------------------------------------------
// This file is part of SquirrelVNC, a modified version of the TightVNC
// software (https://www.tightvnc.com/).
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//-------------------------------------------------------------------------
//

//
// rfb-replay: runs RemoteViewerCore on a recorded or generated stream of
// server-to-client bytes, with no network and no GUI, and writes or checks
// what the client sent back, the events it reported and its final frame
// buffer. See notes/testing.md.
//

#include "io-lib/ByteArrayInputStream.h"
#include "log-writer/Logger.h"
#include "network/RfbInputGate.h"
#include "network/RfbOutputGate.h"
#include "util/Exception.h"
#include "util/ScopeExit.h"
#include "util/TextEncoding.h"
#include "viewer-core/NetworkThread.h"
#include "viewer-core/RemoteViewerCore.h"

#include "FrameBufferPpm.h"
#include "LoopbackServer.h"
#include "MemoryOutputStream.h"
#include "ReplayAuthHandler.h"
#include "ReplayEventsAdapter.h"
#include "SplitInputStream.h"

#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>

#include <algorithm>
#include <future>
#include <memory>
#include <string.h>
#include <string>
#include <vector>

#ifdef _MSC_VER
#pragma warning(disable: 6262) // suppress warning on use of a ~64KB buffer on the stack
#endif

static const TCHAR USAGE[] =
  _T("Usage: rfb-replay [options] INPUT\n")
  _T("       rfb-replay [options] --check CASE\n")
  _T("\n")
  _T("  INPUT              server-to-client bytes, or - for stdin\n")
  _T("  --check CASE       run CASE.rfb and compare with CASE.out,\n")
  _T("                     CASE.events and CASE.ppm; the password is\n")
  _T("                     CASE.password in UTF-8, if it exists\n")
  _T("  --out FILE         client-to-server bytes (default without\n")
  _T("                     --check: stdout)\n")
  _T("  --events FILE      event log\n")
  _T("  --ppm FILE         final frame buffer as PPM\n")
  _T("  --password TEXT    for VNC authentication (default: password)\n")
  _T("  --max-pixels N     frame buffer limit (default: 4096 x 4096)\n")
  _T("  --timeout SECONDS  give up on a session that has not ended\n")
  _T("                     (default: 30)\n")
  _T("  --log LEVEL        core log on stderr, 0 to 9 (default: 1, errors)\n")
  _T("  --split N          give the core the input N bytes at a time\n")
  _T("  --seed S           with --split, pieces of random sizes from 1 to N,\n")
  _T("                     from a generator seeded with S\n")
  _T("  --tcp              serve the input over TCP on 127.0.0.1 and connect\n")
  _T("                     the core to it, as the viewer connects; the end of\n")
  _T("                     the input then reads \"Connection has been\n")
  _T("                     gracefully closed\", logged as \"End of stream\n")
  _T("                     reached\" for comparison\n")
  _T("\n")
  _T("Exit codes: 0 success or match, 1 mismatch, 2 usage or I/O error,\n")
  _T("3 timeout, 4 AddressSanitizer report (ReleaseAsan build).\n");

static const int EXIT_MATCH = 0;
static const int EXIT_MISMATCH = 1;
static const int EXIT_ERROR = 2;
static const int EXIT_TIMEOUT = 3;

#ifdef __SANITIZE_ADDRESS__
//
// In the ReleaseAsan build, an AddressSanitizer report ends the program
// with exit code 4, rather than ASan's default of 1, which would read as
// a mismatch.
//
extern "C" const char *__asan_default_options()
{
  return "exitcode=4";
}
#endif

//
// A Logger that prints the core's log on stderr, up to a level.
//
class StderrLogger final : public Logger
{
public:
  StderrLogger(int maxLevel) : m_maxLevel(maxLevel) {}

  void print(int logLevel, const TCHAR *line) override
  {
    _ftprintf(stderr, _T("[%d] %s\n"), logLevel, line);
  }

  bool acceptsLevel(int logLevel) override
  {
    return logLevel <= m_maxLevel;
  }

private:
  int m_maxLevel;
};

struct Options
{
  StringStorage input;
  StringStorage checkCase;
  StringStorage outFile;
  StringStorage eventsFile;
  StringStorage ppmFile;
  StringStorage password = _T("password");
  bool isPasswordGiven = false;
  UINT64 maxPixels = 4096 * 4096;
  DWORD timeoutMs = 30 * 1000;
  int logLevel = 1;
  // 0: the input in as few pieces as the core asks for.
  size_t splitPiece = 0;
  bool isSeedGiven = false;
  unsigned int seed = 0;
  bool isTcp = false;
};

//
// Parse the command line.
//
// Pre-conditions: options holds the defaults (a new Options).
// Post-conditions: returns true and fills options if the command line is
// valid; otherwise prints the problem and the usage, and returns false.
//
static bool parseOptions(int argc, TCHAR *argv[], Options *options)
{
  for (int i = 1; i < argc; i++) {
    const TCHAR *arg = argv[i];
    bool hasValue = i + 1 < argc;
    if (_tcscmp(arg, _T("--tcp")) == 0) {
      options->isTcp = true;
    } else if (arg[0] == _T('-') && arg[1] == _T('-')) {
      if (!hasValue) {
        _ftprintf(stderr, _T("Missing value for %s\n\n%s"), arg, USAGE);
        return false;
      }
      const TCHAR *value = argv[++i];
      if (_tcscmp(arg, _T("--check")) == 0) {
        options->checkCase = value;
      } else if (_tcscmp(arg, _T("--out")) == 0) {
        options->outFile = value;
      } else if (_tcscmp(arg, _T("--events")) == 0) {
        options->eventsFile = value;
      } else if (_tcscmp(arg, _T("--ppm")) == 0) {
        options->ppmFile = value;
      } else if (_tcscmp(arg, _T("--password")) == 0) {
        options->password = value;
        options->isPasswordGiven = true;
      } else if (_tcscmp(arg, _T("--max-pixels")) == 0) {
        options->maxPixels = _tcstoui64(value, nullptr, 10);
      } else if (_tcscmp(arg, _T("--timeout")) == 0) {
        options->timeoutMs = static_cast<DWORD>(_tcstoul(value, nullptr, 10)) * 1000;
      } else if (_tcscmp(arg, _T("--log")) == 0) {
        options->logLevel = _tstoi(value);
      } else if (_tcscmp(arg, _T("--split")) == 0) {
        options->splitPiece = _tcstoul(value, nullptr, 10);
        if (options->splitPiece == 0) {
          _ftprintf(stderr, _T("--split needs a number of bytes from 1\n\n%s"), USAGE);
          return false;
        }
      } else if (_tcscmp(arg, _T("--seed")) == 0) {
        options->seed = static_cast<unsigned int>(_tcstoul(value, nullptr, 10));
        options->isSeedGiven = true;
      } else {
        _ftprintf(stderr, _T("Unknown option %s\n\n%s"), arg, USAGE);
        return false;
      }
    } else if (options->input.isEmpty()) {
      options->input = arg;
    } else {
      _ftprintf(stderr, _T("Too many arguments\n\n%s"), USAGE);
      return false;
    }
  }

  if (options->isTcp && options->splitPiece != 0) {
    _ftprintf(stderr, _T("--tcp and --split cannot be combined\n\n%s"), USAGE);
    return false;
  }
  if (options->isSeedGiven && options->splitPiece == 0) {
    _ftprintf(stderr, _T("--seed needs --split\n\n%s"), USAGE);
    return false;
  }
  if (options->input.isEmpty() == options->checkCase.isEmpty()) {
    _ftprintf(stderr, _T("Give either INPUT or --check CASE\n\n%s"), USAGE);
    return false;
  }
  return true;
}

//
// Read a whole file, or stdin for "-".
//
// Post-conditions: returns true and the contents in data, or prints an
// error and returns false.
//
static bool readFile(const StringStorage &path, std::vector<char> *data)
{
  FILE *file = nullptr;
  bool isStdin = path.isEqualTo(_T("-"));
  if (isStdin) {
    file = stdin;
    _setmode(_fileno(stdin), _O_BINARY);
  } else if (_tfopen_s(&file, path.getString(), _T("rb")) != 0) {
    _ftprintf(stderr, _T("Cannot open %s\n"), path.getString());
    return false;
  }
  ScopeExit closeFile([&] {
    if (!isStdin) {
      fclose(file);
    }
  });

  data->clear();
  char buffer[65536];
  size_t count;
  while ((count = fread(buffer, 1, sizeof(buffer), file)) > 0) {
    data->insert(data->end(), buffer, buffer + count);
  }
  bool ok = ferror(file) == 0;
  if (!ok) {
    _ftprintf(stderr, _T("Cannot read %s\n"), path.getString());
  }
  return ok;
}

//
// Write a whole file, or stdout for "-".
//
// Post-conditions: returns true, or prints an error and returns false.
//
static bool writeFile(const StringStorage &path, const char *data, size_t size)
{
  FILE *file = nullptr;
  bool isStdout = path.isEqualTo(_T("-"));
  if (isStdout) {
    file = stdout;
    _setmode(_fileno(stdout), _O_BINARY);
  } else if (_tfopen_s(&file, path.getString(), _T("wb")) != 0) {
    _ftprintf(stderr, _T("Cannot create %s\n"), path.getString());
    return false;
  }
  bool ok = fwrite(data, 1, size, file) == size;
  ok = (isStdout ? fflush(file) : fclose(file)) == 0 && ok;
  if (!ok) {
    _ftprintf(stderr, _T("Cannot write %s\n"), path.getString());
  }
  return ok;
}

static bool writeFile(const StringStorage &path, const std::vector<char> &data)
{
  return writeFile(path, data.data(), data.size());
}

//
// Compare client bytes. Post-conditions: returns true if equal; otherwise
// prints the first difference and returns false.
//
static bool compareBytes(const TCHAR *what, const std::vector<char> &expected,
                         const std::vector<char> &actual)
{
  size_t common = std::min(expected.size(), actual.size());
  size_t i = 0;
  while (i < common && expected[i] == actual[i]) {
    i++;
  }
  if (i == common && expected.size() == actual.size()) {
    return true;
  }
  if (i < common) {
    _ftprintf(stderr, _T("%s: differs at byte %zu: expected 0x%02x, got 0x%02x ")
                      _T("(%zu bytes expected, %zu got)\n"),
              what, i, static_cast<UINT8>(expected[i]), static_cast<UINT8>(actual[i]),
              expected.size(), actual.size());
  } else {
    _ftprintf(stderr, _T("%s: %zu bytes expected, %zu got; the first %zu agree\n"),
              what, expected.size(), actual.size(), common);
  }
  return false;
}

static std::vector<std::string> splitLines(const std::vector<char> &text)
{
  std::vector<std::string> lines;
  std::string line;
  for (char c : text) {
    if (c == '\n') {
      lines.push_back(line);
      line.clear();
    } else {
      line += c;
    }
  }
  if (!line.empty()) {
    lines.push_back(line);
  }
  return lines;
}

//
// A line of the event log for printing, with control characters (such as
// a stray CR) shown as \xHH.
//
static std::string visible(const std::string &line)
{
  std::string shown;
  for (char c : line) {
    if (static_cast<UINT8>(c) < 0x20 || c == 0x7f) {
      char hex[8];
      sprintf_s(hex, sizeof(hex), "\\x%02x", static_cast<UINT8>(c));
      shown += hex;
    } else {
      shown += c;
    }
  }
  return shown;
}

//
// Compare event logs. Post-conditions: returns true if equal; otherwise
// prints the first differing line and returns false.
//
static bool compareEvents(const std::vector<char> &expected,
                          const std::vector<char> &actual)
{
  if (expected == actual) {
    return true;
  }
  std::vector<std::string> expectedLines = splitLines(expected);
  std::vector<std::string> actualLines = splitLines(actual);
  size_t i = 0;
  while (i < expectedLines.size() && i < actualLines.size() &&
         expectedLines[i] == actualLines[i]) {
    i++;
  }
  fprintf(stderr, "events: differ at line %zu\n  expected: %s\n  got:      %s\n",
          i + 1,
          i < expectedLines.size() ? visible(expectedLines[i]).c_str() : "(end)",
          i < actualLines.size() ? visible(actualLines[i]).c_str() : "(end)");
  return false;
}

//
// Split a PPM written by frameBufferToPpm() or rfbgen.py into its header
// and pixel data. Post-conditions: returns false if it is not one.
//
static bool parsePpm(const std::vector<char> &ppm, int *width, int *height,
                     size_t *dataOffset)
{
  std::string text(ppm.begin(), ppm.begin() + std::min<size_t>(ppm.size(), 64));
  int consumed = 0;
  if (sscanf_s(text.c_str(), "P6\n%d %d\n255\n%n", width, height, &consumed) != 2 ||
      consumed == 0) {
    return false;
  }
  *dataOffset = static_cast<size_t>(consumed);
  return ppm.size() == *dataOffset + static_cast<size_t>(*width) * *height * 3;
}

//
// Compare frame buffers. Post-conditions: returns true if equal;
// otherwise prints the size difference or the first differing pixel and
// returns false.
//
static bool comparePpm(const std::vector<char> &expected,
                       const std::vector<char> &actual)
{
  if (expected == actual) {
    return true;
  }
  int ew = 0, eh = 0, aw = 0, ah = 0;
  size_t eo = 0, ao = 0;
  if (!parsePpm(expected, &ew, &eh, &eo)) {
    _ftprintf(stderr, _T("ppm: the expected file is not a PPM from rfbgen.py\n"));
    return false;
  }
  parsePpm(actual, &aw, &ah, &ao);
  if (ew != aw || eh != ah) {
    _ftprintf(stderr, _T("ppm: expected %dx%d, got %dx%d\n"), ew, eh, aw, ah);
    return false;
  }
  for (size_t i = 0; i < static_cast<size_t>(ew) * eh; i++) {
    const UINT8 *e = reinterpret_cast<const UINT8 *>(&expected[eo + i * 3]);
    const UINT8 *a = reinterpret_cast<const UINT8 *>(&actual[ao + i * 3]);
    if (e[0] != a[0] || e[1] != a[1] || e[2] != a[2]) {
      size_t count = 0;
      for (size_t j = i; j < static_cast<size_t>(ew) * eh; j++) {
        if (memcmp(&expected[eo + j * 3], &actual[ao + j * 3], 3) != 0) {
          count++;
        }
      }
      _ftprintf(stderr, _T("ppm: %zu pixels differ; the first at (%zu, %zu): ")
                        _T("expected #%02x%02x%02x, got #%02x%02x%02x\n"),
                count, i % ew, i / ew, e[0], e[1], e[2], a[0], a[1], a[2]);
      return false;
    }
  }
  return false;
}

int _tmain(int argc, TCHAR *argv[])
{
  Options options;
  if (!parseOptions(argc, argv, &options)) {
    return EXIT_ERROR;
  }

  bool isCheck = !options.checkCase.isEmpty();
  StringStorage inputPath = options.input;
  if (isCheck) {
    inputPath = options.checkCase;
    inputPath.appendString(_T(".rfb"));
  }

  std::vector<char> input;
  if (!readFile(inputPath, &input)) {
    return EXIT_ERROR;
  }

  // A case with a password other than the default keeps it in
  // CASE.password, in UTF-8; --password still takes precedence.
  if (isCheck && !options.isPasswordGiven) {
    StringStorage passwordPath = options.checkCase;
    passwordPath.appendString(_T(".password"));
    if (_taccess_s(passwordPath.getString(), 0) == 0) {
      std::vector<char> password;
      if (!readFile(passwordPath, &password)) {
        return EXIT_ERROR;
      }
      options.password = TextEncoding::fromUtf8(std::as_bytes(std::span(password)));
    }
  }

  StderrLogger logger(options.logLevel);
  ReplayEventsAdapter adapter;
  ReplayAuthHandler authHandler(options.password.getString());
  std::unique_ptr<InputStream> inputStream;
  if (options.splitPiece == 0) {
    inputStream = std::make_unique<ByteArrayInputStream>(input.data(), input.size());
  } else if (options.isSeedGiven) {
    inputStream = std::make_unique<SplitInputStream>(input.data(), input.size(),
                                                     options.splitPiece, options.seed);
  } else {
    inputStream = std::make_unique<SplitInputStream>(input.data(), input.size(),
                                                     options.splitPiece);
  }
  MemoryOutputStream outputStream;
  RfbInputGate inputGate(inputStream.get());
  RfbOutputGate outputGate(&outputStream);

  // The core's connection runs on the network thread, as in the viewer.
  // Declared before the core, so that it is joined after the core is gone.
  NetworkThread network;

  // With --tcp, the input is served over a socket instead of the gates.
  std::unique_ptr<LoopbackServer> server;
  if (options.isTcp) {
    try {
      server = std::make_unique<LoopbackServer>(input);
    } catch (const Exception &e) {
      _ftprintf(stderr, _T("Cannot listen: %s\n"), e.getMessage());
      return EXIT_ERROR;
    }
  }

  // The core is never destroyed if the session times out (see below), or
  // fails, so it lives on the heap.
  std::unique_ptr<RemoteViewerCore> core = std::make_unique<RemoteViewerCore>(&logger);
  // What the core's connection holds while it runs (RemoteViewerCore::
  // start()): a reference that owns nothing, and tells when the last copy
  // has been released, after which the core may be destroyed. The promise
  // is shared, as a connection left running on failure may release it
  // after main() has returned.
  auto released = std::make_shared<std::promise<void>>();
  std::future<void> isReleased = released->get_future();
  std::shared_ptr<CoreEventsAdapter> adapterRef(&adapter, [released](CoreEventsAdapter *) {
    released->set_value();
  });
  std::vector<char> ppm;
  try {
    core->setMaxFrameBufferPixels(options.maxPixels);
    authHandler.addAuthCapability(*core);
    if (server) {
      core->start(_T("127.0.0.1"), server->getPort(), adapterRef, true);
    } else {
      core->start(&inputGate, &outputGate, adapterRef, true);
    }
    adapterRef.reset();

    if (!adapter.waitForEnd(options.timeoutMs)) {
      // A decoder that loops cannot be stopped from here, and the core
      // cannot be destroyed while its connection runs, so leave without
      // cleaning up.
      _ftprintf(stderr, _T("The session did not end within the timeout\n"));
      fflush(stderr);
      _exit(EXIT_TIMEOUT);
    }
    core->stop();
    isReleased.wait();

    FrameBuffer frameBuffer;
    core->copyFrameBuffer(frameBuffer);
    ppm = frameBufferToPpm(frameBuffer);
  } catch (const Exception &e) {
    _ftprintf(stderr, _T("Error: %s\n"), e.getMessage());
    // Its connection may still hold references to the adapter, so it must
    // not be destroyed.
    (void)core.release();
    return EXIT_ERROR;
  }
  // Before the output is read: the core writes it on the network thread.
  core.reset();

  std::vector<char> out = server ? server->finish() : outputStream.getData();
  std::string eventLog = adapter.getEventLog();
  if (server) {
    // The same end of the input, as a socket reports it.
    const std::string closed = "disconnect \"Connection has been gracefully closed\"";
    size_t at = eventLog.find(closed);
    if (at != std::string::npos) {
      eventLog.replace(at, closed.size(), "disconnect \"End of stream reached\"");
    }
  }
  std::vector<char> events(eventLog.begin(), eventLog.end());

  StringStorage outFile = options.outFile;
  if (outFile.isEmpty() && !isCheck) {
    outFile = _T("-");
  }
  if ((!outFile.isEmpty() && !writeFile(outFile, out)) ||
      (!options.eventsFile.isEmpty() && !writeFile(options.eventsFile, events)) ||
      (!options.ppmFile.isEmpty() && !writeFile(options.ppmFile, ppm))) {
    return EXIT_ERROR;
  }

  if (!isCheck) {
    return EXIT_MATCH;
  }

  std::vector<char> expectedOut, expectedEvents, expectedPpm;
  StringStorage path;
  path.format(_T("%s.out"), options.checkCase.getString());
  if (!readFile(path, &expectedOut)) {
    return EXIT_ERROR;
  }
  path.format(_T("%s.events"), options.checkCase.getString());
  if (!readFile(path, &expectedEvents)) {
    return EXIT_ERROR;
  }
  path.format(_T("%s.ppm"), options.checkCase.getString());
  if (!readFile(path, &expectedPpm)) {
    return EXIT_ERROR;
  }

  // Compare all three, so that one run reports every difference.
  bool outMatches = compareBytes(_T("out"), expectedOut, out);
  bool eventsMatch = compareEvents(expectedEvents, events);
  bool ppmMatches = comparePpm(expectedPpm, ppm);
  return outMatches && eventsMatch && ppmMatches ? EXIT_MATCH : EXIT_MISMATCH;
}
