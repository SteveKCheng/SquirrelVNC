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
// svg2ico: build-time tool that renders an SVG file into square icons of
// several sizes with Direct2D, and writes them as a multi-size .ico file
// and/or as separate PNG files.
//
// Direct2D's SVG support (Windows 10 1703 and later) covers static
// SVG 1.1 shapes, paths, fills, strokes, gradients, clip paths, opacity
// and transforms. Text, filters, masks, patterns and markers are not
// supported and are skipped by Direct2D without an error; svg2ico warns
// about them.
//
// Rendering uses a WIC bitmap render target, which is always software
// rendered and needs neither a GPU nor an interactive session.
//

#include <windows.h>
#include <d2d1_3.h>
#include <wincodec.h>
#include <shlwapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

const wchar_t USAGE[] =
  L"Usage: svg2ico [options] input.svg\n"
  L"\n"
  L"Renders input.svg into square icons. The drawing (its viewBox, or\n"
  L"else its width and height) is scaled to fit inside the icon, keeping\n"
  L"its aspect ratio.\n"
  L"\n"
  L"Options:\n"
  L"  --ico FILE          Write all sizes into one .ico file.\n"
  L"  --png-dir DIR       Write each size to DIR\\<prefix><size>.png.\n"
  L"  --png-prefix TEXT   File name prefix for --png-dir (default: icon-).\n"
  L"  --sizes LIST        Comma-separated sizes in pixels\n"
  L"                      (default: 16,20,24,32,40,48,64,96,256).\n"
  L"  --margin PERCENT    Empty margin on each side, as a percentage of the\n"
  L"                      icon size (default: 0).\n"
  L"  --extent UNITS      Scale as if the drawing were at least UNITS user\n"
  L"                      units across, so that a smaller drawing is\n"
  L"                      surrounded by empty space. For icons drawn on\n"
  L"                      grids of different sizes (default: none).\n"
  L"  --align MODE        Where the drawing sits along its shorter axis:\n"
  L"                      center, start (top/left) or end (bottom/right)\n"
  L"                      (default: center).\n"
  L"  --no-snap           Do not round the drawing's offset to whole pixels.\n";

enum class Align { Center, Start, End };

struct Options
{
  std::wstring input;
  std::wstring icoFile;
  std::wstring pngDir;
  std::wstring pngPrefix = L"icon-";
  std::vector<UINT> sizes = { 16, 20, 24, 32, 40, 48, 64, 96, 256 };
  float margin = 0.0f;
  float extent = 0.0f;
  Align align = Align::Center;
  bool snap = true;
};

// Error with a message for the user. HRESULT failures carry the call
// that failed.
class Error : public std::runtime_error
{
public:
  explicit Error(const std::string &message) : std::runtime_error(message) {}
};

void check(HRESULT hr, const char *what)
{
  if (FAILED(hr)) {
    char buffer[64];
    sprintf_s(buffer, " failed (HRESULT 0x%08lX)", static_cast<unsigned long>(hr));
    throw Error(std::string(what) + buffer);
  }
}

std::string narrow(const std::wstring &s)
{
  int length = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
  std::string result(length > 0 ? length - 1 : 0, '\0');
  if (length > 1) {
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, &result[0], length, nullptr, nullptr);
  }
  return result;
}

std::vector<UINT> parseSizes(const std::wstring &list)
{
  std::vector<UINT> sizes;
  size_t pos = 0;
  while (pos <= list.size()) {
    size_t comma = list.find(L',', pos);
    if (comma == std::wstring::npos) {
      comma = list.size();
    }
    std::wstring item = list.substr(pos, comma - pos);
    wchar_t *end = nullptr;
    unsigned long size = wcstoul(item.c_str(), &end, 10);
    if (item.empty() || *end != L'\0' || size < 1 || size > 256) {
      throw Error("Invalid size \"" + narrow(item) + "\": sizes must be 1 to 256");
    }
    sizes.push_back(static_cast<UINT>(size));
    pos = comma + 1;
  }
  std::sort(sizes.begin(), sizes.end());
  sizes.erase(std::unique(sizes.begin(), sizes.end()), sizes.end());
  return sizes;
}

