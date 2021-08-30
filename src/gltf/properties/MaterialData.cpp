/**
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "MaterialData.hpp"
#include "TextureData.hpp"

// TODO: retrieve & pass in correct UV set from FBX
std::unique_ptr<Tex> Tex::ref(const TextureData* tex, uint32_t texCoord) {
  return std::unique_ptr<Tex>{(tex != nullptr) ? new Tex(tex->ix, texCoord, tex->translation, tex->rotation, tex->scale) : nullptr};
}

Tex::Tex(uint32_t texRef, uint32_t texCoord, Vec2f translation, float rotation, Vec2f scale) 
: texRef(texRef), texCoord(texCoord), translation(translation), rotation(rotation), scale(scale) {}

void to_json(json& j, const Tex& data) {
  j = json{{"index", data.texRef}, {"texCoord", data.texCoord}};

  if (data.translation != Vec2f(0.0f, 0.0f) || data.rotation != 0.0f || data.scale != Vec2f(1.0f, 1.0f)) {
    json extensions = {};
    extensions[KHR_TEXTURE_TRANSFORM] = {
      {"offset", toStdVec(data.translation)},
      {"rotation", data.rotation},
      {"scale", toStdVec(data.scale)},
    };

    j["extensions"] = extensions;
  }
}

KHRCmnUnlitMaterial::KHRCmnUnlitMaterial() {}

void to_json(json& j, const KHRCmnUnlitMaterial& d) {
  j = json({});
}

inline float clamp(float d, float bottom = 0, float top = 1) {
  return std::max(bottom, std::min(top, d));
}
inline Vec3f
clamp(const Vec3f& vec, const Vec3f& bottom = VEC3F_ZERO, const Vec3f& top = VEC3F_ONE) {
  return Vec3f::Max(bottom, Vec3f::Min(top, vec));
}
inline Vec4f
clamp(const Vec4f& vec, const Vec4f& bottom = VEC4F_ZERO, const Vec4f& top = VEC4F_ONE) {
  return Vec4f::Max(bottom, Vec4f::Min(top, vec));
}

bool isBlend = false;

PBRMetallicRoughness::PBRMetallicRoughness(
    const TextureData* baseColorTexture,
    const TextureData* metRoughTexture,
    const Vec4f& baseColorFactor,
    float metallic,
    float roughness)
    : baseColorTexture(Tex::ref(baseColorTexture)),
      metRoughTexture(Tex::ref(metRoughTexture)),
      baseColorFactor(clamp(baseColorFactor)),
      metallic(clamp(metallic)),
      roughness(clamp(roughness)) {}

void to_json(json& j, const PBRMetallicRoughness& d) {
  j = {};
  if (d.baseColorTexture != nullptr) {
    j["baseColorTexture"] = *d.baseColorTexture;
  }
  if (d.baseColorFactor.LengthSquared() > 0) {
    isBlend = d.baseColorFactor.w == 0.00f;
//    j["baseColorFactor"] = toStdVec(d.baseColorFactor);
    j["baseColorFactor"] = toStdVec(Vec4f(d.baseColorFactor.x, d.baseColorFactor.y, d.baseColorFactor.z, 1.0f));
  }
  if (d.metRoughTexture != nullptr) {
    j["metallicRoughnessTexture"] = *d.metRoughTexture;
    // if a texture is provided, throw away metallic/roughness values
    j["roughnessFactor"] = 1.0f;
    j["metallicFactor"] = 1.0f;
  } else {
    // without a texture, however, use metallic/roughness as constants
    j["metallicFactor"] = d.metallic;
    j["roughnessFactor"] = d.roughness;
  }
}

MaterialData::MaterialData(
    std::string name,
    RawMaterialType materialType,
    const RawShadingModel shadingModel,
    const TextureData* normalTexture,
    const TextureData* occlusionTexture,
    const TextureData* emissiveTexture,
    const Vec3f& emissiveFactor,
    std::shared_ptr<KHRCmnUnlitMaterial> const khrCmnConstantMaterial,
    std::shared_ptr<PBRMetallicRoughness> const pbrMetallicRoughness)
    : Holdable(),
      name(std::move(name)),
      shadingModel(shadingModel),
      materialType(materialType),
      normalTexture(Tex::ref(normalTexture)),
      occlusionTexture(Tex::ref(occlusionTexture)),
      emissiveTexture(Tex::ref(emissiveTexture)),
      emissiveFactor(clamp(emissiveFactor)),
      khrCmnConstantMaterial(khrCmnConstantMaterial),
      pbrMetallicRoughness(pbrMetallicRoughness) {}

json MaterialData::serialize() const {
  json result = {{"name", name},
                 {"extras",
                  {{"fromFBX",
                    {{"shadingModel", Describe(shadingModel)},
                     {"isTruePBR", shadingModel == RAW_SHADING_MODEL_PBR_MET_ROUGH}}}}}};

  switch (materialType) {
  case RAW_MATERIAL_TYPE_OPAQUE: // fallthrough
  case RAW_MATERIAL_TYPE_SKINNED_OPAQUE:
    result["alphaMode"] = "OPAQUE";
    break;
  case RAW_MATERIAL_TYPE_TRANSPARENT: // fallthrough
  case RAW_MATERIAL_TYPE_SKINNED_TRANSPARENT:
    result["alphaMode"] = "BLEND";
    break;
  
  case RAW_MATERIAL_TYPE_TRANSPARENT_MASK: // fallthrough
  case RAW_MATERIAL_TYPE_SKINNED_TRANSPARENT_MASK:
    result["alphaMode"] = "MASK";
    break;
  }

  if (normalTexture != nullptr) {
    result["normalTexture"] = *normalTexture;
  }
  if (occlusionTexture != nullptr) {
    result["occlusionTexture"] = *occlusionTexture;
  }
  if (emissiveTexture != nullptr) {
    result["emissiveTexture"] = *emissiveTexture;
  }
  if (emissiveFactor.LengthSquared() > 0) {
    result["emissiveFactor"] = toStdVec(emissiveFactor);
  }
  if (pbrMetallicRoughness != nullptr) {
    result["pbrMetallicRoughness"] = *pbrMetallicRoughness;
  }
  if (khrCmnConstantMaterial != nullptr) {
    json extensions = {};
    extensions[KHR_MATERIALS_CMN_UNLIT] = *khrCmnConstantMaterial;
    result["extensions"] = extensions;
  }

  // alphaMode was just set above
  if (result["alphaMode"] == "")
      result["alphaMode"] = isBlend ? "BLEND" : "OPAQUE";

  for (const auto& i : userProperties) {
    auto& prop_map = result["extras"]["fromFBX"]["userProperties"];

    json j = json::parse(i);
    for (const auto& k : json::iterator_wrapper(j)) {
      prop_map[k.key()] = k.value();
    }
  }

  return result;
}
