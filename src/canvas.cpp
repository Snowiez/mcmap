/**
 * This file contains functions to draw blocks to a png file
 */

#include "./canvas.h"

// End tag to use when in need of an irrelevant NBT value, pre-initialised for
// performance
NBT empty;

//  ____                       _
// / ___|_ __ ___  _ __  _ __ (_)_ __   __ _
//| |   | '__/ _ \| '_ \| '_ \| | '_ \ / _` |
//| |___| | | (_) | |_) | |_) | | | | | (_| |
// \____|_|  \___/| .__/| .__/|_|_| |_|\__, |
//                |_|   |_|            |___/
// The following methods are related to the cropping mechanism.

size_t IsometricCanvas::getCroppedWidth() const {
  // Not implemented, returns the actual width. Might come back to this but it
  // is not as interesting as the height.
  return width;
}

size_t IsometricCanvas::firstLine() const {
  // Tip: Return -7 for a freaky glichy look
  // return -7;

  // We search for the first non-empty line
  size_t line = 0;

  for (size_t row = 0; row < height && !line; row++)
    for (size_t pixel = 0; pixel < width && !line; pixel++)
      if (*(bytesBuffer + (pixel + row * width) * BYTESPERPIXEL))
        line = row;

  return line - (padding - 2);
}

size_t IsometricCanvas::lastLine() const {
  // We search for the last non-empty line
  size_t line = 0;

  for (size_t row = height - 1; row > 0 && !line; row--)
    for (size_t pixel = 0; pixel < width && !line; pixel++)
      if (*(bytesBuffer + (pixel + row * width) * BYTESPERPIXEL))
        line = row;

  return line + (padding - 2);
}

size_t IsometricCanvas::getCroppedHeight() const {
  size_t croppedHeight = lastLine() - firstLine();
  if (croppedHeight == (padding - 2) * 2)
    return 0;
  else
    return croppedHeight + 1;
}

size_t IsometricCanvas::getCroppedOffset() const {
  // The first line to render in the cropped view of the canvas, as an offset
  // from the beginning of the byte buffer
  return firstLine() * width * BYTESPERPIXEL;
}

// ____                     _
//|  _ \ _ __ __ ___      _(_)_ __   __ _
//| | | | '__/ _` \ \ /\ / / | '_ \ / _` |
//| |_| | | | (_| |\ V  V /| | | | | (_| |
//|____/|_|  \__,_| \_/\_/ |_|_| |_|\__, |
//                                  |___/
// The following methods are used to draw the map into the canvas' 2D buffer

// Translate a chunk in the canvas to a chunk in the world
void IsometricCanvas::translate(size_t xCanvasPos, size_t zCanvasPos,
                                int64_t *xPos, int64_t *zPos) {
  switch (map.orientation) {
  case NW:
    *xPos = (map.minX >> 4) + xCanvasPos;
    *zPos = (map.minZ >> 4) + zCanvasPos;
    break;
  case SW:
    std::swap(xCanvasPos, zCanvasPos);
    *xPos = (map.minX >> 4) + xCanvasPos;
    *zPos = (map.maxZ >> 4) - zCanvasPos;
    break;
  case NE:
    std::swap(xCanvasPos, zCanvasPos);
    *xPos = (map.maxX >> 4) - xCanvasPos;
    *zPos = (map.minZ >> 4) + zCanvasPos;
    break;
  case SE:
    *xPos = (map.maxX >> 4) - xCanvasPos;
    *zPos = (map.maxZ >> 4) - zCanvasPos;
    break;
  }
}

void IsometricCanvas::drawTerrain(const Terrain::Data &world) {
  for (size_t xCanvasPos = 0; xCanvasPos < nXChunks; xCanvasPos++) {
    for (size_t zCanvasPos = 0; zCanvasPos < nZChunks; zCanvasPos++) {
      drawChunk(world, xCanvasPos, zCanvasPos);
      logger::printProgress("Rendering chunks",
                            xCanvasPos * nZChunks + zCanvasPos,
                            nZChunks * nXChunks);
    }
  }

  return;
}