Options parseOptions(int argc, wchar_t *argv[])
{
  Options options;
  for (int i = 1; i < argc; i++) {
    std::wstring arg = argv[i];
    auto value = [&]() -> std::wstring {
      if (i + 1 >= argc) {
        throw Error("Option " + narrow(arg) + " needs a value");
      }
      return argv[++i];
    };
    if (arg == L"--ico") {
      options.icoFile = value();
    } else if (arg == L"--png-dir") {
      options.pngDir = value();
    } else if (arg == L"--png-prefix") {
      options.pngPrefix = value();
    } else if (arg == L"--sizes") {
      options.sizes = parseSizes(value());
    } else if (arg == L"--margin") {
      std::wstring text = value();
      wchar_t *end = nullptr;
      options.margin = wcstof(text.c_str(), &end) / 100.0f;
      if (text.empty() || *end != L'\0' || options.margin < 0.0f || options.margin >= 0.5f) {
        throw Error("Invalid margin \"" + narrow(text) + "\": must be 0 to less than 50");
      }
    } else if (arg == L"--extent") {
      std::wstring text = value();
      wchar_t *end = nullptr;
      options.extent = wcstof(text.c_str(), &end);
      if (text.empty() || *end != L'\0' || !(options.extent > 0.0f)) {
        throw Error("Invalid extent \"" + narrow(text) + "\": must be positive");
      }
    } else if (arg == L"--align") {
      std::wstring mode = value();
      if (mode == L"center") {
        options.align = Align::Center;
      } else if (mode == L"start") {
        options.align = Align::Start;
      } else if (mode == L"end") {
        options.align = Align::End;
      } else {
        throw Error("Invalid alignment \"" + narrow(mode) + "\"");
      }
    } else if (arg == L"--no-snap") {
      options.snap = false;
    } else if (arg == L"--help" || arg == L"-h" || arg == L"/?") {
      fputws(USAGE, stdout);
      exit(0);
    } else if (!arg.empty() && arg[0] == L'-') {
      throw Error("Unknown option " + narrow(arg));
    } else if (options.input.empty()) {
      options.input = arg;
    } else {
      throw Error("More than one input file given");
    }
  }
  if (options.input.empty()) {
    throw Error("No input file given");
  }
  if (options.icoFile.empty() && options.pngDir.empty()) {
    throw Error("Nothing to do: give --ico and/or --png-dir");
  }
  return options;
}

std::vector<BYTE> readFile(const std::wstring &path)
{
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw Error("Cannot open " + narrow(path));
  }
  return std::vector<BYTE>(std::istreambuf_iterator<char>(file),
                           std::istreambuf_iterator<char>());
}

void writeFile(const std::wstring &path, const std::vector<BYTE> &data)
{
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(reinterpret_cast<const char *>(data.data()), data.size());
  if (!file) {
    throw Error("Cannot write " + narrow(path));
  }
}

class Renderer
{
public:
  explicit Renderer(const std::vector<BYTE> &svg)
  : m_svg(svg)
  {
    check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                            m_d2dFactory.GetAddressOf()),
          "D2D1CreateFactory");
    check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                           IID_PPV_ARGS(m_wicFactory.GetAddressOf())),
          "Creating the WIC imaging factory");
  }

  // Renders the SVG into a size x size bitmap and returns it encoded as
  // PNG, with straight (not premultiplied) alpha.
  std::vector<BYTE> renderPng(UINT size, const Options &options)
  {
    ComPtr<IWICBitmap> bitmap;
    check(m_wicFactory->CreateBitmap(size, size, GUID_WICPixelFormat32bppPBGRA,
                                     WICBitmapCacheOnLoad, bitmap.GetAddressOf()),
          "Creating a WIC bitmap");

    D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
      D2D1_RENDER_TARGET_TYPE_SOFTWARE,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
      96.0f, 96.0f);
    ComPtr<ID2D1RenderTarget> target;
    check(m_d2dFactory->CreateWicBitmapRenderTarget(bitmap.Get(), properties,
                                                    target.GetAddressOf()),
          "Creating a Direct2D render target");
    ComPtr<ID2D1DeviceContext5> context;
    check(target.As(&context),
          "Getting ID2D1DeviceContext5 (SVG needs Windows 10 1703 or later)");

    // The document is a device resource, so it is created for each
    // render target. A viewport of 1x1 is replaced below.
    ComPtr<IStream> stream;
    stream.Attach(SHCreateMemStream(m_svg.data(), static_cast<UINT>(m_svg.size())));
    if (!stream) {
      throw Error("Cannot create a memory stream");
    }
    ComPtr<ID2D1SvgDocument> document;
    check(context->CreateSvgDocument(stream.Get(), D2D1::SizeF(1.0f, 1.0f),
                                     document.GetAddressOf()),
          "Parsing the SVG document");

    D2D1_SVG_VIEWBOX viewBox = getViewBox(document.Get());

    // Fit the drawing inside the icon less the margins. Its viewport gets
    // the drawing's own aspect ratio, so the default preserveAspectRatio
    // maps the viewBox onto it exactly, and the offset positions it.
    float inner = size * (1.0f - 2.0f * options.margin);
    float scale = inner / (std::max)({ viewBox.width, viewBox.height, options.extent });
    float width = viewBox.width * scale;
    float height = viewBox.height * scale;
    float x = offset(size, width, options);
    float y = offset(size, height, options);
    check(document->SetViewportSize(D2D1::SizeF(width, height)), "Setting the SVG viewport");

    context->BeginDraw();
    context->Clear(D2D1::ColorF(0, 0.0f));
    context->SetTransform(D2D1::Matrix3x2F::Translation(x, y));
    context->DrawSvgDocument(document.Get());
    check(context->EndDraw(), "Drawing the SVG document");

    return encodePng(bitmap.Get());
  }

