#include "../base/Logger.h"
#include "../base/OglErr.h"
#include "../base/Perf.h"
#include "../base/GLContext.h"
#include "../math/Random.h"
#include "../model/OBB.h"
#include "../gfx/GpuComputeSync.h"
#include "../gfx/ShaderMaker.h"
#include "../gfx/ShaderBase.h"
#include "../gfx/CameraNode.h"
#include "../gfx/RenderParams.h"
#include "../gfx/LightNode.h"
#include "../gfx/LightManager.h"
#include "../gfx/ShadowBoxSide.h"
#include "../gfx/Picker.h"
#include "../gfx/TexCache.h"
#include "../gfx/RenderSettings.h"
#include "../gfx/Picker.h"
#include "../gfx/RenderParams.h"
#include "../model/Material.h"
#include "../model/ModelCache.h"
#include "../model/ShaderStorageBuffer.h"
#include "../model/FragmentBufferData.h"
#include "../model/IndexBufferData.h"
#include "../model/MeshSpec.h"
#include "../model/Model.h"
#include "../model/MeshSpec.h"
#include "../model/VboData.h"
#include "../model/IboData.h"
#include "../model/VaoDataGeneric.h"
#include "../model/MeshNode.h"
#include "../world/PhysicsWorld.h"
#include "../world/RenderBucket.h"
#include "../world/NodeUtils.h"
#include <iostream>