void IsometricCanvas::drawChunk(const Terrain::Data &terrain,
                                const int64_t xPos, const int64_t zPos) {
  int64_t worldChunkX = 0, worldChunkZ = 0;
  translate(xPos, zPos, &worldChunkX, &worldChunkZ);

  const NBT &chunk = terrain.chunkAt(worldChunkX, worldChunkZ);
  const uint8_t height = terrain.heightAt(worldChunkX, worldChunkZ);

  if (chunk.is_end()                          // Catch uninitialized chunks
      || !chunk.contains("DataVersion")       // Dataversion is required
      || !chunk.contains("Level")             // Level data is required
      || !chunk["Level"].contains("Sections") // No sections mean no blocks
  )
    return;

  // This value is primordial: it states which version of minecraft the chunk
  // was created under, and we use it to know which interpreter to use later in
  // the sections
  const int dataVersion = *chunk["DataVersion"].get<const int *>();

  // Set the interpreter according to the type of chunk encountered
  sectionInterpreter interpreter = NULL;
  if (dataVersion < 2534)
    interpreter = blockAtPre116;
  else
    interpreter = blockAtPost116;

  // Reset the beacons
  numBeacons = 0;

  // Setup the markers
  localMarkers = 0;
  for (uint8_t i = 0; i < totalMarkers; i++) {
    if (CHUNK((*markers)[i].x) == worldChunkX &&
        CHUNK((*markers)[i].z) == worldChunkZ) {
      chunkMarkers[localMarkers++] =
          (i << 8) + (((*markers)[i].x & 0x0f) << 4) + ((*markers)[i].z & 0x0f);
    }
  }

  const uint8_t minSection = std::max((map.minY >> 4), (height & 0x0f));
  const uint8_t maxSection = std::min((map.maxY >> 4) + 1, (height >> 4));

  for (uint8_t yPos = minSection; yPos < maxSection; yPos++) {
    drawSection(chunk["Level"]["Sections"][yPos], xPos, zPos, yPos,
                interpreter);
  }

  if (numBeacons || localMarkers)
    for (uint8_t yPos = maxSection; yPos < 13; yPos++)
      drawBeams(xPos, zPos, yPos);
}

// Section version of translate above
inline void orient(uint8_t &x, uint8_t &z, Orientation o) {
  switch (o) {
  case NW:
    return;
  case NE:
    std::swap(x, z);
    x = 15 - x;
    return;
  case SW:
    std::swap(x, z);
    z = 15 - z;
    return;
  case SE:
    x = 15 - x;
    z = 15 - z;
    return;
  }
}

void IsometricCanvas::drawBeams(const int64_t xPos, const int64_t zPos,
                                const uint8_t yPos) {
  // Draw beacon beams in an empty section
  uint8_t x, z, index;

  for (uint8_t beam = 0; beam < numBeacons; beam++) {
    x = beacons[beam] >> 4;
    z = beacons[beam] & 0x0f;

    for (uint8_t y = 0; y < 16; y++)
      drawBlock(&beaconBeam, (xPos << 4) + x, (zPos << 4) + z, (yPos << 4) + y,
                empty);
  }

  for (uint8_t marker = 0; marker < localMarkers; marker++) {
    x = (chunkMarkers[marker] >> 4) & 0x0f;
    z = chunkMarkers[marker] & 0x0f;
    index = chunkMarkers[marker] >> 8;

    for (uint8_t y = 0; y < 16; y++)
      drawBlock(&(*markers)[index].color, (xPos << 4) + x, (zPos << 4) + z,
                (yPos << 4) + y, empty);
  }
}

struct section {
  Colors::Block *colors[4096];
  const NBT *data[4096];
};

Colors::Block *next = NULL;