private:
  // Returns the root element's viewBox. Without one, the drawing has no
  // scaling, so a viewBox from (0, 0) to its width and height is set.
  static D2D1_SVG_VIEWBOX getViewBox(ID2D1SvgDocument *document)
  {
    ComPtr<ID2D1SvgElement> root;
    document->GetRoot(root.GetAddressOf());
    if (!root) {
      throw Error("The SVG document has no root element");
    }
    D2D1_SVG_VIEWBOX viewBox = {};
    if (root->IsAttributeSpecified(L"viewBox")) {
      check(root->GetAttributeValue(L"viewBox", D2D1_SVG_ATTRIBUTE_POD_TYPE_VIEWBOX,
                                    &viewBox, sizeof(viewBox)),
            "Reading the viewBox");
    } else {
      viewBox.width = getLength(root.Get(), L"width");
      viewBox.height = getLength(root.Get(), L"height");
      check(root->SetAttributeValue(L"viewBox", D2D1_SVG_ATTRIBUTE_POD_TYPE_VIEWBOX,
                                    &viewBox, sizeof(viewBox)),
            "Setting the viewBox");
    }
    if (viewBox.width <= 0.0f || viewBox.height <= 0.0f) {
      throw Error("The SVG root element needs a viewBox, or a width and height,"
                  " that are positive");
    }
    return viewBox;
  }

  // Width or height of the root element, which must be a plain number
  // (user units or pixels) when there is no viewBox.
  static float getLength(ID2D1SvgElement *root, const wchar_t *name)
  {
    D2D1_SVG_LENGTH length = {};
    if (!root->IsAttributeSpecified(name)) {
      return 0.0f;
    }
    check(root->GetAttributeValue(name, D2D1_SVG_ATTRIBUTE_POD_TYPE_LENGTH,
                                  &length, sizeof(length)),
          "Reading the width or height");
    if (length.units != D2D1_SVG_LENGTH_UNITS_NUMBER) {
      throw Error("Without a viewBox, the SVG root element's width and height"
                  " must not be percentages");
    }
    return length.value;
  }

  // Offset of the drawing along one axis: the free space is split
  // according to the alignment, and the margin applies at both ends.
  static float offset(UINT size, float extent, const Options &options)
  {
    float marginPixels = size * options.margin;
    float freeSpace = size - 2.0f * marginPixels - extent;
    float position;
    switch (options.align) {
    case Align::Start:
      position = marginPixels;
      break;
    case Align::End:
      position = marginPixels + freeSpace;
      break;
    default:
      position = marginPixels + freeSpace / 2.0f;
      break;
    }
    // A whole-pixel offset keeps edges that are aligned in the drawing
    // on pixel boundaries at every size.
    return options.snap ? std::floor(position + 0.5f) : position;
  }

  std::vector<BYTE> encodePng(IWICBitmap *bitmap)
  {
    // PNG stores straight alpha, so convert from premultiplied first.
    ComPtr<IWICFormatConverter> converter;
    check(m_wicFactory->CreateFormatConverter(converter.GetAddressOf()),
          "Creating a WIC format converter");
    check(converter->Initialize(bitmap, GUID_WICPixelFormat32bppBGRA,
                                WICBitmapDitherTypeNone, nullptr, 0.0,
                                WICBitmapPaletteTypeCustom),
          "Converting to straight alpha");

    ComPtr<IStream> stream;
    check(CreateStreamOnHGlobal(nullptr, TRUE, stream.GetAddressOf()), "Creating a stream");
    ComPtr<IWICBitmapEncoder> encoder;
    check(m_wicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr,
                                      encoder.GetAddressOf()),
          "Creating the PNG encoder");
    check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), "Initializing the PNG encoder");
    ComPtr<IWICBitmapFrameEncode> frame;
    check(encoder->CreateNewFrame(frame.GetAddressOf(), nullptr), "Creating a PNG frame");
    check(frame->Initialize(nullptr), "Initializing the PNG frame");
    check(frame->WriteSource(converter.Get(), nullptr), "Writing the PNG frame");
    check(frame->Commit(), "Committing the PNG frame");
    check(encoder->Commit(), "Committing the PNG file");

    STATSTG stat = {};
    check(stream->Stat(&stat, STATFLAG_NONAME), "Reading the PNG size");
    std::vector<BYTE> png(static_cast<size_t>(stat.cbSize.QuadPart));
    LARGE_INTEGER start = {};
    check(stream->Seek(start, STREAM_SEEK_SET, nullptr), "Rewinding the PNG stream");
    ULONG read = 0;
    check(stream->Read(png.data(), static_cast<ULONG>(png.size()), &read), "Reading the PNG data");
    return png;
  }

  const std::vector<BYTE> &m_svg;
  ComPtr<ID2D1Factory> m_d2dFactory;
  ComPtr<IWICImagingFactory> m_wicFactory;
};

