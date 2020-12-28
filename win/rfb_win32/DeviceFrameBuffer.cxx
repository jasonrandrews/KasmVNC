/* Copyright (C) 2002-2005 RealVNC Ltd.  All Rights Reserved.
 * Copyright 2014-2017 Pierre Ossman for Cendio AB
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

 // -=- DeviceFrameBuffer.cxx
 //
 // The DeviceFrameBuffer class encapsulates the pixel data of the system
 // display.

#include <vector>
#include <rfb_win32/DeviceFrameBuffer.h>
#include <rfb_win32/DeviceContext.h>
#include <rfb_win32/IconInfo.h>
#include <rfb/VNCServer.h>
#include <rfb/LogWriter.h>

#include <intrin.h>

#include <os/w32tiger.h>

using namespace rfb;
using namespace win32;

static LogWriter vlog("DeviceFrameBuffer");

// -=- DeviceFrameBuffer class

DeviceFrameBuffer::DeviceFrameBuffer(HDC device, const Rect& wRect)
    : device(device)
    , d3dDevice(0)
    , d3dDeviceContext(0)
    , dxgiOutputDuplication(0)
    , frameTexture(0)
{
    //
    HDESK desktop = OpenInputDesktop(0, FALSE, GENERIC_ALL);

    if (!desktop) {
        throw Exception("OpenInputDesktop() failed!");
    }

    D3D_DRIVER_TYPE driverTypes[] = {
        D3D_DRIVER_TYPE_HARDWARE,
        D3D_DRIVER_TYPE_WARP
    };

    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
        D3D_FEATURE_LEVEL_9_1
    };

    HRESULT result = 0;

    for (D3D_DRIVER_TYPE& driver : driverTypes) {
        D3D_FEATURE_LEVEL featureLevel;

        if ((result = D3D11CreateDevice(0, driver, 0, 0, featureLevels, ARRAYSIZE(featureLevels), D3D11_SDK_VERSION, &d3dDevice, &featureLevel, &d3dDeviceContext)) >= 0) {
            break;
        }
    }

    if (result < 0) {
        throw Exception("D3D11CreateDevice() failed!");
    }

    //
    IDXGIDevice* dxgiDevice = 0;

    result = d3dDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);

    if (result < 0) {
        throw Exception("d3dDevice->QueryInterface() failed!");
    }

    //
    IDXGIAdapter* dxgiAdapter = 0;

    result = dxgiDevice->GetParent(__uuidof(IDXGIAdapter), (void**)&dxgiAdapter);

    dxgiDevice->Release();
    dxgiDevice = 0;

    if (result < 0) {
        throw Exception("dxgiDevice->GetParent() failed!");
    }

    //
    IDXGIOutput* dxgiOutput = 0;

    result = dxgiAdapter->EnumOutputs(0, &dxgiOutput);

    dxgiAdapter->Release();
    dxgiAdapter = 0;

    if (result < 0) {
        throw Exception("dxgiAdapter->EnumOutputs() failed!");
    }

    
    DXGI_OUTPUT_DESC outputDescription = { 0 };
    dxgiOutput->GetDesc(&outputDescription);

    //
    IDXGIOutput6* dxgiOutputImpl = 0;

    result = dxgiOutput->QueryInterface(__uuidof(dxgiOutputImpl), (void**)&dxgiOutputImpl);

    dxgiOutput->Release();
    dxgiOutput = 0;

    if (result < 0) {
        throw Exception("dxgiOutput->QueryInterface() failed!");
    }

    //
    result = dxgiOutputImpl->DuplicateOutput(d3dDevice, &dxgiOutputDuplication);

    dxgiOutputImpl->Release();
    dxgiOutputImpl = 0;

    if (result == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE) {
        throw Exception("Too many desktop records active at once!");
    }

    if (result < 0) {
        throw Exception("dxgiOutputImpl->DuplicateOutput() failed!");
    }

    //
    format.parse("rgb888");
    width_ = outputDescription.DesktopCoordinates.right - outputDescription.DesktopCoordinates.left;
    height_ = outputDescription.DesktopCoordinates.bottom - outputDescription.DesktopCoordinates.top;

    //
    data = new rdr::U8[width_ * height_ * 4];
    memset(data, 0, width_ * height_ * 4);
    stride = width_;
}

DeviceFrameBuffer::~DeviceFrameBuffer() {
    if (dxgiOutputDuplication) {
        dxgiOutputDuplication->ReleaseFrame();
        dxgiOutputDuplication->Release();
    }

    if (d3dDeviceContext) {
        d3dDeviceContext->Release();
    }

    if (d3dDevice) {
        d3dDevice->Release();
    }
}

void
DeviceFrameBuffer::grabRect(const Rect &rect) {
}

void
DeviceFrameBuffer::grabRegion(const Region &rgn) {
  //  acquire the next frame
  HRESULT result = 0;

  IDXGIResource* frameResource = 0;
  DXGI_OUTDUPL_FRAME_INFO frameInfo = { 0 };
  dxgiOutputDuplication->AcquireNextFrame(0, &frameInfo, &frameResource);

  // no new frames, data still holdes the last frame
  if (frameInfo.AccumulatedFrames == 0) {
    dxgiOutputDuplication->ReleaseFrame();
    return;
  }

  // access the screen as a GPU texture
  ID3D11Texture2D* screenTexture = 0;

  result = frameResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&screenTexture);

  frameResource->Release();
  frameResource = 0;
  
  if (result < 0) {
    return;
  }

  // create a CPU accessible texture, if necessary
  if (!frameTexture) {
    D3D11_TEXTURE2D_DESC textureDescription = { 0 };
    screenTexture->GetDesc(&textureDescription);
    textureDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    textureDescription.Usage = D3D11_USAGE_STAGING;
    textureDescription.BindFlags = 0;
    textureDescription.MiscFlags = 0;
    d3dDevice->CreateTexture2D(&textureDescription, 0, &frameTexture);
  }

  // copy whole screen
  // TODO: change to region based copying
  ID3D11DeviceContext* immediateContext = 0;
  d3dDevice->GetImmediateContext(&immediateContext);
  immediateContext->CopyResource(frameTexture, screenTexture);

  // copy the data to the internal buffer
  D3D11_MAPPED_SUBRESOURCE textureData = { 0 };

  result = immediateContext->Map(frameTexture, 0, D3D11_MAP_READ, 0, &textureData);

  if (result == 0) {
    for (int y = 0; y < height_; y++) {
      memcpy(data + y * width_ * 4, (uint8_t*)textureData.pData + textureData.RowPitch * y, width_ * 4);
    }
  }

  d3dDeviceContext->Unmap(frameTexture, 0);

  //
  dxgiOutputDuplication->ReleaseFrame();
}

void DeviceFrameBuffer::setCursor(HCURSOR hCursor, VNCServer* server)
{
   // - If hCursor is null then there is no cursor - clear the old one

  if (hCursor == 0) {
    server->setCursor(0, 0, Point(), NULL);
    return;
  }

  try {

    int width, height;
    rdr::U8Array buffer;

    // - Get the size and other details about the cursor.

    IconInfo iconInfo((HICON)hCursor);

    BITMAP maskInfo;
    if (!GetObject(iconInfo.hbmMask, sizeof(BITMAP), &maskInfo))
      throw rdr::SystemException("GetObject() failed", GetLastError());
    if (maskInfo.bmPlanes != 1)
      throw rdr::Exception("unsupported multi-plane cursor");
    if (maskInfo.bmBitsPixel != 1)
      throw rdr::Exception("unsupported cursor mask format");

    width = maskInfo.bmWidth;
    height = maskInfo.bmHeight;
    if (!iconInfo.hbmColor)
      height /= 2;

    buffer.buf = new rdr::U8[width * height * 4];

    Point hotspot = Point(iconInfo.xHotspot, iconInfo.yHotspot);

    if (iconInfo.hbmColor) {
      // Colour cursor
      BITMAPV5HEADER bi;
      BitmapDC dc(device, iconInfo.hbmColor);

      memset(&bi, 0, sizeof(BITMAPV5HEADER));

      bi.bV5Size        = sizeof(BITMAPV5HEADER);
      bi.bV5Width       = width;
      bi.bV5Height      = -height; // Negative for top-down
      bi.bV5Planes      = 1;
      bi.bV5BitCount    = 32;
      bi.bV5Compression = BI_BITFIELDS;
      bi.bV5RedMask     = 0x000000FF;
      bi.bV5GreenMask   = 0x0000FF00;
      bi.bV5BlueMask    = 0x00FF0000;
      bi.bV5AlphaMask   = 0xFF000000;

      if (!GetDIBits(dc, iconInfo.hbmColor, 0, height,
                     buffer.buf, (LPBITMAPINFO)&bi, DIB_RGB_COLORS))
        throw rdr::SystemException("GetDIBits", GetLastError());

      // We may not get the RGBA order we want, so shuffle things around
      int ridx, gidx, bidx, aidx;

      ridx = __builtin_ffs(bi.bV5RedMask) / 8;
      gidx = __builtin_ffs(bi.bV5GreenMask) / 8;
      bidx = __builtin_ffs(bi.bV5BlueMask) / 8;
      // Usually not set properly
      aidx = 6 - ridx - gidx - bidx;

      if ((bi.bV5RedMask != ((unsigned)0xff << ridx*8)) ||
          (bi.bV5GreenMask != ((unsigned)0xff << gidx*8)) ||
          (bi.bV5BlueMask != ((unsigned)0xff << bidx*8)))
        throw rdr::Exception("unsupported cursor colour format");

      rdr::U8* rwbuffer = buffer.buf;
      for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
          rdr::U8 r, g, b, a;

          r = rwbuffer[ridx];
          g = rwbuffer[gidx];
          b = rwbuffer[bidx];
          a = rwbuffer[aidx];

          rwbuffer[0] = r;
          rwbuffer[1] = g;
          rwbuffer[2] = b;
          rwbuffer[3] = a;

          rwbuffer += 4;
        }
      }
    } else {
      // B/W cursor

      rdr::U8Array mask(maskInfo.bmWidthBytes * maskInfo.bmHeight);
      rdr::U8* andMask = mask.buf;
      rdr::U8* xorMask = mask.buf + height * maskInfo.bmWidthBytes;

      if (!GetBitmapBits(iconInfo.hbmMask,
                         maskInfo.bmWidthBytes * maskInfo.bmHeight, mask.buf))
        throw rdr::SystemException("GetBitmapBits", GetLastError());

      bool doOutline = false;
      rdr::U8* rwbuffer = buffer.buf;
      for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
          int byte = y * maskInfo.bmWidthBytes + x / 8;
          int bit = 7 - x % 8;

          if (!(andMask[byte] & (1 << bit))) {
            // Valid pixel, so make it opaque
            rwbuffer[3] = 0xff;

            // Black or white?
            if (xorMask[byte] & (1 << bit))
              rwbuffer[0] = rwbuffer[1] = rwbuffer[2] = 0xff;
            else
              rwbuffer[0] = rwbuffer[1] = rwbuffer[2] = 0;
          } else if (xorMask[byte] & (1 << bit)) {
            // Replace any XORed pixels with black, because RFB doesn't support
            // XORing of cursors.  XORing is used for the I-beam cursor, which is most
            // often used over a white background, but also sometimes over a black
            // background.  We set the XOR'd pixels to black, then draw a white outline
            // around the whole cursor.

            rwbuffer[0] = rwbuffer[1] = rwbuffer[2] = 0;
            rwbuffer[3] = 0xff;

            doOutline = true;
          } else {
            // Transparent pixel
            rwbuffer[0] = rwbuffer[1] = rwbuffer[2] = rwbuffer[3] = 0;
          }

          rwbuffer += 4;
        }
      }

      if (doOutline) {
        vlog.debug("drawing cursor outline!");

        // The buffer needs to be slightly larger to make sure there
        // is room for the outline pixels
        rdr::U8Array outline((width + 2)*(height + 2)*4);
        memset(outline.buf, 0, (width + 2)*(height + 2)*4);

        // Pass 1, outline everything
        rdr::U8* in = buffer.buf;
        rdr::U8* out = outline.buf + width*4 + 4;
        for (int y = 0; y < height; y++) {
          for (int x = 0; x < width; x++) {
            // Visible pixel?
            if (in[3] > 0) {
              // Outline above...
              memset(out - (width+2)*4 - 4, 0xff, 4 * 3);
              // ...besides...
              memset(out - 4, 0xff, 4 * 3);
              // ...and above
              memset(out + (width+2)*4 - 4, 0xff, 4 * 3);
            }
            in += 4;
            out += 4;
          }
          // outline is slightly larger
          out += 2*4;
        }

        // Pass 2, overwrite with actual cursor
        in = buffer.buf;
        out = outline.buf + width*4 + 4;
        for (int y = 0; y < height; y++) {
          for (int x = 0; x < width; x++) {
            if (in[3] > 0)
              memcpy(out, in, 4);
            in += 4;
            out += 4;
          }
          out += 2*4;
        }

        width += 2;
        height += 2;
        hotspot.x += 1;
        hotspot.y += 1;

        delete [] buffer.buf;
        buffer.buf = outline.takeBuf();
      }
    }

    server->setCursor(width, height, hotspot, buffer.buf);

  } catch (rdr::Exception& e) {
    vlog.error("%s", e.str());
  }
}