void IsometricCanvas::drawSection(const NBT &section, const int64_t xPos,
                                  const int64_t zPos, const uint8_t yPos,
                                  sectionInterpreter interpreter) {
  // TODO Take care of this case in the chunk drawing
  if (!interpreter) {
    logger::error("Invalid section interpreter\n");
    return;
  }

  // Return if the section is undrawable
  if (section.is_end() || !section.contains("Palette"))
    return;

  uint8_t markerIndex = 0;
  bool beaconBeamColumn = false, markerColumn = false;
  uint16_t colorIndex = 0, index = 0, beaconIndex = 4095;
  int64_t worldChunkX = 0, worldChunkZ = 0;
  Colors::Block *cache[256],
      fallback; // <- empty color to use in case no color is defined

  // Pre-fetch the vectors from the section: the block palette
  const std::vector<NBT> *sectionPalette =
      section["Palette"].get<const std::vector<NBT> *>();
  // The raw block indexes
  const std::vector<int64_t> *blockStates =
      section["BlockStates"].get<const std::vector<int64_t> *>();

  // This will be used by the section interpreter later
  const uint64_t blockBitLength =
      std::max(uint64_t(ceil(log2(sectionPalette->size()))), 4ul);

  // We need the real position of the section for bounds checking
  translate(xPos, zPos, &worldChunkX, &worldChunkZ);

  // Preload the colors in the order they appear in the palette into an array
  // for cheaper access
  for (auto &color : *sectionPalette) {
    const string namespacedId = *color["Name"].get<const string *>();
    auto defined = palette.find(namespacedId);

    if (defined == palette.end()) {
      logger::error("Color of block {} not found\n", namespacedId);
      cache[colorIndex++] = &fallback;
    } else {
      cache[colorIndex++] = &defined->second;
      if (namespacedId == "minecraft:beacon")
        beaconIndex = colorIndex - 1;
    }
  }

  struct section s;

  for (int i = 0; i < 4096; i++) {
    index = interpreter(blockBitLength, blockStates, i);
    s.colors[i] = cache[index];
    s.data[i] = &sectionPalette->operator[](index);
  }

  // Main drawing loop, for every block of the section
  for (uint8_t x = 0; x < 16; x++) {
    for (uint8_t z = 0; z < 16; z++) {
      // Orient the indexes for them to correspond to the orientation
      uint8_t xReal = x, zReal = z;
      orient(xReal, zReal, map.orientation);

      // If we are oob, skip the line
      if ((worldChunkX << 4) + xReal > map.maxX ||
          (worldChunkX << 4) + xReal < map.minX ||
          (worldChunkZ << 4) + zReal > map.maxZ ||
          (worldChunkZ << 4) + zReal < map.minZ)
        continue;

      for (uint8_t i = 0; i < numBeacons; i++)
        if (beacons[i] == (x << 4) + z)
          beaconBeamColumn = true;

      for (uint8_t i = 0; i < localMarkers; i++)
        if ((chunkMarkers[i] & 0xff) == (x << 4) + z) {
          markerColumn = true;
          markerIndex = chunkMarkers[i] >> 8;
        }

      for (uint8_t y = 0; y < 16; y++) {
        // Render the beams, even if we are oob
        if (beaconBeamColumn)
          drawBlock(&beaconBeam, (xPos << 4) + x, (zPos << 4) + z,
                    (yPos << 4) + y, empty);

        if (markerColumn)
          drawBlock(&(*markers)[markerIndex].color, (xPos << 4) + x,
                    (zPos << 4) + z, (yPos << 4) + y, empty);

        // Check that we do not step over the height limit
        if ((yPos << 4) + y < map.minY || (yPos << 4) + y > map.maxY)
          continue;

        next = (y != 15 ? s.colors[x + 16 * z + 256 * (y + 1)] : NULL);

        drawBlock(s.colors[x + 16 * z + 256 * y], (xPos << 4) + x,
                  (zPos << 4) + z, (yPos << 4) + y,
                  *s.data[x + 16 * z + 256 * y]);

        // A beam can begin at every moment in a section
        if (index == beaconIndex) {
          beacons[numBeacons++] = (x << 4) + z;
          beaconBeamColumn = true;
        }
      }

      markerColumn = beaconBeamColumn = false;
      markerIndex = 0;
    }
  }

  return;
}

// ____  _            _
//| __ )| | ___   ___| | _____
//|  _ \| |/ _ \ / __| |/ / __|
//| |_) | | (_) | (__|   <\__ \.
//|____/|_|\___/ \___|_|\_\___/
//
// Functions to render individual types of blocks

IsometricCanvas::drawer blockRenderers[] = {
    &IsometricCanvas::drawFull,
#define DEFINETYPE(STRING, CALLBACK) &IsometricCanvas::CALLBACK,
#include "./blocktypes.def"
#undef DEFINETYPE
};

