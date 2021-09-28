/**
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "NodeData.hpp"

NodeData::NodeData(
    std::string name,
    const Vec3f& translation,
    const Quatf& rotation,
    const Vec3f& scale,
    bool isJoint)
    : Holdable(),
      name(std::move(name)),
      isJoint(isJoint),
      translation(translation),
      rotation(rotation),
      scale(scale),
      children(),
      mesh(-1),
      camera(-1),
      light(-1),
      skin(-1) {}

void NodeData::AddChildNode(uint32_t childIx) {
  children.push_back(childIx);
}

void NodeData::SetMesh(uint32_t meshIx) {
  assert(mesh < 0);
  assert(!isJoint);
  mesh = meshIx;
}

void NodeData::SetSkin(uint32_t skinIx) {
  assert(skin < 0);
  assert(!isJoint);
  skin = skinIx;
}

void NodeData::SetCamera(uint32_t cameraIndex) {
  assert(!isJoint);
  camera = cameraIndex;
}

void NodeData::SetLight(uint32_t lightIndex) {
  assert(!isJoint);
  light = lightIndex;
}

void snapToZero(float &f) {
  const float eps = 1e-10;
  if (std::fabs(f) < eps) f = 0;
}
json NodeData::serialize() const {
  json result = {{"name", name}};

  // if any of the T/R/S have NaN components, just leave them out of the glTF
  auto maybeAdd = [&](std::string key, std::vector<float> vec) -> void {
    if (std::none_of(vec.begin(), vec.end(), [&](float n) { return std::isnan(n); })) {
      result[key] = vec;
    }
  };
  Vec3f trans = translation; 
  snapToZero(trans.x);
  snapToZero(trans.y);
  snapToZero(trans.z);
  if (trans.LengthSquared() > 0) {
    maybeAdd("translation", toStdVec(trans));
  }

  auto quat = rotation;
  auto &q = quat.vector();
  snapToZero(q[0]);
  snapToZero(q[1]);
  snapToZero(q[2]);

  if (q[0] != 0 || q[1] != 0 || q[2] != 0 || quat.scalar() != 1.0f) {
    maybeAdd("rotation", toStdVec(quat));
  }

  if (scale.x != 1.0f || scale.y != 1.0f || scale.z != 1.0) {
    maybeAdd("scale", toStdVec(scale));
  }

  if (!children.empty()) {
    result["children"] = children;
  }
  if (isJoint) {
    // sanity-check joint node
    assert(mesh < 0 && skin < 0);
  } else {
    // non-joint node
    if (mesh >= 0) {
      result["mesh"] = mesh;
    }
    if (!skeletons.empty()) {
      result["skeletons"] = skeletons;
    }
    if (skin >= 0) {
      result["skin"] = skin;
    }
    if (camera >= 0) {
      result["camera"] = camera;
    }
    if (light >= 0) {
      result["extensions"][KHR_LIGHTS_PUNCTUAL]["light"] = light;
    }
  }

  for (const auto& i : userProperties) {
    auto& prop_map = result["extras"]["fromFBX"]["userProperties"];

    json j = json::parse(i);
    for (const auto& k : json::iterator_wrapper(j)) {
      prop_map[k.key()] = k.value();
    }
  }

  return result;
}
