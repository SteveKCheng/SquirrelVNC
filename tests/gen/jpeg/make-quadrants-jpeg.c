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
// make-quadrants-jpeg: writes quadrants-16x16.jpg, the JPEG used by the
// Tight JPEG replay cases (tests/gen/cases.py, quadrants_jpeg()).
//
// The image is 16 x 16 pixels in four solid 8 x 8 quadrants, in reading
// order (200, 40, 40), (40, 160, 60), (50, 70, 190) and (230, 210, 60),
// compressed by libjpeg-turbo's TurboJPEG API at quality 95 without
// chroma subsampling (4:4:4). Solid blocks leave little room for
// decoders to differ. The checked-in file was made with libjpeg-turbo
// 3.2.0 from vcpkg; another version may write different bytes for the
// same image.
//
// It is not built with the solution, and needs nothing but TurboJPEG,
// e.g. from a Visual Studio x64 command prompt in this directory:
//
//   cl /MT make-quadrants-jpeg.c /I<vcpkg>\include <vcpkg>\lib\turbojpeg.lib
//
// where <vcpkg> is vcpkg_installed\x64-windows-static\x64-windows-static
// at the top of the source tree. It writes the file in the current
// directory. If the JPEG changes, the decoded image quadrants-16x16.ppm
// must be taken again from rfb-replay and reviewed (see quadrants_jpeg()).
//

#include <stdio.h>
#include <turbojpeg.h>

int main(void)
{
  static const unsigned char colours[4][3] = {
    {200, 40, 40}, {40, 160, 60}, {50, 70, 190}, {230, 210, 60}
  };
  unsigned char rgb[16 * 16 * 3];
  for (int y = 0; y < 16; y++) {
    for (int x = 0; x < 16; x++) {
      for (int c = 0; c < 3; c++) {
        rgb[(y * 16 + x) * 3 + c] = colours[(y / 8) * 2 + x / 8][c];
      }
    }
  }

  tjhandle handle = tj3Init(TJINIT_COMPRESS);
  if (handle == NULL) {
    fprintf(stderr, "tj3Init failed\n");
    return 1;
  }
  tj3Set(handle, TJPARAM_QUALITY, 95);
  tj3Set(handle, TJPARAM_SUBSAMP, TJSAMP_444);

  unsigned char *jpeg = NULL;
  size_t size = 0;
  if (tj3Compress8(handle, rgb, 16, 0, 16, TJPF_RGB, &jpeg, &size) != 0) {
    fprintf(stderr, "tj3Compress8 failed: %s\n", tj3GetErrorStr(handle));
    tj3Destroy(handle);
    return 1;
  }

  int status = 0;
  FILE *file = fopen("quadrants-16x16.jpg", "wb");
  if (file == NULL || fwrite(jpeg, 1, size, file) != size) {
    fprintf(stderr, "cannot write quadrants-16x16.jpg\n");
    status = 1;
  }
  if (file != NULL && fclose(file) != 0) {
    status = 1;
  }
  if (status == 0) {
    printf("wrote quadrants-16x16.jpg, %zu bytes\n", size);
  }
  tj3Free(jpeg);
  tj3Destroy(handle);
  return status;
}
