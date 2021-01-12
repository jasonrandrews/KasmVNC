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
#include <rfb/TightIndexedGIFEncoder.h>
#include <rfb/TightConstants.h>

#include <gif_lib.h>

using namespace rfb;

static const PixelFormat pfRGBX(32, 24, false, true, 255, 255, 255, 0, 8, 16);

TightIndexedGIFEncoder::TightIndexedGIFEncoder(SConnection* conn) :
  Encoder(conn, encodingTight, (EncoderFlags)(EncoderUseNativePF), 256),
  qualityLevel(-1)
{
}

TightIndexedGIFEncoder::~TightIndexedGIFEncoder()
{
}

bool TightIndexedGIFEncoder::isSupported()
{
  if (!conn->cp.supportsEncoding(encodingTight))
    return false;

  if (conn->cp.supportsIndexedGIF)
    return true;

  // Tight support, but not IndexedGIF
  return false;
}

void TightIndexedGIFEncoder::setQualityLevel(int level)
{
  qualityLevel = level;
}

bool TightIndexedGIFEncoder::treatLossless()
{
  return true;
}

static int gifwrite(GifFileType *gf, const GifByteType *dat, int len)
{
  FILE *f = (FILE *) gf->UserData;

  if (fwrite(dat, len, 1, f) != 1)
    abort();

  return len;
}

static unsigned encode(rdr::U8 **tmpbuf, const rdr::U8 *data, const Palette& inpal,
			const bool rgb, const unsigned stride,
			const unsigned w, const unsigned h)
{
  unsigned i;
  int err;
  size_t len = 0;
  rdr::U8 rowbuf[4096];

  FILE *f = open_memstream((char **) tmpbuf, &len);
  if (!f) abort();
  GifFileType *gf = EGifOpen(f, gifwrite, &err);
  if (!gf) abort();

  GifColorType pal[256];
  for (i = 0; i < (unsigned) inpal.size(); i++) {
    rdr::U32 col = inpal.getColour(i);
    if (rgb) {
      pal[i].Red = col & 0xff;
      pal[i].Green = (col >> 8) & 0xff;
      pal[i].Blue = (col >> 16) & 0xff;
    } else {
      pal[i].Blue = col & 0xff;
      pal[i].Green = (col >> 8) & 0xff;
      pal[i].Red = (col >> 16) & 0xff;
    }
  }

  ColorMapObject colmap;
  colmap.ColorCount = inpal.size();
  // Must be power of two
  if (colmap.ColorCount > 128) {
    colmap.ColorCount = 256;
    colmap.BitsPerPixel = 8;
  } else if (colmap.ColorCount > 64) {
    colmap.ColorCount = 128;
    colmap.BitsPerPixel = 7;
  } else if (colmap.ColorCount > 32) {
    colmap.ColorCount = 64;
    colmap.BitsPerPixel = 6;
  } else {
    colmap.ColorCount = 32;
    colmap.BitsPerPixel = 5;
  }
  colmap.SortFlag = false;
  colmap.Colors = pal;

  if (EGifPutScreenDesc(gf, w, h, colmap.BitsPerPixel, 0, &colmap) == GIF_ERROR)
    abort();

  if (EGifPutImageDesc(gf, 0, 0, w, h, 0, &colmap) == GIF_ERROR)
    abort();

  for (i = 0; i < h; i++) {
    rdr::U32 *start = (rdr::U32 *) &data[stride * 4 * i];
    unsigned x;

    for (x = 0; x < w; x++) {
      rowbuf[x] = inpal.lookup(start[x]);
    }

    EGifPutLine(gf, rowbuf, w);
  }

  EGifCloseFile(gf, NULL);
  fclose(f);

  return len;
}

void TightIndexedGIFEncoder::compressOnly(const PixelBuffer* pb,
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

void TightIndexedGIFEncoder::writeOnly(const std::vector<uint8_t> &out) const
{
  rdr::OutStream* os;

  os = conn->getOutStream();

  os->writeU8(tightIndexedGIF << 4);

  writeCompact(out.size(), os);
  os->writeBytes(&out[0], out.size());
}

void TightIndexedGIFEncoder::writeRect(const PixelBuffer* pb, const Palette& palette)
{
  const rdr::U8* buffer;
  int stride;

  rdr::OutStream* os;

  buffer = pb->getBuffer(pb->getRect(), &stride);
  rdr::U8 *tmpbuf;

  const unsigned len = encode(&tmpbuf, buffer, palette, pfRGBX.equal(pb->getPF()), stride,
  				pb->getRect().width(), pb->getRect().height());

  os = conn->getOutStream();

  os->writeU8(tightIndexedGIF << 4);

  writeCompact(len, os);
  os->writeBytes(tmpbuf, len);

  free(tmpbuf);
}

void TightIndexedGIFEncoder::writeSolidRect(int width, int height,
                                      const PixelFormat& pf,
                                      const rdr::U8* colour)
{
  // FIXME: Add a shortcut in the IndexedGIF compressor to handle this case
  //        without having to use the default fallback which is very slow.
  Encoder::writeSolidRect(width, height, pf, colour);
}

void TightIndexedGIFEncoder::writeCompact(rdr::U32 value, rdr::OutStream* os) const
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
