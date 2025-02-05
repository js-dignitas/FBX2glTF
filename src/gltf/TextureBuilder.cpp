/**
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "TextureBuilder.hpp"

#include <stb_image.h>
#include <stb_image_write.h>

#include <utils/File_Utils.hpp>
#include <utils/Image_Utils.hpp>
#include <utils/String_Utils.hpp>

#include <gltf/properties/ImageData.hpp>
#include <gltf/properties/TextureData.hpp>

#ifdef CopyFile
#undef CopyFile
#endif

// keep track of some texture data as we load them
struct TexInfo {
  explicit TexInfo(int rawTexIx) : rawTexIx(rawTexIx) {}

  ~TexInfo() {
    if (pixels) {
      stbi_image_free(pixels);
    }
  }

  const int rawTexIx;
  int width{};
  int height{};
  int channels{};
  uint8_t* pixels = nullptr;
};

std::shared_ptr<TextureData> TextureBuilder::combine(
    const std::vector<int>& ixVec,
    const std::string& tag,
    const pixel_merger& computePixel,
    bool includeAlphaChannel) {
  const std::string key = texIndicesKey(ixVec, tag);
  auto iter = textureByIndicesKey.find(key);
  if (iter != textureByIndicesKey.end()) {
    return iter->second;
  }

  int width = -1, height = -1;
  Vec2f translation(0.0f, 0.0f);
  float rotation = 0.0f;
  Vec2f scale(1.0f, 1.0f);
  std::string mergedFilename = tag;
  std::vector<TexInfo> texes{};
  for (const int rawTexIx : ixVec) {
    TexInfo info(rawTexIx);
    if (rawTexIx >= 0) {
      const RawTexture& rawTex = raw.GetTexture(rawTexIx);
      const std::string& fileLoc = rawTex.fileLocation;
      const std::string& name = FileUtils::GetFileBase(FileUtils::GetFileName(fileLoc));
      if (!fileLoc.empty()) {
        info.pixels = stbi_load(fileLoc.c_str(), &info.width, &info.height, &info.channels, 0);
        if (!info.pixels) {
          fmt::printf("Warning: merge texture [%d](%s) could not be loaded.\n", rawTexIx, name);
        } else {
          if (width < 0) {
            width = info.width;
            height = info.height;
            translation = rawTex.translation;
            rotation = rawTex.rotation;
            scale = rawTex.scale;
          } else if (width != info.width || height != info.height) {
            fmt::printf(
                "Warning: texture %s (%d, %d) can't be merged with previous texture(s) of dimension (%d, %d)\n",
                name,
                info.width,
                info.height,
                width,
                height);
            // this is bad enough that we abort the whole merge
            return nullptr;
          }
          mergedFilename += "_" + name;
        }
      }
    }
    texes.push_back(info);
  }
  // at the moment, the best choice of filename is also the best choice of name
  const std::string mergedName = mergedFilename;

  if (width < 0) {
    // no textures to merge; bail
    return nullptr;
  }
  // TODO: which channel combinations make sense in input files?

  // write 3 or 4 channels depending on whether or not we need transparency
  int channels = includeAlphaChannel ? 4 : 3;

  std::vector<uint8_t> mergedPixels(static_cast<size_t>(channels * width * height));
  for (int xx = 0; xx < width; xx++) {
    for (int yy = 0; yy < height; yy++) {
      std::vector<pixel> pixels(texes.size());
      std::vector<const pixel*> pixelPointers(texes.size(), nullptr);
      for (int jj = 0; jj < texes.size(); jj++) {
        const TexInfo& tex = texes[jj];
        // each texture's structure will depend on its channel count
        int ii = tex.channels * (xx + yy * width);
        int kk = 0;
        if (tex.pixels != nullptr) {
          for (; kk < tex.channels; kk++) {
            pixels[jj][kk] = tex.pixels[ii++] / 255.0f;
          }
        }
        for (; kk < pixels[jj].size(); kk++) {
          pixels[jj][kk] = 1.0f;
        }
        pixelPointers[jj] = &pixels[jj];
      }
      const pixel merged = computePixel(pixelPointers);
      int ii = channels * (xx + yy * width);
      for (int jj = 0; jj < channels; jj++) {
        mergedPixels[ii + jj] = static_cast<uint8_t>(fmax(0, fmin(255.0f, merged[jj] * 255.0f)));
      }
    }
  }

  // write a .png iff we need transparency in the destination texture
  bool png = includeAlphaChannel;

  std::vector<char> imgBuffer;
  int res;
  if (png) {
    res = stbi_write_png_to_func(
        WriteToVectorContext,
        &imgBuffer,
        width,
        height,
        channels,
        mergedPixels.data(),
        width * channels);
  } else {
    res = stbi_write_jpg_to_func(
        WriteToVectorContext, &imgBuffer, width, height, channels, mergedPixels.data(), 80);
  }
  if (!res) {
    fmt::printf("Warning: failed to generate merge texture '%s'.\n", mergedFilename);
    return nullptr;
  }

  ImageData* image;
  if (options.outputBinary && !options.separateTextures) {
    const auto bufferView =
        gltf.AddRawBufferView(*gltf.defaultBuffer, imgBuffer.data(), to_uint32(imgBuffer.size()));
    image = new ImageData(mergedName, *bufferView, png ? "image/png" : "image/jpeg");
  } else {
    const std::string imageFilename = mergedFilename + (png ? ".png" : ".jpg");
    const std::string imagePath = outputFolder + imageFilename;
    FILE* fp = fopen(imagePath.c_str(), "wb");
    if (fp == nullptr) {
      fmt::printf("Warning:: Couldn't write file '%s' for writing.\n", imagePath);
      return nullptr;
    }

    if (fwrite(imgBuffer.data(), imgBuffer.size(), 1, fp) != 1) {
      fmt::printf(
          "Warning: Failed to write %lu bytes to file '%s'.\n", imgBuffer.size(), imagePath);
      fclose(fp);
      return nullptr;
    }
    fclose(fp);
    if (verboseOutput) {
      fmt::printf("Wrote %lu bytes to texture '%s'.\n", imgBuffer.size(), imagePath);
    }
    image = new ImageData(mergedName, imageFilename);
  }
  std::shared_ptr<TextureData> texDat = gltf.textures.hold(
      new TextureData(mergedName, *gltf.defaultSampler, *gltf.images.hold(image), translation, rotation, scale));
  textureByIndicesKey.insert(std::make_pair(key, texDat));
  return texDat;
}

/** Create a new TextureData for the given RawTexture index, or return a previously created one. */
std::shared_ptr<TextureData> TextureBuilder::simple(int rawTexIndex, const std::string& tag) {
  const std::string key = texIndicesKey({rawTexIndex}, tag);
  auto iter = textureByIndicesKey.find(key);
  if (iter != textureByIndicesKey.end()) {
    return iter->second;
  }


  RawTexture rawTexture = raw.GetTexture(rawTexIndex);
  std::string textureName = FileUtils::GetFileBase(rawTexture.name);
  std::string relativeFilename = FileUtils::GetFileName(rawTexture.fileLocation);
  auto suffix = FileUtils::GetFileSuffix(rawTexture.fileLocation);
  bool embeddedTextures = options.outputBinary && !options.separateTextures;
  std::string baseName = FileUtils::GetFileBase(relativeFilename);

  std::string outputPath;
  std::string dstAbs;

  //TGAs are not supported by GLTF spec, convert to PNG
  if(suffix.has_value() && suffix->compare("tga")==0) {
    std::string tmpFolder;
    if(outputFolder.empty()) 
      tmpFolder = "./convertedTextures";
    else {
      tmpFolder = outputFolder+"/convertedTextures";
    }

    bool precise = rawTexture.usage == RAW_TEXTURE_USAGE_NORMAL ||
        rawTexture.usage == RAW_TEXTURE_USAGE_SHININESS ||
        rawTexture.usage == RAW_TEXTURE_USAGE_AO_MET_ROUGH ||
        rawTexture.usage == RAW_TEXTURE_USAGE_MODULATION;

    bool transparent = rawTexture.occlusion != RAW_TEXTURE_OCCLUSION_OPAQUE;
    std::string ext = transparent || precise ? ".png" : ".jpg";
    std::string tmpPath = tmpFolder + "/" + baseName + ext;
    relativeFilename = FileUtils::GetFileName(tmpPath);
    outputPath = outputFolder + "/" + relativeFilename;
    dstAbs = FileUtils::GetAbsolutePath(outputPath);

    if (embeddedTextures || !FileUtils::FileExists(dstAbs)) {
      // Check for existence of ImageMagick convert binary in path
      if (std::system("magick -version") !=
          0) { // GAM TODO - Pipe to /dev/null or NUL to avoid console spew (also should check the
               // output to make sure its really ImageMagick)
        fmt::printf(
            "Warning: Failed to find installed version of ImageMagick 'magick'. TGA conversion requires ImageMagick (https://imagemagick.org/) installation in system path)\n");
        // If we can't find convert set the file path to empty string, so next step will fail and
        // set to error texture
        rawTexture.fileLocation = "";
        rawTexture.name = "";
      } else {
        // Ensure output folder exists
        FileUtils::MakeDir(tmpFolder);

        std::string cmdStr = "magick \"" + rawTexture.fileLocation +
            "\" -flip -resize \"1024>\" \"" + tmpPath +
            "\""; // GAM TODO - Pipe to /dev/null or NUL to avoid console spew

        auto res = std::system(cmdStr.c_str());
        if (res == 0) {
          rawTexture.fileLocation = tmpPath;
          rawTexture.name = baseName + ext;
          if (verboseOutput) {
            fmt::printf(
                "Converted TGA texture '%s' to output folder: %s\n",
                rawTexture.fileLocation,
                tmpPath);
          }
        } else {
          // If we fail, set the file path to empty string, so next step will fail and set to error
          // texture
          fmt::printf("Warning: Failed to magick TGA:%s to %s\n", rawTexture.fileLocation, tmpPath);
          rawTexture.fileLocation = "";
          rawTexture.name = "";
        }
      }
    } else if (!embeddedTextures) {
        // Use the target texture png/jpg path
      rawTexture.fileLocation = tmpPath;
      rawTexture.name = baseName + ext;
    }

    //Update the texture details
    textureName = FileUtils::GetFileBase(rawTexture.name);
    relativeFilename = FileUtils::GetFileName(rawTexture.fileLocation);
    suffix = FileUtils::GetFileSuffix(rawTexture.fileLocation);
  }

  ImageData* image = nullptr;

  if (embeddedTextures) {
    auto bufferView = gltf.AddBufferViewForFile(*gltf.defaultBuffer, rawTexture.fileLocation);
    if (bufferView) {
      std::string mimeType;
      if (suffix) {
        mimeType = ImageUtils::suffixToMimeType(suffix.value());
      } else {
        mimeType = "image/jpeg";
        fmt::printf(
            "Warning: Can't deduce mime type of texture '%s'; using %s.\n",
            rawTexture.fileLocation,
            mimeType);
      }
      image = new ImageData(relativeFilename, *bufferView, mimeType);
    }

  } else if (!relativeFilename.empty()) {
    image = new ImageData(relativeFilename, relativeFilename);
    auto srcAbs = FileUtils::GetAbsolutePath(rawTexture.fileLocation);
    if (!FileUtils::FileExists(outputPath) && srcAbs != dstAbs) {
        if (FileUtils::CopyFile(rawTexture.fileLocation, outputPath, true)) {
          if (verboseOutput) {
            fmt::printf("Copied texture '%s' to output folder: %s\n", textureName, outputPath);
          }
        } else {
          // no point commenting further on read/write error; CopyFile() does enough of that, and we
          // certainly want to to add an image struct to the glTF JSON, with the correct relative
          // path reference, even if the copy failed.
        }
    }
  }
  

  if (!image) {
    // fallback is tiny transparent PNG
//    image = new ImageData(textureName, "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8/5+hHgAHggJ/PchI7wAAAABJRU5ErkJggg==");
    return nullptr;
  }

  std::shared_ptr<TextureData> texDat = gltf.textures.hold(
      new TextureData(textureName, *gltf.defaultSampler, *gltf.images.hold(image), rawTexture.translation, rawTexture.rotation, rawTexture.scale));
  textureByIndicesKey.insert(std::make_pair(key, texDat));
  return texDat;
}
