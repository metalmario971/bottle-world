/**
*  @file RenderTarget.h
*  @date May 27, 2017
*  @author MetalMario971
*/
#pragma once
#ifndef __RENDERTARGET_149586780355650870_H__
#define __RENDERTARGET_149586780355650870_H__

#include "../gfx/RenderTarget.h"

namespace BR2 {
/**
*    @class RenderTarget
*    @brief The output of a rendering operation.
*/
//Storage class for OpenGL render target data.
class BufferRenderTarget : public RenderTarget {
  friend class FramebufferBase;
public:
  BufferRenderTarget(std::shared_ptr<GLContext> ctx, bool bShared);
  virtual ~BufferRenderTarget() override;

  bool getShared() { return _bShared; }
  GLuint getGlTexId() { return _iGlTexId; }
  string_t getName() { return _strName; }
  GLenum getTextureChannel() { return _eTextureChannel; }
  GLenum getAttachment() { return _eAttachment; }
  GLenum getTextureTarget() { return _eTextureTarget; }
  GLuint getTexId() { return _iGlTexId; }
  GLint getLayoutIndex() { return _iLayoutIndex; }
  RenderTargetType::e getTargetType() { return _eTargetType; }
  GLenum getBlitBit() { return _eBlitBit; }
  bool getMsaaEnabled();

  void bind(GLenum eAttachment = 0);
  
  virtual int32_t getWidth() override;
  virtual int32_t getHeight() override;

private:
  string_t _strName;
  GLuint _iGlTexId;    // Texture Id
  GLenum _eTextureTarget; //GL_TEXTURE_2D, or other
  GLenum _eAttachment;//GL_COLORATTACHMENT_0 + n
  GLint _iLayoutIndex;// The (layout = 0).. in the shader
  GLenum _eTextureChannel;//GL_TEXTURE0 +..
  GLenum _eBlitBit; // GL_COLOR_BUFFER_BIT or GL_DEPTH_BUFFER_BIT
  RenderTargetType::e _eTargetType;
  bool _bShared = false;
  int32_t _iWidth = 0;
  int32_t _iHeight = 0;
};


}//ns Game



#endif
