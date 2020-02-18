#include "../base/GLContext.h"
#include "../model/UtilMeshBox.h"
#include "../model/MeshUtils.h"

namespace BR2 {

UtilMeshBox::UtilMeshBox(std::shared_ptr<CameraNode> cam, std::shared_ptr<GLContext> ctx, Box3f* pCube, vec3& vOffset, Color4f& color) :
  UtilMesh(cam, ctx, MeshUtils::MeshMakerVert::getVertexFormat(), nullptr, GL_TRIANGLES)
  , _vOffset(vOffset)
  , _vColor(color)
  , _blnWireFrame(true)
  , _pCube(pCube) {
  //wireframe isn't getting unloaded. 
}
UtilMeshBox::~UtilMeshBox() {
}
void UtilMeshBox::generate() {
  mat4 m = mat4::identity();
  //Note: to copy everything is too slow.  We will just keep the spec here and delete it when done
  std::shared_ptr<MeshData> ms = MeshUtils::makeBox(getContext(), _pCube, &_vColor, &m, &_vOffset);
  copyFromSpec(ms);
}
void UtilMeshBox::preDraw() {
  if (_blnWireFrame == true) {
    glPolygonMode(GL_FRONT, GL_LINE); //GfxPolygonMode::PolygonModeWireframe);
  }

  getContext()->setLineWidth(1.0);
  //Gd::setLineWidth(1.0f);
}
void UtilMeshBox::postDraw() {
  //if(_blnWireFrame==true)
  //    Gd::popPolygonMode();
  glPolygonMode(GL_FRONT, GL_FILL);

  //CRITICAL we set these to null.
  //_verts = NULL;
  //_indexes = NULL;
}

}//ns BR2
