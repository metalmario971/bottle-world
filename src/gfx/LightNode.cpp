#include "../base/GLContext.h"
#include "../base/Logger.h"
#include "../base/Exception.h"
#include "../base/EngineConfig.h"
#include "../base/GraphicsWindow.h"
#include "../base/FpsMeter.h"
#include "../base/Gu.h"
#include "../base/Perf.h"

#include "../gfx/LightNode.h"
#include "../gfx/CameraNode.h"
#include "../gfx/FrustumBase.h"
#include "../gfx/LightManager.h"
#include "../gfx/ShadowBox.h"
#include "../gfx/ShadowFrustum.h"
#include "../model/OBB.h"
#include "../world/Scene.h"

namespace BR2 {
#pragma region LightNodeBase
LightNodeBase::LightNodeBase(string_t name, bool bShadow) : PhysicsNode(name, nullptr) {
  _bEnableShadows = bShadow;
  _color = vec4(1, 1, 1, 1);
}
LightNodeBase::~LightNodeBase() {
}
std::shared_ptr<LightManager> LightNodeBase::getLightManager() {
  return getScene()->getLightManager();
}
void LightNodeBase::init() {
  PhysicsNode::init();
}
void LightNodeBase::update(float delta, std::map<Hash32, std::shared_ptr<Animator>>& mapAnimators) {
  PhysicsNode::update(delta, mapAnimators);
}
vec3* LightNodeBase::getFinalPosPtr() {
  _vGpuBufferedPosition = getFinalPos();//getTransform()->getPos();
  return &_vGpuBufferedPosition;
}
bool LightNodeBase::getIsShadowsEnabled() {
  return _bEnableShadows && (Gu::getEngineConfig()->getEnableObjectShadows() ||
    Gu::getEngineConfig()->getEnableTerrainShadows());
}
#pragma endregion

#pragma region LightNodeDir
LightNodeDir::LightNodeDir(string_t name, bool bShadow) : LightNodeBase(name, bShadow) {

}
std::shared_ptr<LightNodeDir> LightNodeDir::create(string_t name, bool bShadow) {
  std::shared_ptr<LightNodeDir> lp = std::make_shared<LightNodeDir>(name, bShadow);
  lp->init();
  lp->_pSpec = std::make_shared<BaseSpec>("*LightNodeDir");

  return lp;
}
LightNodeDir::~LightNodeDir() {
  //DEL_MEM(_pGpuLight);
  //DEL_MEM(_pShadowFrustum);
}
void LightNodeDir::setLookAt(const vec3& v) {
  _vLookAt = v;
  _pShadowFrustum->setChanged();
}
void LightNodeDir::init() {
  LightNodeBase::init();

  _vLookAt = vec3(0, 0, 0);
  _vDir = vec3(0, 0, 1);
  _vUp = vec3(0, 1, 0);
  _vRight = vec3(1, 0, 0);

  int32_t iShadowMapRes = Gu::getEngineConfig()->getShadowMapResolution();
  _pShadowFrustum = std::make_shared<ShadowFrustum>(std::dynamic_pointer_cast<LightNodeDir>(shared_from_this()),
    iShadowMapRes, iShadowMapRes);    //TEST
  _pShadowFrustum->init();
  _pShadowFrustum->getFrustum()->setZFar(500);

  _pGpuLight = std::make_shared<GpuDirLight>();
  setMaxDistance(100.0f);
  _pShadowFrustum->setChanged();
}

void LightNodeDir::setMaxDistance(float f) {
  _pShadowFrustum->getFrustum()->setZFar(f);
  _pShadowFrustum->setChanged();
}
void LightNodeDir::update(float delta, std::map<Hash32, std::shared_ptr<Animator>>& mapAnimators) {
  LightNodeBase::update(delta, mapAnimators);
}
std::future<bool> LightNodeDir::cullShadowVolumesAsync(CullParams& cp) {
  //*Remove this.  It needs to be on the RenderBucket itself, per-frustum
  std::future<bool> fut = std::async(std::launch::async, [&] {
    Perf::pushPerf();
    {
      _pShadowFrustum->updateAndCullAsync(cp);

      _vDir = (getLookAt() - getFinalPos()).normalized();
      vec3 vUp = vec3(0, 1, 0);
      _vRight = (vUp.cross(_vDir)).normalized();
      _vUp = (_vDir.cross(_vRight)).normalized();

      //update
      _pGpuLight->_vPos = getFinalPos();
      _pGpuLight->_vDir = _vDir;
      _pGpuLight->_fMaxDistance = _pShadowFrustum->getFrustum()->getZFar();
      _pGpuLight->_vDiffuseColor = _color;
      _pGpuLight->_fLinearAttenuation = 0.0f;
      _pGpuLight->_mView = *_pShadowFrustum->getViewMatrixPtr();
      _pGpuLight->_mProj = *_pShadowFrustum->getProjMatrixPtr();
      _pGpuLight->_mPVB = *_pShadowFrustum->getPVBPtr();
    }
    Perf::popPerf();

    return true;
    });
  return fut;
}
bool LightNodeDir::renderShadows(std::shared_ptr<ShadowFrustum> pf) {
  //**Update shadow map frustum.
  if (getIsShadowsEnabled()) {
    _pShadowFrustum->renderShadows(pf);
  }
  return true;
}
void LightNodeDir::calcBoundBox(Box3f& __out_ pBox, const vec3& obPos, float extra_pad) {
  pBox.genResetLimits();
  pBox = *_pShadowFrustum->getFrustum()->getBoundBox();
  getOBB()->setInvalid();

  SceneNode::calcBoundBox(pBox, obPos, extra_pad);
}
#pragma endregion
#pragma region LightNodePoint
LightNodePoint::LightNodePoint(string_t name, bool bShadowBox) : LightNodeBase(name, bShadowBox) {
}
std::shared_ptr<LightNodePoint> LightNodePoint::create(string_t name, bool bhasShadowBox) {
  std::shared_ptr<LightNodePoint> lp = std::make_shared<LightNodePoint>(name, bhasShadowBox);
  lp->init();
  lp->_pSpec = std::make_shared<BaseSpec>("*LightNodePoint");
  return lp;
}
std::shared_ptr<LightNodePoint> LightNodePoint::create(string_t name, vec3&& pos, float radius, vec4&& color, string_t action, bool bShadowsEnabled) {
  std::shared_ptr<LightNodePoint> lp = LightNodePoint::create(name, bShadowsEnabled);
  lp->update(0.0f, std::map<Hash32, std::shared_ptr<Animator>>());
  lp->setPos(std::move(pos));
  lp->setLightRadius(radius);
  lp->setLightColor(std::move(color));
  lp->getShadowBox()->getSmallBoxSize() = 0.6f;
  return lp;
}

LightNodePoint::~LightNodePoint() {
  _pShadowBox = nullptr;
  _pGpuLight = nullptr;
}
void LightNodePoint::init() {
  LightNodeBase::init();
  _pGpuLight = std::make_shared<GpuPointLight>();

  int32_t iShadowMapRes = Gu::getEngineConfig()->getShadowMapResolution();
  _pShadowBox = std::make_shared<ShadowBox>(std::dynamic_pointer_cast<LightNodePoint>(shared_from_this()), iShadowMapRes, iShadowMapRes);    //TEST
  _pShadowBox->init();

}
void LightNodePoint::update(float delta, std::map<Hash32, std::shared_ptr<Animator>>& mapAnimators) {
  LightNodeBase::update(delta, mapAnimators);
}
std::future<bool> LightNodePoint::cullShadowVolumesAsync(CullParams& cp) {

  //*Remove this.  It needs to be on the RenderBucket itself, per-frustum
  std::future<bool> fut = std::async(std::launch::async, [&] {
    Perf::pushPerf();
    {
      //**TODO: Uncomment
      //**TODO: Uncomment
      //**TODO: Uncomment
      //std::future<bool> b = _pShadowBox->updateAndCullAsync(cp);
      //b.wait();
      //**TODO: Uncomment
      //**TODO: Uncomment
      //**TODO: Uncomment

      updateFlicker();

      //update
      _pGpuLight->_vPos = getFinalPos();
      _pGpuLight->_fRadius = _fRadius;
      _pGpuLight->_vDiffuseColor = _color;
    }
    Perf::popPerf();
    return true;
  });
  return fut;
}
bool LightNodePoint::renderShadows(std::shared_ptr<ShadowBox> pf) {
  //**Update shadow map frustum.
  if (getIsShadowsEnabled()) {
    _pShadowBox->renderShadows(pf);
  }

  return true;
}
void LightNodePoint::updateFlicker() {
  if (_bFlickerEnabled) {
    auto s = getScene();
    if (s) {
      auto w = s->getWindow();
      if (w) {

        _fFlickerValue += (1 / 10.0f) * 1.0f / w->getFpsMeter()->getFps();

        if (_fFlickerValue >= 1.0f) {
          _fLastRandomValue = _fNextRandomValue;
          _fNextRandomValue = Random::nextFloat(0.0f, 1.0f);

          _fFlickerValue = 0.0f;
        }

        _fFlickerAddRadius = _fFlickerMaxRadius * (Alg::cosine_interpolate(_fLastRandomValue, _fNextRandomValue, _fFlickerValue));//pnoise in range -1,1 so make it 1,1

      }
    }


  }

  // optimized radius for shader.
  float f = getLightRadius();
  if (f != 0.0f) {
    _f_1_Radius_2 = (1.0f) / (f * f);
  }
}
float LightNodePoint::getLightRadius() {
  //Negative light radiuses are infinite
  if (_fRadius == _cInfiniteLightRadius)
    return _cInfiniteLightRadius;
  else
    return(_fRadius + _fFlickerAddRadius);
}
void LightNodePoint::setLightRadius(float r) {
  if (r < 0.0f) {
    r = _cInfiniteLightRadius;
  }
  _fRadius = r;
}

void LightNodePoint::calcBoundBox(Box3f& __out_ pBox, const vec3& obPos, float extra_pad) {
  pBox.genResetLimits();
  pBox._min = pBox._max = getPos();
  pBox._min -= getLightRadius();
  pBox._max += getLightRadius();
  getOBB()->setInvalid();

  SceneNode::calcBoundBox(pBox, obPos, extra_pad);
}
#pragma endregion

}//ns game