inline void IsometricCanvas::drawBlock(Colors::Block *color, const size_t x,
                                       const size_t z, const size_t y,
                                       const NBT &metadata) {
  const size_t bmpPosX = 2 * (sizeZ - 1) + (x - z) * 2 + padding;
  const size_t bmpPosY = height - 2 + x + z - sizeX - sizeZ -
                         (y - map.minY) * heightOffset - padding;

  if (bmpPosX > width - 1)
    throw std::range_error("Invalid x: " + std::to_string(bmpPosX) + "/" +
                           std::to_string(width));

  if (bmpPosY > height - 1)
    throw std::range_error("Invalid y: " + std::to_string(bmpPosY) + "/" +
                           std::to_string(height));

  if (color->primary.empty())
    return;

  // Pointer to the color to use, and local color copy if changes are due
  Colors::Block localColor, *colorPtr = color;
  if (shading) {
    // Make a local copy of the color
    localColor = *colorPtr;

    // Get the target shading from the profile
    float fsub = brightnessLookup[y];
    int sub = int(fsub * (float(color->primary.brightness()) / 323.0f + .21f));

    // Modify the colors
    localColor.primary.modColor(sub);
    localColor.dark.modColor(sub);
    localColor.light.modColor(sub);
    localColor.secondary.modColor(sub);

    // Make sure the local color is used later
    colorPtr = &localColor;
  }

  // Then call the function registered with the block's type
  (this->*blockRenderers[color->type])(bmpPosX, bmpPosY, metadata, colorPtr);
}

inline void blend(uint8_t *const destination, const uint8_t *const source) {
  if (!source[PALPHA])
    return;

  if (destination[PALPHA] == 0 || source[PALPHA] == 255) {
    memcpy(destination, source, BYTESPERPIXEL);
    return;
  }
#define BLEND(ca, aa, cb)                                                      \
  uint8_t(((size_t(ca) * size_t(aa)) + (size_t(255 - aa) * size_t(cb))) / 255)
  destination[0] = BLEND(source[0], source[PALPHA], destination[0]);
  destination[1] = BLEND(source[1], source[PALPHA], destination[1]);
  destination[2] = BLEND(source[2], source[PALPHA], destination[2]);
  destination[PALPHA] +=
      (size_t(source[PALPHA]) * size_t(255 - destination[PALPHA])) / 255;
#undef BLEND
}

inline void addColor(uint8_t *const color, const uint8_t *const add) {
  const float v2 = (float(add[PALPHA]) / 255.0f);
  const float v1 = (1.0f - (v2 * .2f));
  color[0] = clamp(uint16_t(float(color[0]) * v1 + float(add[0]) * v2));
  color[1] = clamp(uint16_t(float(color[1]) * v1 + float(add[1]) * v2));
  color[2] = clamp(uint16_t(float(color[2]) * v1 + float(add[2]) * v2));
}

void IsometricCanvas::drawHead(const size_t x, const size_t y, const NBT &,
                               const Colors::Block *block) {
  /* Small block centered
   * |    |
   * |    |
   * | PP |
   * | DL | */
  uint8_t *pos = pixel(x + 1, y + 2);
  memcpy(pos, &block->primary, BYTESPERPIXEL);
  memcpy(pos + CHANSPERPIXEL, &block->primary, BYTESPERPIXEL);

  pos = pixel(x + 1, y + 3);
  memcpy(pos, &block->dark, BYTESPERPIXEL);
  memcpy(pos + CHANSPERPIXEL, &block->light, BYTESPERPIXEL);
}

void IsometricCanvas::drawThin(const size_t x, const size_t y, const NBT &,
                               const Colors::Block *block) {
  /* Overwrite the block below's top layer
   * |    |
   * |    |
   * |    |
   * |XXXX|
   *   XX   */
  uint8_t *pos = pixel(x, y + 3);
  for (size_t i = 0; i < 4; ++i, pos += CHANSPERPIXEL) {
    memcpy(pos, &block->primary, BYTESPERPIXEL);
  }
  pos = pixel(x + 1, y + 4);
  memcpy(pos, &block->dark, BYTESPERPIXEL);
  memcpy(pos + CHANSPERPIXEL, &block->light, BYTESPERPIXEL);
}

void IsometricCanvas::drawHidden(const size_t, const size_t, const NBT &,
                                 const Colors::Block *) {
  return;
}

void IsometricCanvas::drawTransparent(const size_t x, const size_t y,
                                      const NBT &, const Colors::Block *block) {
  // Avoid the top and dark/light edges for a clearer look through
  uint8_t *pos = pixel(x, y + 1);
  for (uint8_t j = 1; j < 4; j++, pos = pixel(x, y + j)) {
    for (uint8_t i = 0; i < 4; i++)
      blend(pos + i * CHANSPERPIXEL,
            (uint8_t *)(i < 2 ? &block->light : &block->dark));
  }

  if (next && next->primary != block->primary) {
    pos = pixel(x, y);
    for (uint8_t i = 0; i < 4; i++)
      blend(pos + i * CHANSPERPIXEL, (uint8_t *)&block->primary);
  }
}

