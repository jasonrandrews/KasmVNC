/* Copyright (C) 2000-2003 Constantin Kaplinsky.  All Rights Reserved.
 * Copyright (C) 2011 D. R. Commander.  All Rights Reserved.
 * Copyright 2014 Pierre Ossman for Cendio AB
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 * USA.
 */
#include <rdr/OutStream.h>
#include <rfb/encodings.h>
#include <rfb/SConnection.h>
#include <rfb/ServerCore.h>
#include <rfb/Palette.h>
#include <rfb/PixelBuffer.h>
#include <rfb/TightIndexedPNGEncoder.h>
#include <rfb/TightConstants.h>

#include <png.h>

using namespace rfb;

static const PixelFormat pfRGBX(32, 24, false, true, 255, 255, 255, 0, 8, 16);

TightIndexedPNGEncoder::TightIndexedPNGEncoder(SConnection* conn) :
  Encoder(conn, encodingTight, (EncoderFlags)(EncoderUseNativePF), 256),
  qualityLevel(-1)
{
}

TightIndexedPNGEncoder::~TightIndexedPNGEncoder()
{
}

bool TightIndexedPNGEncoder::isSupported()
{
  if (!conn->cp.supportsEncoding(encodingTight))
    return false;

  if (conn->cp.supportsIndexedPNG)
    return true;

  // Tight support, but not IndexedPNG
  return false;
}

void TightIndexedPNGEncoder::setQualityLevel(int level)
{
  qualityLevel = level;
}

bool TightIndexedPNGEncoder::treatLossless()
{
  return true;
}

static unsigned encode(rdr::U8 **tmpbuf, const rdr::U8 *data, const Palette& inpal,
			const bool rgb, const unsigned stride,
			const unsigned w, const unsigned h)
{
  unsigned i;
  size_t len = 0;
  rdr::U8 rowbuf[4096];

  png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png_ptr) abort();
  png_infop info = png_create_info_struct(png_ptr);
  if (!info) abort();
  if (setjmp(png_jmpbuf(png_ptr))) abort();

  FILE *f = open_memstream((char **) tmpbuf, &len);
  if (!f) abort();

  png_init_io(png_ptr, f);
  png_set_IHDR(png_ptr, info, w, h, 8, PNG_COLOR_TYPE_PALETTE, PNG_INTERLACE_NONE,
  		PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

  png_color pal[256];
  for (i = 0; i < (unsigned) inpal.size(); i++) {
    rdr::U32 col = inpal.getColour(i);
    if (rgb) {
      pal[i].red = col & 0xff;
      pal[i].green = (col >> 8) & 0xff;
      pal[i].blue = (col >> 16) & 0xff;
    } else {
      pal[i].blue = col & 0xff;
      pal[i].green = (col >> 8) & 0xff;
      pal[i].red = (col >> 16) & 0xff;
    }
  }

  png_set_PLTE(png_ptr, info, pal, inpal.size());
  png_write_info(png_ptr, info);
  png_set_packing(png_ptr);

  for (i = 0; i < h; i++) {
    rdr::U32 *start = (rdr::U32 *) &data[stride * 4 * i];
    unsigned x;

    for (x = 0; x < w; x++) {
      rowbuf[x] = inpal.lookup(start[x]);
    }

    png_write_row(png_ptr, rowbuf);
  }

  png_write_end(png_ptr, NULL);
  png_destroy_info_struct(png_ptr, &info);
  png_destroy_write_struct(&png_ptr, NULL);
  fclose(f);

  return len;
}

void TightIndexedPNGEncoder::compressOnly(const PixelBuffer* pb,
                                    std::vector<uint8_t> &out, const Palette& pal) const
{
  const rdr::U8* buffer;
  int stride;
  buffer = pb->getBuffer(pb->getRect(), &stride);
  rdr::U8 *tmpbuf;

  const unsigned len = encode(&tmpbuf, buffer, pal, pfRGBX.equal(pb->getPF()), stride,
  				pb->getRect().width(), pb->getRect().height());

  out.resize(len);
  memcpy(&out[0], tmpbuf, len);

  free(tmpbuf);
}

void TightIndexedPNGEncoder::writeOnly(const std::vector<uint8_t> &out) const
{
  rdr::OutStream* os;

  os = conn->getOutStream();

  os->writeU8(tightIndexedPNG << 4);

  writeCompact(out.size(), os);
  os->writeBytes(&out[0], out.size());
}

void TightIndexedPNGEncoder::writeRect(const PixelBuffer* pb, const Palette& palette)
{
  const rdr::U8* buffer;
  int stride;

  rdr::OutStream* os;

  buffer = pb->getBuffer(pb->getRect(), &stride);
  rdr::U8 *tmpbuf;

  const unsigned len = encode(&tmpbuf, buffer, palette, pfRGBX.equal(pb->getPF()), stride,
  				pb->getRect().width(), pb->getRect().height());

  os = conn->getOutStream();

  os->writeU8(tightIndexedPNG << 4);

  writeCompact(len, os);
  os->writeBytes(tmpbuf, len);

  free(tmpbuf);
}

void TightIndexedPNGEncoder::writeSolidRect(int width, int height,
                                      const PixelFormat& pf,
                                      const rdr::U8* colour)
{
  // FIXME: Add a shortcut in the IndexedPNG compressor to handle this case
  //        without having to use the default fallback which is very slow.
  Encoder::writeSolidRect(width, height, pf, colour);
}

void TightIndexedPNGEncoder::writeCompact(rdr::U32 value, rdr::OutStream* os) const
{
  // Copied from TightEncoder as it's overkill to inherit just for this
  rdr::U8 b;

  b = value & 0x7F;
  if (value <= 0x7F) {
    os->writeU8(b);
  } else {
    os->writeU8(b | 0x80);
    b = value >> 7 & 0x7F;
    if (value <= 0x3FFF) {
      os->writeU8(b);
    } else {
      os->writeU8(b | 0x80);
      os->writeU8(value >> 14 & 0xFF);
    }
  }
}