// Direct2D drops the elements it does not support while parsing, without
// an error, so they are looked for in the source. Element names are taken
// from the start tags; elements in other namespaces, such as Inkscape's,
// are not drawn by any renderer and are ignored. The warnings are in the
// format that MSBuild shows as warnings.
void warnUnsupported(const std::vector<BYTE> &svg, const std::wstring &path)
{
  static const char *const SUPPORTED[] = {
    "svg", "g", "defs", "use", "path", "rect", "circle", "ellipse",
    "line", "polyline", "polygon", "linearGradient", "radialGradient",
    "stop", "clipPath", "image", "title", "desc", "metadata",
  };
  std::vector<std::string> reported;
  std::string text(svg.begin(), svg.end());
  for (size_t pos = text.find('<'); pos != std::string::npos; pos = text.find('<', pos + 1)) {
    size_t end = text.find_first_of(" \t\r\n/>", pos + 1);
    std::string name = text.substr(pos + 1, end == std::string::npos ? std::string::npos
                                                                     : end - pos - 1);
    // Skip end tags, comments, CDATA, the XML declaration and namespaced
    // elements.
    if (name.empty() || name.find_first_of("/!?:") != std::string::npos) {
      continue;
    }
    if (std::find(std::begin(SUPPORTED), std::end(SUPPORTED), name) == std::end(SUPPORTED) &&
        std::find(reported.begin(), reported.end(), name) == reported.end()) {
      fprintf(stderr, "%s : warning : <%s> is not supported by Direct2D and is not drawn\n",
              narrow(path).c_str(), name.c_str());
      reported.push_back(name);
    }
  }
}

void appendLE(std::vector<BYTE> &out, UINT32 value, int bytes)
{
  for (int i = 0; i < bytes; i++) {
    out.push_back(static_cast<BYTE>(value >> (8 * i)));
  }
}

// Builds an .ico file whose images are all stored as 32-bit PNG, which
// Windows Vista and later accept at every size.
std::vector<BYTE> buildIco(const std::vector<UINT> &sizes,
                           const std::vector<std::vector<BYTE>> &images)
{
  const UINT32 headerSize = 6;
  const UINT32 entrySize = 16;
  std::vector<BYTE> ico;
  appendLE(ico, 0, 2);                                  // Reserved
  appendLE(ico, 1, 2);                                  // Type: icon
  appendLE(ico, static_cast<UINT32>(sizes.size()), 2);  // Image count

  UINT32 offset = headerSize + entrySize * static_cast<UINT32>(sizes.size());
  for (size_t i = 0; i < sizes.size(); i++) {
    BYTE dimension = static_cast<BYTE>(sizes[i] == 256 ? 0 : sizes[i]);
    UINT32 length = static_cast<UINT32>(images[i].size());
    ico.push_back(dimension);   // Width
    ico.push_back(dimension);   // Height
    ico.push_back(0);           // Palette colours
    ico.push_back(0);           // Reserved
    appendLE(ico, 1, 2);        // Colour planes
    appendLE(ico, 32, 2);       // Bits per pixel
    appendLE(ico, length, 4);
    appendLE(ico, offset, 4);
    offset += length;
  }
  for (const std::vector<BYTE> &image : images) {
    ico.insert(ico.end(), image.begin(), image.end());
  }
  return ico;
}

void run(const Options &options)
{
  std::vector<BYTE> svg = readFile(options.input);
  warnUnsupported(svg, options.input);
  Renderer renderer(svg);

  std::vector<std::vector<BYTE>> images;
  for (UINT size : options.sizes) {
    images.push_back(renderer.renderPng(size, options));
    if (!options.pngDir.empty()) {
      writeFile(options.pngDir + L"\\" + options.pngPrefix + std::to_wstring(size) + L".png",
                images.back());
    }
  }
  if (!options.icoFile.empty()) {
    writeFile(options.icoFile, buildIco(options.sizes, images));
  }
}

} // namespace

int wmain(int argc, wchar_t *argv[])
{
  try {
    Options options = parseOptions(argc, argv);
    check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "CoInitializeEx");
    run(options);
    CoUninitialize();
    return 0;
  } catch (const std::exception &e) {
    fprintf(stderr, "svg2ico: %s\n", e.what());
    return 1;
  }
}