void IsometricCanvas::drawTorch(const size_t x, const size_t y, const NBT &,
                                const Colors::Block *block) {
  /* TODO Callback to handle the orientation
   * Print the secondary on top of two primary
   * |    |
   * |  S |
   * |  P |
   * |  P | */
  memcpy(pixel(x + 2, y + 1), &block->secondary, BYTESPERPIXEL);
  memcpy(pixel(x + 2, y + 2), &block->primary, BYTESPERPIXEL);
  memcpy(pixel(x + 2, y + 3), &block->primary, BYTESPERPIXEL);
}

void IsometricCanvas::drawPlant(const size_t x, const size_t y, const NBT &,
                                const Colors::Block *block) {
  /* Print a plant-like block
   * TODO Make that nicer ?
   * |    |
   * | X X|
   * |  X |
   * | X  | */
  uint8_t *pos = pixel(x, y + 1);
  memcpy(pos + (CHANSPERPIXEL), &block->primary, BYTESPERPIXEL);
  memcpy(pos + (CHANSPERPIXEL * 3), &block->primary, BYTESPERPIXEL);

  memcpy(pixel(x + 2, y + 2), &block->primary, BYTESPERPIXEL);
  memcpy(pixel(x + 1, y + 3), &block->primary, BYTESPERPIXEL);
}

void IsometricCanvas::drawUnderwaterPlant(const size_t x, const size_t y,
                                          const NBT &,
                                          const Colors::Block *block) {
  /* Print a plant-like block
   * |    |
   * |WXWX|
   * |WWXW|
   * |WXWW| */

  drawPlant(x, y, empty, block);
  drawTransparent(x, y, empty, &water);
}

void IsometricCanvas::drawFire(const size_t x, const size_t y, const NBT &,
                               const Colors::Block *const color) {
  // This basically just leaves out a few pixels
  // Top row
  uint8_t *pos = pixel(x, y);
  blend(pos, (uint8_t *)&color->light);
  blend(pos + CHANSPERPIXEL * 2, (uint8_t *)&color->dark);
  // Second and third row
  for (size_t i = 1; i < 3; ++i) {
    pos = pixel(x, y + i);
    blend(pos, (uint8_t *)&color->dark);
    blend(pos + (CHANSPERPIXEL * i), (uint8_t *)&color->primary);
    blend(pos + (CHANSPERPIXEL * 3), (uint8_t *)&color->light);
  }
  // Last row
  pos = pixel(x, y + 3);
  blend(pos + (CHANSPERPIXEL * 2), (uint8_t *)&color->light);
}

void IsometricCanvas::drawOre(const size_t x, const size_t y, const NBT &,
                              const Colors::Block *color) {
  /* Print a vein with the secondary in the block
   * |PSPP|
   * |DDSL|
   * |DSLS|
   * |SDLL| */

  int sub = (float(color->primary.BRIGHTNESS) / 323.0f + .21f);

  Colors::Color secondaryLight(color->secondary);
  Colors::Color secondaryDark(color->secondary);
  secondaryLight.modColor(sub - 15);
  secondaryDark.modColor(sub - 25);

  const Colors::Color *sprite[4][4] = {
      {&color->primary, &color->secondary, &color->primary, &color->primary},
      {&color->dark, &color->dark, &secondaryLight, &color->light},
      {&color->dark, &secondaryDark, &color->light, &secondaryLight},
      {&secondaryDark, &color->dark, &color->light, &color->light}};

  uint8_t *pos = pixel(x, y);
  for (size_t j = 0; j < 4; ++j, pos = pixel(x, y + j))
    for (size_t i = 0; i < 4; ++i, pos += CHANSPERPIXEL)
      memcpy(pos, sprite[j][i], BYTESPERPIXEL);
}

void IsometricCanvas::drawGrown(const size_t x, const size_t y, const NBT &,
                                const Colors::Block *color) {
  /* Print the secondary color on top
   * |SSSS|
   * |DSSL|
   * |DDLL|
   * |DDLL| */

  // First determine how much the color has to be lightened up or darkened
  // The brighter the color, the stronger the impact
  int sub = (float(color->primary.BRIGHTNESS) / 323.0f + .21f);

  Colors::Color secondaryLight(color->secondary);
  Colors::Color secondaryDark(color->secondary);
  secondaryLight.modColor(sub - 15);
  secondaryDark.modColor(sub - 25);

  const Colors::Color *sprite[4][4] = {
      {&color->secondary, &color->secondary, &color->secondary,
       &color->secondary},
      {&color->dark, &secondaryDark, &secondaryLight, &color->light},
      {&color->dark, &color->dark, &color->light, &color->light},
      {&color->dark, &color->dark, &color->light, &color->light}};

  uint8_t *pos = pixel(x, y);
  for (size_t j = 0; j < 4; ++j, pos = pixel(x, y + j))
    for (size_t i = 0; i < 4; ++i, pos += CHANSPERPIXEL)
      memcpy(pos, sprite[j][i], BYTESPERPIXEL);
}

