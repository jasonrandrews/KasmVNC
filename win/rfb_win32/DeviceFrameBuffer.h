/* Copyright (C) 2002-2005 RealVNC Ltd.  All Rights Reserved.
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

// -=- DeviceFrameBuffer.h

// *** THIS INTERFACE NEEDS TIDYING TO SEPARATE COORDINATE SYSTEMS BETTER ***

#ifndef __RFB_WIN32_DEVICE_FRAME_BUFFER_H__
#define __RFB_WIN32_DEVICE_FRAME_BUFFER_H__

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_6.h>

#include <rfb/Cursor.h>
#include <rfb/Region.h>
#include <rfb/Exception.h>
#include <rfb/Configuration.h>

namespace rfb {

  class VNCServer;

  namespace win32 {

    // -=- DeviceFrameBuffer interface
    class DeviceFrameBuffer : public FullFramePixelBuffer {
    public:
      DeviceFrameBuffer(HDC deviceContext, const Rect& area_=Rect());
      virtual ~DeviceFrameBuffer();

      // - FrameBuffer overrides
      virtual void grabRect(const Rect &rect);
      virtual void grabRegion(const Region &region);

      // - DeviceFrameBuffer specific methods
      void setCursor(HCURSOR c, VNCServer* server);

    protected:
      HDC device;
      ID3D11Device* d3dDevice;
      ID3D11DeviceContext* d3dDeviceContext;
      IDXGIOutputDuplication* dxgiOutputDuplication;
      ID3D11Texture2D* frameTexture;
    };
  };
};

#endif // __RFB_WIN32_DEVICE_FRAME_BUFFER_H__
