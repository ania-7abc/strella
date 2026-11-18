#include "strella/preimage.hpp"

#include <csetjmp>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "strella/ast.hpp"
#include "strella/diagnostics.hpp"
#include "strella/location.hpp"

#ifdef STRELLA_HAVE_LIBPNG
#include <png.h>
#include <pngconf.h>
#endif

namespace strella
{
namespace
{

auto loadRawImage(std::string const& path, std::string const& file,
                  SourceLocation const& location) -> Preimage
{
  std::ifstream stream {path, std::ios::binary};
  if (!stream)
  {
    fail(file, location, "cannot open preimage '" + path + "'");
  }

  std::vector<std::uint8_t> bytes {std::istreambuf_iterator<char> {stream},
                                   std::istreambuf_iterator<char> {}};

  if (bytes.size() != 32 && bytes.size() != 64)
  {
    fail(file, location,
         "preimage '" + path + "' must contain exactly 32 or 64 bytes");
  }

  Preimage image;
  image.size = static_cast<int>(bytes.size());
  image.bytes = std::move(bytes);
  image.sourcePath = path;
  return image;
}

#ifdef STRELLA_HAVE_LIBPNG
// libpng is a C library: its resources are owned manually and errors are
// reported through setjmp/longjmp, so C++ ownership and exception guidelines
// do not apply here.
// NOLINTBEGIN
auto loadPngImage(std::string const& path, std::string const& file,
                  SourceLocation const& location) -> Preimage
{
  std::FILE* handle = std::fopen(path.c_str(), "rb");
  if (handle == nullptr)
  {
    fail(file, location, "cannot open preimage '" + path + "'");
  }

  png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr,
                                           nullptr, nullptr);
  if (png == nullptr)
  {
    std::fclose(handle);
    fail(file, location, "cannot initialize the PNG decoder");
  }

  png_infop info = png_create_info_struct(png);
  if (info == nullptr)
  {
    png_destroy_read_struct(&png, nullptr, nullptr);
    // cppcheck-suppress doubleFree -- fail() is [[noreturn]].
    std::fclose(handle);
    fail(file, location, "cannot initialize the PNG decoder");
  }

  if (setjmp(png_jmpbuf(png)))
  {
    png_destroy_read_struct(&png, &info, nullptr);
    // cppcheck-suppress doubleFree -- fail() is [[noreturn]].
    std::fclose(handle);
    fail(file, location, "cannot decode PNG image '" + path + "'");
  }

  png_init_io(png, handle);
  png_read_info(png, info);

  auto const width = png_get_image_width(png, info);
  auto const height = png_get_image_height(png, info);
  auto const colorType = png_get_color_type(png, info);
  auto const bitDepth = png_get_bit_depth(png, info);

  if (width != 16 || height != 16)
  {
    png_destroy_read_struct(&png, &info, nullptr);
    // cppcheck-suppress doubleFree -- fail() is [[noreturn]].
    std::fclose(handle);
    fail(file, location, "preimage PNG '" + path + "' must be 16x16 pixels");
  }

  if (bitDepth == 16)
  {
    png_set_strip_16(png);
  }
  if (colorType == PNG_COLOR_TYPE_PALETTE)
  {
    png_set_palette_to_rgb(png);
  }
  if (colorType == PNG_COLOR_TYPE_GRAY && bitDepth < 8)
  {
    png_set_expand_gray_1_2_4_to_8(png);
  }
  if (png_get_valid(png, info, PNG_INFO_tRNS) != 0U)
  {
    png_set_tRNS_to_alpha(png);
  }
  if (colorType == PNG_COLOR_TYPE_GRAY
      || colorType == PNG_COLOR_TYPE_GRAY_ALPHA)
  {
    png_set_gray_to_rgb(png);
  }
  png_set_add_alpha(png, 0xFF, PNG_FILLER_AFTER);
  png_read_update_info(png, info);

  std::vector<std::uint8_t> pixels(std::size_t {16} * 16U * 4U);
  std::vector<png_bytep> rows(16);
  for (int row = 0; row < 16; ++row)
  {
    rows[static_cast<std::size_t>(row)]
        = pixels.data() + (static_cast<std::size_t>(row) * 16 * 4);
  }

  png_read_image(png, rows.data());
  png_read_end(png, nullptr);
  png_destroy_read_struct(&png, &info, nullptr);
  std::fclose(handle);

  // Pack the 16x16 image into 64 bytes of colour display memory.  Every byte
  // contains four pixels and every pixel uses two bits: bit 0 is red, bit 1
  // is blue.
  Preimage image;
  image.size = 64;
  image.bytes.assign(64, 0);
  image.sourcePath = path;

  for (int y = 0; y < 16; ++y)
  {
    for (int x = 0; x < 16; ++x)
    {
      auto const pixelIndex
          = (static_cast<std::size_t>(y) * 16U) + static_cast<std::size_t>(x);
      auto const* rgba = pixels.data() + (pixelIndex * 4);
      std::uint8_t bits = 0;
      if (rgba[0] >= 0x80)
      {
        bits |= 1U;
      }
      if (rgba[2] >= 0x80)
      {
        bits |= 2U;
      }

      auto const cell = static_cast<std::size_t>(pixelIndex / 4);
      auto const shift = static_cast<unsigned>((pixelIndex % 4) * 2);
      image.bytes[cell] |= static_cast<std::uint8_t>(bits << shift);
    }
  }

  return image;
}
// NOLINTEND
#endif

} // namespace

auto loadPreimage(std::string const& path, std::string const& file,
                  SourceLocation const& location) -> Preimage
{
  if (path.empty())
  {
    fail(file, location, "preimage path is empty");
  }

  auto const dot = path.find_last_of('.');
  auto extension
      = dot == std::string::npos ? std::string {} : path.substr(dot + 1);
  for (auto& character : extension)
  {
    if (character >= 'A' && character <= 'Z')
    {
      character = static_cast<char>(character - 'A' + 'a');
    }
  }

  if (extension == "png")
  {
#ifdef STRELLA_HAVE_LIBPNG
    return loadPngImage(path, file, location);
#else
    fail(file, location,
         "PNG preimages require libpng; rebuild with libpng available");
#endif
  }

  return loadRawImage(path, file, location);
}

} // namespace strella