namespace BR2 {
MeshNode::MeshNode(string_t name, bool pickable, std::shared_ptr<MeshSpec> ms) : MeshNode(name, pickable, ms, nullptr) {
}
MeshNode::MeshNode(string_t name, bool pickable, std::shared_ptr<MeshSpec> ps, std::shared_ptr<ModelNode> mn) : SceneNode(name, ps) {
  _pModelNode = mn;
  _bPickable = pickable;
}
MeshNode::~MeshNode() {
  //  DEL_MEM(_pVaoData);
  //  DEL_MEM(_pMaterial);
  //  DEL_MEM(_pArmJoints);
  _vecBoneNodesOrdered.resize(0);
  _vecArmaturesOrdered.resize(0);
}
std::shared_ptr<MeshNode> MeshNode::create(string_t name, bool pickable, std::shared_ptr<MeshSpec> ps, std::shared_ptr<ModelNode> mn) {
  std::shared_ptr<MeshNode> m = std::make_shared<MeshNode>(name, pickable, ps, mn);
  m->init();
  return m;
}
std::shared_ptr<MeshNode> MeshNode::create(string_t name, bool pickable, std::shared_ptr<MeshSpec> pd) {
  std::shared_ptr<MeshNode> m = std::make_shared<MeshNode>(name, pickable, pd);
  m->init();
  return m;
}
void MeshNode::init() {
  SceneNode::init();
  if (getMeshSpec()->getHideRender()) {
    show();
  }
  else {
    hide();
  }

  if (getMeshSpec()->getMaterial() != nullptr) {
    _pMaterial = getMeshSpec()->getMaterial()->makeCopy();
  }

  if (getMeshSpec()->hasSkin()) {
    createSkin();
  }
}
std::shared_ptr<MeshSpec> MeshNode::getMeshSpec() { return std::dynamic_pointer_cast<MeshSpec>(SceneNode::getSpec()); }
void MeshNode::printDataToStdout() {
  GpuAnimatedMeshWeightData* wdat = new GpuAnimatedMeshWeightData[getMeshSpec()->getWeightOffsetsGpu()->getNumElements()];
  getMeshSpec()->getWeightOffsetsGpu()->copyDataServerClient(getMeshSpec()->getWeightOffsetsGpu()->getNumElements(), wdat);
  std::cout << "WDAT" << std::endl;
  for (size_t i = 0; i < getMeshSpec()->getWeightOffsetsGpu()->getNumElements(); ++i) {
    std::cout << " (" << wdat[i]._offset << ", " << wdat[i]._count << ") ";
  }
  std::cout << std::endl;
  std::cout << std::endl;
  delete[] wdat;

  GpuAnimatedMeshWeight* weights = new GpuAnimatedMeshWeight[getMeshSpec()->getWeightsGpu()->getNumElements()];
  getMeshSpec()->getWeightsGpu()->copyDataServerClient(getMeshSpec()->getWeightsGpu()->getNumElements(), weights);
  std::cout << "Weihts" << std::endl;
  for (size_t i = 0; i < getMeshSpec()->getWeightsGpu()->getNumElements(); ++i) {
    std::cout << " (" << weights[i]._iArmJointOffset << ", " << weights[i]._weight << ") ";
  }
  std::cout << std::endl;
  std::cout << std::endl;
  delete[] weights;

  GpuAnimatedMeshSkinMatrix* arms = new GpuAnimatedMeshSkinMatrix[_pArmJoints->getNumElements()];
  _pArmJoints->copyDataServerClient(_pArmJoints->getNumElements(), arms);
  std::cout << "ARM MATS" << std::endl;
  for (size_t i = 0; i < _pArmJoints->getNumElements(); ++i) {
    std::cout << " (" << arms[i]._matrix.toString() << ") ";
  }
  std::cout << std::endl;
  std::cout << std::endl;
  delete[] arms;
}
void MeshNode::createSkin() {
  AssertOrThrow2(_pModelNode != nullptr);
  AssertOrThrow2(getMeshSpec() != nullptr);

  //Fill identity buffer of skin mats
  int32_t iOrdCount = 0;
  std::vector <mat4> idents;
  for (std::pair<Hash32, std::shared_ptr<Armature>> parm : _pModelNode->getModelSpec()->getArmatureMapOrdered()) {
    for (std::pair<Hash32, std::shared_ptr<BoneSpec>> pbone : *parm.second->getBoneCacheOrdered()) {
      iOrdCount++;
      idents.push_back(mat4::identity());
    }
  }

  _pArmJoints = std::make_shared<ShaderStorageBuffer>(Gu::getCoreContext(), sizeof(GpuAnimatedMeshSkinMatrix));
  _pArmJoints->allocate(iOrdCount);
  _pArmJoints->copyDataClientServer(iOrdCount, (void*)idents.data());

  orderBoneNodesForGpu();
}
void MeshNode::orderBoneNodesForGpu() {
  //Retrieve a list of all matrixes as appearing on the GPU - this si crucially ordered.
  //[arm, bone,bone, arm2, bone, bone...]
  //Armature order comes frist
 // int32_t iMaxArmId = -1;
  _vecBoneNodesOrdered.resize(0);
  _vecArmaturesOrdered.resize(0);

  //Build list of armatures, ordered by ID
  for (std::pair<Hash32, std::shared_ptr<Armature>> arm_pair : _pModelNode->getModelSpec()->getArmatureMapOrdered()) {
    for (std::shared_ptr<ArmatureNode> parm_node : _pModelNode->getArmatureNodes()) {
      if (arm_pair.second == parm_node->getSpec()) {
        _vecArmaturesOrdered.push_back(parm_node);
      }
    }
  }

  //Build list of bone nodes, ordered by armature, then bone id
  int32_t iMaxBoneId = -1;
  for (std::shared_ptr<ArmatureNode> parm_node : _vecArmaturesOrdered) {
    iMaxBoneId = -1;//Reset for each armature

    for (std::shared_ptr<BoneNode> bn : parm_node->getBonesOrdered()) {
      _vecBoneNodesOrdered.push_back(bn);

      //Check bone order is sequential.
      if (bn->getBoneSpec()->getBoneId() > iMaxBoneId) {
        iMaxBoneId = bn->getBoneSpec()->getBoneId();
      }
      else {
        BRLogError("'" + getMeshSpec()->getName() + "' : Node, Bone Cache is not ordered properly.");
        Gu::debugBreak();
      }
    }
  }
  //*If we have skin we have to have bone nodes, or lese there was an error somewhere.
  AssertOrThrow2(_vecBoneNodesOrdered.size() > 0);
}
void MeshNode::computeAndDispatchSkin() {
  if (getMeshSpec()->hasSkin()) {
    if (false) {
      printDataToStdout();
    }
    computeSkinFrame();
  }
}
void MeshNode::update(float delta, std::map<Hash32, std::shared_ptr<Animator>>& pAnimator) {
  SceneNode::update(delta, pAnimator);
}
void MeshNode::computeSkinFrame() {
  Perf::pushPerf();
  Gu::getCoreContext()->chkErrDbg();
  copyJointsToGpu();
  Perf::popPerf();
  //dispatchSkinCompute();
}
void MeshNode::copyJointsToGpu() {
  Perf::pushPerf();
  AssertOrThrow2(_pArmJoints != nullptr);
  std::vector<mat4> mats;
  std::vector<string_t> names; //TEST
  mat4 mtmp;
  // static bool copyLocal = false;
  for (std::shared_ptr<BoneNode> bn : _vecBoneNodesOrdered) {
    //It's all fucked up because of blender's ordering --skin meshes
    //should not be part of the scene graph.  Really, the bones should be part
    //of the scenegraph.
    // Here we're doing
    //  MESH_LOCAL * BONE * (ARMATURE * MODEL)
    mtmp = getSpec()->getBind() * bn->getLocal();
    if (getParent()) {
      mtmp = mtmp * (std::dynamic_pointer_cast<SceneNode>(getParent()))->getLocal();
    }

    mats.push_back(mtmp);
    names.push_back(bn->getBoneSpec()->getName());//TEST
  }
  _pArmJoints->copyDataClientServer(mats.size(), mats.data());
  Perf::popPerf();
}
void MeshNode::getMeshLocalMatrix(mat4& __out_ mat_mesh) {
  //debug
  std::shared_ptr<MeshNode> mg = getThis<MeshNode>();
  if (false) {
    std::cout << std::endl;
    std::cout << std::endl;
    std::cout << std::endl;
    std::cout << std::endl;
    std::cout << std::endl;
    //actual model.
  }

  //1/17/18 Node; removed model_world in order to prevent bullshit like having a unique model matrix

  //Add the mesh-local matrix
 // mat_mesh = mg->getLocal();
  if (mg->getMeshSpec()->hasSkin()) {
    int nnn = 0;
    nnn++;
    mat_mesh = mat4::identity();
  }
  if (mg->getSpec()->getParentType() == ParentType::e::Bone) {
    static int n = 0;
    if (n == 0) {
      mat_mesh = mg->getLocal();// * mat_model_world
    }
    if (n == 1) {
      mat_mesh = mat4::identity();// * mat_model_world;
    }
  }
  else {
    mat_mesh = mg->getLocal();
  }
}
void MeshNode::drawDeferred(RenderParams& rp) {
  //Children.
  SceneNode::drawDeferred(rp);
  draw(rp, false);
}
void MeshNode::drawTransparent(RenderParams& rp) {
  SceneNode::drawTransparent(rp);
  draw(rp, true);
}
void MeshNode::draw(RenderParams& rp, bool bTransparent) {
  //This is surprisingly a huge thing
  mat4 mat_mesh;
  getMeshLocalMatrix(mat_mesh);

  //New "multiple vertex types" system. 1/12/18
  std::shared_ptr<VertexFormat> meshFmt = getMeshSpec()->getVertexFormat();

  if (_pMaterial == nullptr) {
    _pMaterial = Gu::getModelCache()->getDefaultMaterial();
    //showNoMaterialError();
    //meshFmt = v_v3::getVertexFormat();
  }

  //Bind a new shader based on format.
  std::shared_ptr<ShaderBase> pShader;
  if (_pMaterial && _pMaterial->getEnableTransparency() && bTransparent) {
    pShader = Gu::getShaderMaker()->getGlassShader(meshFmt);
  }
  else {
    pShader = Gu::getShaderMaker()->getDiffuseShader(meshFmt);
  }
  if (pShader == nullptr) {
    BRLogWarnCycle("Could not find shader for mesh '" + (getSpec() ? getSpec()->getName() : "") + "'");
  }
  else {
    pShader->bind();
    rp.setShader(pShader);

    //Camera
    std::shared_ptr<CameraNode> bc = rp.getCamera();
    rp.getShader()->setCameraUf(bc, &mat_mesh);

    //Pick ID
    uint32_t pickid = getPickId(); // if not pickable, then PICK_ID_NOT_SET is a valid uniform.
    if (_bPickable && pickid == PICK_ID_NOT_SET) {
      BRLogErrorCycle("Pick ID not set for model '" + name() + "'.");
    }
    rp.getShader()->setUf("_ufPickId", (void*)&pickid);

    //Skin
    bindSkin(rp.getShader());

    //Material
    if (_pMaterial != nullptr) {
      _pMaterial->bind(rp.getShader(), true, true);
    }

    //Mesh
    rp.setMesh(getThis<MeshNode>());

    rp.draw();
  }
}
void MeshNode::bindSkin(std::shared_ptr<ShaderBase> shader) {
  int wc = 0;
  if (getMeshSpec()->hasSkin()) {
    if (false) {
      printDataToStdout();
    }
    shader->bindSsbo(getMeshSpec()->getWeightOffsetsGpu(), "ssInWeightOffsets", 0);
    //    Gu::checkErrorsDbg();
    shader->bindSsbo(getMeshSpec()->getWeightsGpu(), "ssInJointWeights", 1);
    //   Gu::checkErrorsDbg();
    shader->bindSsbo(_pArmJoints, "ssInSkinMatrices", 2);
    wc = 1;//This appears only to be a conditional wc>0 
  }
  shader->setUf("_ufWeightCount", (void*)&wc);
}
void MeshNode::drawForward(RenderParams& rp) {
  SceneNode::drawForward(rp);
  if (Gu::getRenderSettings()->getDebug()->getShowNormals()) {

    //Draw Normals
    std::shared_ptr<MeshNode> mg = getThis<MeshNode>();
    mat4 mat_mesh;
    getMeshLocalMatrix(mat_mesh);
    std::shared_ptr<ShaderBase> pShader = Gu::getShaderMaker()->getNormalsShader_v3n3();
    pShader->bind();
    rp.setShader(pShader);
    std::shared_ptr<CameraNode> bc = rp.getCamera();
    rp.getShader()->setCameraUf(bc, &mat_mesh);
    vec4 vColor(1, 0, 1, 1);
    float fLen = 0.3f;
    vec3 vCamPos = bc->getPos();
    rp.getShader()->setUf("_ufNormalColor", (void*)&vColor, -1, true);
    rp.getShader()->setUf("_ufNormalLength", (void*)&fLen, -1, true);
    rp.getShader()->setUf("_ufCamPos", (void*)&vCamPos, -1, true);

    bindSkin(rp.getShader());

    rp.setMesh(mg);
    rp.draw();
  }
}
void MeshNode::drawShadow(RenderParams& rp) {
  mat4 mModel;
  getMeshLocalMatrix(mModel);
  rp.getShader()->setUf("_ufModel", (void*)&mModel);
  bindSkin(rp.getShader());

  if (_pMaterial == nullptr) {
    _pMaterial = Gu::getModelCache()->getDefaultMaterial();
    // showNoMaterialError();
  }

  _pMaterial->bind(rp.getShader(), true, false);
  rp.getShader()->draw(getThis<MeshNode>());
}
void MeshNode::afterAddedToScene(std::shared_ptr<Scene> scene) {
  //We can't access picker here right now, just set this to "Not Set"
 //Set pick ID after we've been added to the scene.
  std::shared_ptr<Picker> picker = NodeUtils::getPicker(getThis<SceneNode>());
  if (picker) {
    _iPickId = picker->genPickId();
  }
  else {
    BRLogError("Could not set pick ID for " + name() + "  afterAddedToScene SHOULD be called on all child nodes attached to the root added..");
  }

  SceneNode::afterAddedToScene(scene);
}

void MeshNode::calcBoundBox(Box3f& __out_ pBox, const vec3& obPos, float extra_pad) {
  pBox.genResetLimits();
  if (getSpec()) {
    std::shared_ptr<MeshNode> mn = getThis<MeshNode>();
    if (mn->getMeshSpec()->hasSkin()) {
      //Mesh + skin - compute box by the bone boxes.
      for (std::shared_ptr<BoneNode> bn : mn->getBoneNodesOrdered()) {
        pBox.genExpandByBox(bn->getBoundBoxObject());
      }
      //Make the OBB be the same as the box
      //bone node boxes are already transformed to armature space
      getOBB()->calc(mat4::identity(), &pBox);
    }
    else {
      //Simple mesh.  No skin.
      getOBB()->calc(getLocal(), getSpec()->getBoundBoxObject());
      for (int i = 0; i < 8; ++i) {
        pBox.genExpandByPoint(getOBB()->getVerts()[i]);
      }
    }
  }
  SceneNode::calcBoundBox(pBox, obPos, extra_pad);

}
void MeshNode::showNoMaterialError() {
  //Check that textured meshes actually have texture.  If they don't swap the vertex format for something we can draw
  //if (_pMaterial == nullptr){
  //  //  if(getMeshSpec()->getVertexFormat()->getComponentForUserType(VertexUserType::x2_01) != nullptr) {
  //    BroLogError("Mesh '", getSpec()->getName(), "' did not have a material. All meshes must have a material in the",
  //        "new system. Note: if the object is",
  //        "to be rendered colored, make sure to delete all UV Maps in blender in Data -> UV Maps");
  //  //  }
  //}
}



}//ns Game