void IsometricCanvas::drawRod(const size_t x, const size_t y, const NBT &,
                              const Colors::Block *const color) {
  /* A full fat rod
   * | PP |
   * | DL |
   * | DL |
   * | DL | */
  uint8_t *pos = pixel(x + 1, y);
  memcpy(pos, &color->primary, BYTESPERPIXEL);
  memcpy(pos + CHANSPERPIXEL, &color->primary, BYTESPERPIXEL);

  pos = pixel(x + 1, y + 1);
  for (int i = 1; i < 4; i++, pos = pixel(x + 1, y + i)) {
    memcpy(pos, &color->dark, BYTESPERPIXEL);
    memcpy(pos + CHANSPERPIXEL, &color->light, BYTESPERPIXEL);
  }
}

void IsometricCanvas::drawBeam(const size_t x, const size_t y, const NBT &,
                               const Colors::Block *const color) {
  /* No top to make it look more continuous
   * |    |
   * | DL |
   * | DL |
   * | DL | */
  uint8_t *pos = pixel(x + 1, y + 1);
  for (int i = 1; i < 4; i++, pos = pixel(x + 1, y + i)) {
    blend(pos, (uint8_t *)&color->dark);
    blend(pos + CHANSPERPIXEL, (uint8_t *)&color->light);
  }
}

void IsometricCanvas::drawSlab(const size_t x, const size_t y,
                               const NBT &metadata,
                               const Colors::Block *color) {
  /* This one has a hack to make it look like a gradual step up:
   * The second layer has primary colors to make the height difference
   * less obvious.
   * |    |    |PPPP|
   * |PPPP| or |DDLL| or full
   * |DPPL|    |DDLL|
   * |DDLL|    |    | */

  bool top = false;
  string type;

  const Colors::Color *spriteTop[3][4] = {
      {&color->primary, &color->primary, &color->primary, &color->primary},
      {&color->dark, &color->dark, &color->light, &color->light},
      {&color->dark, &color->dark, &color->light, &color->light}};

  const Colors::Color *spriteBottom[3][4] = {
      {&color->primary, &color->primary, &color->primary, &color->primary},
      {&color->dark, &color->primary, &color->primary, &color->light},
      {&color->dark, &color->dark, &color->light, &color->light}};

  const Colors::Color *(*target)[3][4] = &spriteBottom;

  if (metadata.contains("Properties") &&
      metadata["Properties"].contains("type")) {
    type = *metadata["Properties"]["type"].get<const string *>();

    // Draw a full block if it is a double slab
    if (type == "double") {
      drawFull(x, y, metadata, color);
      return;
    }

    if (type == "top") {
      top = true;
      target = &spriteTop;
    }
  }

  uint8_t *pos = pixel(x, y + (top ? 0 : 1));
  for (size_t j = 0; j < 3; ++j, pos = pixel(x, y + j + (top ? 0 : 1)))
    for (size_t i = 0; i < 4; ++i, pos += CHANSPERPIXEL)
      memcpy(pos, (*target)[j][i], BYTESPERPIXEL);
}

void IsometricCanvas::drawWire(const size_t x, const size_t y, const NBT &,
                               const Colors::Block *color) {
  uint8_t *pos = pixel(x + 1, y + 3);
  memcpy(pos, &color->primary, BYTESPERPIXEL);
  memcpy(pos + CHANSPERPIXEL, &color->primary, BYTESPERPIXEL);
}

/*
        void setUpStep(const size_t x, const size_t y, const uint8_t *
   const color, const uint8_t * const light, const uint8_t * const dark) {
   uint8_t *pos = pixel(x, y); for (size_t i = 0; i < 4; ++i, pos +=
   CHANSPERPIXEL) { memcpy(pos, color, BYTESPERPIXEL);
                }
                pos = pixel(x, y+1);
                for (size_t i = 0; i < 4; ++i, pos += CHANSPERPIXEL) {
                        memcpy(pos, color, BYTESPERPIXEL);
                }
        }
*/

