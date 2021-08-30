/**
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "Image_Utils.hpp"

#include <algorithm>
#include <string>

#define STB_IMAGE_IMPLEMENTATION

#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION

#include <stb_image_write.h>

namespace ImageUtils {

static ImageOcclusion imageOcclusion(FILE* f) {
  int width, height, channels;
  // RGBA: we have to load the pixels to figure out if the image is fully opaque
  uint8_t* pixels = stbi_load_from_file(f, &width, &height, &channels, 0);
  bool hasMaskTransparency = false;
  if (pixels != nullptr) {
    int pixelCount = width * height;
    for (int ix = 0; ix < pixelCount; ix++) {
      uint8_t alpha = pixels[4 * ix + 3];
      if (alpha < 255 && alpha > 0) {
        stbi_image_free(pixels);
        return IMAGE_TRANSPARENT;
      }

      if (alpha == 0) {
        hasMaskTransparency = true;
      }
    }
  }

  stbi_image_free(pixels);
  return hasMaskTransparency ? IMAGE_TRANSPARENT_MASK : IMAGE_OPAQUE;
}

ImageProperties GetImageProperties(char const* filePath) {
  ImageProperties result = {
      1,
      1,
      IMAGE_OPAQUE,
  };

  FILE* f = fopen(filePath, "rb");
  if (f == nullptr) {
    return result;
  }

  int channels;
  int success = stbi_info_from_file(f, &result.width, &result.height, &channels);

  if (success && channels == 4) {
    result.occlusion = imageOcclusion(f);
  }
  return result;
}

std::string suffixToMimeType(std::string suffix) {
  std::transform(suffix.begin(), suffix.end(), suffix.begin(), ::tolower);

  if (suffix == "jpg" || suffix == "jpeg") {
    return "image/jpeg";
  }
  if (suffix == "png") {
    return "image/png";
  }
  return "image/unknown";
}

} // namespace ImageUtils
