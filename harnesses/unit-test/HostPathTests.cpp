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
// Unit tests of HostPath, which parses the host the user gives
// ("host", "host:display", "host::port", with an optional SSH part).
//

#include "UnitTest.h"

#include "rfb/HostPath.h"

#include <stdio.h>

#include <string>

namespace
{
  struct Case
  {
    const wchar_t *path;
    bool valid;
    const wchar_t *vncHost;
    int vncPort;
    const wchar_t *sshHost;
    int sshPort;
  };

  void checkCase(const Case &c)
  {
    HostPath hostPath;
    bool valid = hostPath.set(c.path);
    bool ok = valid == c.valid && hostPath.isValid() == c.valid;
    if (ok && c.valid) {
      ok = hostPath.get().str() == c.path &&
           hostPath.getVncHost().str() == c.vncHost &&
           hostPath.getVncPort() == c.vncPort &&
           hostPath.getSshHost().str() == c.sshHost &&
           hostPath.isSshHostSpecified() == (c.sshHost[0] != L'\0') &&
           hostPath.getSshPort() == c.sshPort;
    }
    if (ok && !c.valid) {
      // An invalid path leaves the object empty.
      ok = hostPath.get().isEmpty() && hostPath.getVncHost().isEmpty() &&
           hostPath.getSshHost().isEmpty() && hostPath.getVncPort() == 0;
    }
    char what[600];
    sprintf_s(what, "HostPath \"%.500ls\" is %s", c.path, c.valid ? "valid" : "invalid");
    check(ok, what);
  }
}

void runHostPathTests()
{
  static const Case CASES[] = {
    { L"host", true, L"host", 5900, L"", 0 },
    { L"host:1", true, L"host", 5901, L"", 0 },
    { L"host:99", true, L"host", 5999, L"", 0 },
    { L"host:100", true, L"host", 100, L"", 0 },
    { L"host::5", true, L"host", 5, L"", 0 },
    { L"host::65535", true, L"host", 65535, L"", 0 },
    { L"host::65536", false },
    { L"host:", false },
    { L"host::", false },
    { L"host:1x", false },
    { L"host:123456", false },
    { L":1", true, L"localhost", 5901, L"", 0 },
    { L"192.168.0.1:2", true, L"192.168.0.1", 5902, L"", 0 },
    { L"h_st.example-1", true, L"h_st.example-1", 5900, L"", 0 },
    { L"ssh/vnc", true, L"vnc", 5900, L"ssh", 22 },
    { L"user@ssh:2222/vnc::7", true, L"vnc", 7, L"user@ssh", 2222 },
    { L"ssh:/vnc", false },
    { L"/vnc", false },
    { L"@ssh/vnc", false },
    { L"ssh/", true, L"localhost", 5900, L"ssh", 22 },
    { L"", false },
    { L"bad host", false },
    { L"[::1]", false },
    { L"host\\x", false },
    // Internationalised names, which GetAddrInfoW() converts to Punycode.
    { L"b\u00fccher.example", true, L"b\u00fccher.example", 5900, L"", 0 },
    { L"\u4f8b\u3048.\u30c6\u30b9\u30c8::5901", true,
      L"\u4f8b\u3048.\u30c6\u30b9\u30c8", 5901, L"", 0 },
    { L"\U0001F43F.example", true, L"\U0001F43F.example", 5900, L"", 0 },
    { L"ssh\u00fc/vnc", true, L"vnc", 5900, L"ssh\u00fc", 22 },
    { L"\u00fcser@ssh/vnc", false },          // a user name is ASCII only
    { L"a\xD800" L"b", false },                // unpaired surrogates
    { L"a\xDC00" L"b", false },
    { L"a\xD800", false },
    { L"a\u0085b", false },                  // C1 control
    { L"a\u00a0b", false },                  // no-break space
    { L"a\u00adb", false },                  // soft hyphen
    { L"a\u200bb", false },                  // zero-width space
    { L"a\u202eb", false },                  // right-to-left override
    { L"a\u3000b", false },                  // ideographic space
    { L"a\ufeffb", false },                  // zero-width no-break space
    { L"a\ufffdb", false },                  // replacement character
  };
  for (const Case &c : CASES) {
    checkCase(c);
  }

  HostPath hostPath;
  check(!hostPath.set(nullptr) && !hostPath.isValid(), "HostPath of null is invalid");

  std::wstring longest(255, L'a');
  Case longestCase = { longest.c_str(), true, longest.c_str(), 5900, L"", 0 };
  checkCase(longestCase);
  std::wstring tooLong(256, L'a');
  Case tooLongCase = { tooLong.c_str(), false };
  checkCase(tooLongCase);

  // A path over the length limit is rejected, not cut short to a valid one.
  std::wstring overLimit = L"host::5900" + std::wstring(600, L'0');
  check(!hostPath.set(overLimit.c_str()), "HostPath over the length limit is invalid");

  check(hostPath.set(L"first::1") && !hostPath.set(L"bad host") && !hostPath.isValid(),
        "HostPath forgets a valid path when set to an invalid one");
}