void IsometricCanvas::drawLog(const size_t x, const size_t y,
                              const NBT &metadata, const Colors::Block *color) {

  string axis = "y";
  int sub = (float(color->primary.BRIGHTNESS) / 323.0f + .21f);

  Colors::Color secondaryLight(color->secondary);
  Colors::Color secondaryDark(color->secondary);
  secondaryLight.modColor(sub - 15);
  secondaryDark.modColor(sub - 25);

  const Colors::Color *spriteY[4][4] = {
      {&color->secondary, &color->secondary, &color->secondary,
       &color->secondary},
      {&color->dark, &color->dark, &color->light, &color->light},
      {&color->dark, &color->dark, &color->light, &color->light},
      {&color->dark, &color->dark, &color->light, &color->light}};

  const Colors::Color *spriteX[4][4] = {
      {&color->primary, &color->primary, &color->primary, &color->primary},
      {&secondaryDark, &secondaryDark, &color->light, &color->light},
      {&secondaryDark, &secondaryDark, &color->light, &color->light},
      {&secondaryDark, &secondaryDark, &color->light, &color->light}};

  const Colors::Color *spriteZ[4][4] = {
      {&color->primary, &color->primary, &color->primary, &color->primary},
      {&color->dark, &color->dark, &secondaryLight, &secondaryLight},
      {&color->dark, &color->dark, &secondaryLight, &secondaryLight},
      {&color->dark, &color->dark, &secondaryLight, &secondaryLight}};

  const Colors::Color *(*target)[4][4] = &spriteY;

  if (metadata.contains("Properties") &&
      metadata["Properties"].contains("axis")) {
    axis = *metadata["Properties"]["axis"].get<const string *>();

    if (axis == "x") {
      if (map.orientation == NW || map.orientation == SE)
        target = &spriteZ;
      else
        target = &spriteX;
    } else if (axis == "z") {
      if (map.orientation == NW || map.orientation == SE)
        target = &spriteX;
      else
        target = &spriteZ;
    }
  }

  uint8_t *pos = pixel(x, y);
  for (size_t j = 0; j < 4; ++j, pos = pixel(x, y + j))
    for (size_t i = 0; i < 4; ++i, pos += CHANSPERPIXEL)
      memcpy(pos, (*target)[j][i], BYTESPERPIXEL);
}

void IsometricCanvas::drawFull(const size_t x, const size_t y, const NBT &,
                               const Colors::Block *color) {
  // Sets pixels around x,y where A is the anchor
  // T = given color, D = darker, L = lighter
  // A T T T
  // D D L L
  // D D L L
  // D D L L

  const Colors::Color *sprite[4][4] = {
      {&color->primary, &color->primary, &color->primary, &color->primary},
      {&color->dark, &color->dark, &color->light, &color->light},
      {&color->dark, &color->dark, &color->light, &color->light},
      {&color->dark, &color->dark, &color->light, &color->light}};

  uint8_t covered = (next && next->primary.ALPHA == 255 ? 1 : 0);
  // Top row
  uint8_t *pos = pixel(x, y + covered);

  if (color->primary.ALPHA == 255) {
    for (size_t j = covered; j < 4; ++j, pos = pixel(x, y + j))
      for (size_t i = 0; i < 4; ++i, pos += CHANSPERPIXEL)
        memcpy(pos, sprite[j][i], BYTESPERPIXEL);
  } else {
    for (size_t j = covered; j < 4; ++j, pos = pixel(x, y + j))
      for (size_t i = 0; i < 4; ++i, pos += CHANSPERPIXEL)
        blend(pos, (uint8_t *)sprite[j][i]);
  }
}

// __  __                _
//|  \/  | ___ _ __ __ _(_)_ __   __ _
//| |\/| |/ _ \ '__/ _` | | '_ \ / _` |
//| |  | |  __/ | | (_| | | | | | (_| |
//|_|  |_|\___|_|  \__, |_|_| |_|\__, |
//                 |___/         |___/
// This is the canvas merging code.

size_t IsometricCanvas::calcAnchor(const IsometricCanvas &subCanvas) {
  // Determine where in the canvas' 2D matrix is the subcanvas supposed to
  // go: the anchor is the bottom left pixel in the canvas where the
  // sub-canvas must be superimposed
  size_t anchorX = 0, anchorY = height;
  const size_t minOffset =
      subCanvas.map.minX - map.minX + subCanvas.map.minZ - map.minZ;
  const size_t maxOffset =
      map.maxX - subCanvas.map.maxX + map.maxZ - subCanvas.map.maxZ;

  switch (map.orientation) {
    // We know an image's width is relative to it's terrain size; we use
    // that property to determine where to put the subcanvas.
  case NW:
    anchorX = minOffset * 2;
    anchorY = height - maxOffset;
    break;

    // This is the opposite of NW
  case SE:
    anchorX = maxOffset * 2;
    anchorY = height - minOffset;
    break;

  case SW:
    anchorX = maxOffset * 2;
    anchorY = height - maxOffset;
    break;

  case NE:
    anchorX = minOffset * 2;
    anchorY = height - minOffset;
    break;
  }

  // Adjust the padding before translating to an offset
  anchorX = anchorX + padding - subCanvas.padding;
  anchorY = anchorY - padding + subCanvas.padding;

  // Translate those coordinates as an offset from the beginning of the
  // buffer
  return (anchorX + width * anchorY) * BYTESPERPIXEL;
}

void overLay(uint8_t *const dest, const uint8_t *const source,
             const size_t width) {
  // Render a sub-canvas above the canvas' content
  for (size_t pixel = 0; pixel < width; pixel++) {
    const uint8_t *data = source + pixel * BYTESPERPIXEL;
    // If the subCanvas is empty here, skip
    if (!data[3])
      continue;

    // If the subCanvas has a fully opaque block or the canvas has
    // nothing, just overwrite the canvas' pixel
    if (data[3] == 0xff || !(dest + pixel * BYTESPERPIXEL)[3]) {
      memcpy(dest + pixel * BYTESPERPIXEL, data, BYTESPERPIXEL);
      continue;
    }

    // Finally, blend the transparent pixel into the canvas
    blend(dest + pixel * BYTESPERPIXEL, data);
  }
}

void underLay(uint8_t *const dest, const uint8_t *const source,
              const size_t width) {
  // Render a sub-canvas under the canvas' content
  uint8_t tmpPixel[4];

  for (size_t pixel = 0; pixel < width; pixel++) {
    const uint8_t *data = source + pixel * BYTESPERPIXEL;
    // If the subCanvas is empty here, or the canvas already has a pixel
    if (!data[3] || (dest + pixel * BYTESPERPIXEL)[3] == 0xff)
      continue;

    memcpy(tmpPixel, dest + pixel * BYTESPERPIXEL, BYTESPERPIXEL);
    memcpy(dest + pixel * BYTESPERPIXEL, data, BYTESPERPIXEL);
    blend(dest + pixel * BYTESPERPIXEL, tmpPixel);
  }
}

void IsometricCanvas::merge(const IsometricCanvas &subCanvas) {
#ifdef CLOCK
  auto begin = std::chrono::high_resolution_clock::now();
#endif

  // This routine determines where the subCanvas' buffer should be
  // written, then writes it in the objects' own buffer. This results in a
  // "merge" that really is a superimposition of the subCanvas onto the
  // main canvas.
  //
  // This routine is supposed to be called multiple times with ORDERED
  // subcanvasses
  if (subCanvas.width > width || subCanvas.height > height) {
    logger::error("Cannot merge a canvas of bigger dimensions\n");
    return;
  }

  // Determine where in the canvas' 2D matrix is the subcanvas supposed to
  // go: the anchor is the bottom left pixel in the canvas where the
  // sub-canvas must be superimposed, translated as an offset from the
  // beginning of the buffer
  const size_t anchor = calcAnchor(subCanvas);

  // For every line of the subCanvas, we create a pointer to its
  // beginning, and a pointer to where in the canvas it should be copied
  for (size_t line = 1; line < subCanvas.height + 1; line++) {
    uint8_t *subLine = subCanvas.bytesBuffer + subCanvas.size -
                       line * subCanvas.width * BYTESPERPIXEL;
    uint8_t *position = bytesBuffer + anchor - line * width * BYTESPERPIXEL;

    // Then import the line over or under the existing data, depending on
    // the orientation
    if (map.orientation == NW || map.orientation == SW)
      overLay(position, subLine, subCanvas.width);
    else
      underLay(position, subLine, subCanvas.width);
  }

#ifdef CLOCK
  auto end = std::chrono::high_resolution_clock::now();
  logger::info("Merged canvas in {}ms\n",
               std::chrono::duration<double, std::milli>(end - begin).count());
#endif
}
