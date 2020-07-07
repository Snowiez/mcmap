#include "worldloader.h"

NBT minecraft_air(nbt::tag_type::tag_end);

void scanWorldDirectory(const std::filesystem::path &regionDir,
                        Coordinates *savedWorld) {
  const char delimiter = '.';
  std::string index;
  char buffer[4];
  int32_t regionX, regionZ;
  savedWorld->setUndefined();

  for (auto &region : std::filesystem::directory_iterator(regionDir)) {
    // This loop parses files with name 'r.x.y.mca', extracting x and y. This is
    // done by creating a string stream and using `getline` with '.' as a
    // delimiter.
    std::stringstream ss(region.path().filename().c_str());
    std::getline(ss, index, delimiter); // This removes the 'r.'
    std::getline(ss, index, delimiter);

    regionX = atoi(index.c_str());

    std::getline(ss, index, delimiter);

    regionZ = atoi(index.c_str());

    std::ifstream regionData(region.path());
    for (size_t chunk = 0; chunk < REGIONSIZE * REGIONSIZE; chunk++) {
      regionData.read(buffer, 4);

      if (*((uint32_t *)&buffer) == 0) {
        continue;
      }

      savedWorld->minX =
          std::min(savedWorld->minX, int32_t((regionX << 5) + (chunk & 0x1f)));
      savedWorld->maxX =
          std::max(savedWorld->maxX, int32_t((regionX << 5) + (chunk & 0x1f)));
      savedWorld->minZ =
          std::min(savedWorld->minZ, int32_t((regionZ << 5) + (chunk >> 5)));
      savedWorld->maxZ =
          std::max(savedWorld->maxZ, int32_t((regionZ << 5) + (chunk >> 5)));
    }
  }

  // Convert chunk indexes to blocks
  savedWorld->minX = savedWorld->minX << 4;
  savedWorld->minZ = savedWorld->minZ << 4;
  savedWorld->maxX = ((savedWorld->maxX + 1) << 4) - 1;
  savedWorld->maxZ = ((savedWorld->maxZ + 1) << 4) - 1;
}

void Terrain::Data::load(const std::filesystem::path &regionDir) {
  // Parse all the necessary region files
  for (int16_t rx = REGION(map.minX); rx < REGION(map.maxX) + 1; rx++) {
    for (int16_t rz = REGION(map.minZ); rz < REGION(map.maxZ) + 1; rz++) {
      std::filesystem::path regionFile = std::filesystem::path(regionDir) /=
          "r." + std::to_string(rx) + "." + std::to_string(rz) + ".mca";

      if (!std::filesystem::exists(regionFile)) {
        logger::warn("Region file r.{}.{}.mca does not exist, skipping ..\n",
                     rx, rz);
        continue;
      }

      loadRegion(regionFile, rx, rz);
      // Printing the progress requires the coordinates to be re-mapped from 0,
      // hence the awful pasta dish here.
#define SIZEX (REGION(map.maxX) - REGION(map.minX) + 1)
#define SIZEZ (REGION(map.maxZ) - REGION(map.minZ) + 1)
      logger::printProgress("Loading world",
                            (rx - REGION(map.minX)) * SIZEZ +
                                (rz - REGION(map.minZ)),
                            SIZEX * SIZEZ);
#undef SIZEX
#undef SIZEZ
    }
  }
}

void Terrain::Data::loadRegion(const std::filesystem::path &regionFile,
                               const int regionX, const int regionZ) {
  FILE *regionHandle;
  uint8_t regionHeader[REGION_HEADER_SIZE];

  if (!(regionHandle = fopen(regionFile.c_str(), "rb"))) {
    logger::error("Error opening region file {}\n", regionFile.c_str());
    return;
  }
  // Then, we read the header (of size 4K) storing the chunks locations

  if (fread(regionHeader, sizeof(uint8_t), REGION_HEADER_SIZE, regionHandle) !=
      REGION_HEADER_SIZE) {
    logger::error("Header too short in {}\n", regionFile.c_str());
    fclose(regionHandle);
    return;
  }

  // For all the chunks in the file
  for (int it = 0; it < REGIONSIZE * REGIONSIZE; it++) {
    // Bound check
    const int chunkX = (regionX << 5) + (it & 0x1f);
    const int chunkZ = (regionZ << 5) + (it >> 5);
    if (chunkX < map.minX || chunkX > map.maxX || chunkZ < map.minZ ||
        chunkZ > map.maxZ) {
      // Chunk is not in bounds
      continue;
    }

    // Get the location of the data from the header
    const uint32_t offset = (_ntohl(regionHeader + it * 4) >> 8) * 4096;

    loadChunk(offset, regionHandle, chunkX, chunkZ);
  }

  fclose(regionHandle);
}

void Terrain::Data::inflateChunk(vector<NBT> *sections) {
  // Some chunks are "hollow", empty sections being present between blocks.
  // Internally, minecraft does not store those empty sections, instead relying
  // on the section index (key "Y"). This routine creates empty sections where
  // they should be, to save a lot of time when rendering.
  //
  // This method ensure all sections from index 0 to the highest existing
  // section are inside the vector. This allows us to bypass al lot of checks
  // inside the critical drawing loop.

  // First of all, pad the beginning of the array if the lowest sections are
  // empty. This index is important and will be used later
  int8_t index = *(sections->front()["Y"].get<int8_t *>());

  // We use `tag_end` to avoid initalizing too much stuff
  for (int i = index - 1; i > -1; i--)
    sections->insert(sections->begin(), NBT(nbt::tag_type::tag_end));

  // Then, go through the array and fill holes
  // As we check for the "Y" child in the compound, and did not add it
  // previously, the index MUST not change from the original first index
  vector<NBT>::iterator it = sections->begin() + index, next = it + 1;

  while (it != sections->end() && next != sections->end()) {
    uint8_t diff = *(next->operator[]("Y").get<int8_t *>()) - index - 1;

    if (diff) {
      while (diff--) {
        it = sections->insert(it + 1, NBT(nbt::tag_type::tag_end));
        ++index;
      }
    }

    // Increment both iterators
    next = ++it + 1;
    index++;
  }
}

void Terrain::Data::stripChunk(vector<NBT> *sections) {
  // Some chunks have a -1 section, we'll pop that real quick
  if (!sections->empty() && *sections->front()["Y"].get<int8_t *>() == -1) {
    sections->erase(sections->begin());
  }

  // Pop all the empty top sections
  while (!sections->empty() && !sections->back().contains("Palette")) {
    sections->pop_back();
  }
}

void Terrain::Data::cacheColors(vector<NBT> *sections) {
  // Complete the cache, to determine the colors to load
  for (auto section : *sections) {
    if (section.is_end() || !section.contains("Palette"))
      continue;

    for (auto block : *section["Palette"].get<vector<NBT> *>())
      cache.push_back(*block["Name"].get<string *>());
  }
}

uint8_t Terrain::Data::importHeight(vector<NBT> *sections) {
  // If there are sections in the chunk
  const uint8_t chunkMin = *sections->front()["Y"].get<int8_t *>();
  const uint8_t chunkHeight = (*sections->back()["Y"].get<int8_t *>() + 1) << 4;

  // If the chunk's height is the highest found, record it
  if (chunkHeight > (heightBounds & 0xf0))
    heightBounds = chunkHeight | (heightBounds & 0x0f);

  return chunkHeight | chunkMin;
}

bool decompressChunk(const uint32_t offset, FILE *regionHandle,
                     uint8_t *chunkBuffer, uint64_t *length) {
  uint8_t zData[COMPRESSED_BUFFER];

  if (0 != fseek(regionHandle, offset, SEEK_SET)) {
    logger::error("Error accessing chunk data in file: {}\n", strerror(errno));
    return false;
  }

  // Read the 5 bytes that give the size and type of data
  if (5 != fread(zData, sizeof(uint8_t), 5, regionHandle)) {
    logger::error("Error reading chunk size from region file: {}\n",
                  strerror(errno));
    return false;
  }

  *length = _ntohl(zData);
  // len--; // This dates from Zahl's, no idea of its purpose

  if (fread(zData, sizeof(uint8_t), *length, regionHandle) != *length) {
    logger::error("Not enough data for chunk: {}\n", strerror(errno));
    return false;
  }

  z_stream zlibStream;
  memset(&zlibStream, 0, sizeof(z_stream));
  zlibStream.next_in = (Bytef *)zData;
  zlibStream.next_out = (Bytef *)chunkBuffer;
  zlibStream.avail_in = *length;
  zlibStream.avail_out = DECOMPRESSED_BUFFER;
  inflateInit2(&zlibStream, 32 + MAX_WBITS);

  int status = inflate(&zlibStream, Z_FINISH); // decompress in one step
  inflateEnd(&zlibStream);

  if (status != Z_STREAM_END) {
    logger::error("Error decompressing chunk: {}\n", zError(status));
    return false;
  }

  *length = zlibStream.total_out;
  return true;
}

void Terrain::Data::loadChunk(const uint32_t offset, FILE *regionHandle,
                              const int chunkX, const int chunkZ) {
  uint64_t length, chunkPos = chunkIndex(chunkX, chunkZ);

  // Buffers for chunk read from MCA files and decompression.
  uint8_t chunkBuffer[DECOMPRESSED_BUFFER];

  if (!offset || !decompressChunk(offset, regionHandle, chunkBuffer, &length))
    return;

  NBT chunk = NBT::parse(chunkBuffer, length);

  if (!chunk.contains("Level") || !chunk["Level"].contains("Sections")) {
    logger::warn("Chunk {} {} is in an unsupported format, skipping ..\n",
                 chunkX, chunkZ);
    return;
  }

  chunks[chunkPos] = std::move(chunk);
  vector<NBT> *sections =
      chunks[chunkPos]["Level"]["Sections"].get<vector<NBT> *>();

  // Strip the chunk of pointless sections
  stripChunk(sections);

  if (sections->empty()) {
    heightMap[chunkPos] = 0;
    return;
  }

  // Cache the blocks contained in the chunks, to load only the necessary
  // colors later on
  cacheColors(sections);

  // Analyze the sections vector for height info
  heightMap[chunkPos] = importHeight(sections);

  // Fill the chunk's holes with empty sections
  inflateChunk(sections);
}

int16_t blockAtPost116(const uint64_t length,
                       const std::vector<int64_t> *blockStates,
                       const uint16_t index) {
  // The `BlockStates` array contains data on the section's blocks. You have
  // to extract it by understanfing its structure.
  //
  // Although it is a array of long values, one must see it as an array of
  // block indexes, whose element size depends on the size of the Palette.
  // This routine locates the necessary long, extracts the block with bit
  // comparisons, and cross-references it in the palette to get the block
  // name.
  //
  // NEW in 1.16, longs are padded by 0s when a block cannot fit.

  // The length of a block index has to be coded on the minimal possible size,
  // which is the logarithm in base2 of the size of the palette, or 4 if the
  // logarithm is smaller.

  // First, determine how many blocks are in each long. There is an implicit
  // `floor` here, needed later.
  const uint8_t blocksPerLong = 64 / length;

  // Next, calculate where in the long array is the long containing the block.
  const uint64_t longIndex = index / blocksPerLong;

  // Once we located a long, we have to know where in the 64 bits
  // the relevant block is located.
  const uint64_t padding = (index - longIndex * blocksPerLong) * length;

  // Bring the data to the first bits of the long, then extract it by bitwise
  // comparison
  const uint64_t blockIndex =
      ((*blockStates)[longIndex] >> padding) & ((1l << length) - 1);

  // Lower data now contains the index in the palette
  return blockIndex;
}

int16_t blockAtPre116(const uint64_t length,
                      const std::vector<int64_t> *blockStates,
                      const uint16_t index) {
  // The `BlockStates` array contains data on the section's blocks. You have to
  // extract it by understanfing its structure.
  //
  // Although it is a array of long values, one must see it as an array of block
  // indexes, whose element size depends on the size of the Palette. This
  // routine locates the necessary long, extracts the block with bit
  // comparisons, and cross-references it in the palette to get the block name.

  // The length of a block index has to be coded on the minimal possible size,
  // which is the logarithm in base2 of the size of the palette, or 4 if the
  // logarithm is smaller.

  // We skip the `position` first blocks, of length `size`, then divide by 64 to
  // get the number of longs to skip from the array
  const uint64_t skip_longs = index * length >> 6;

  // Once we located the data in a long, we have to know where in the 64 bits it
  // is located. This is the remaining of the previous operation
  const int64_t padding = index * length & 63;

  // Craft a mask from the length of the block index and the padding, the apply
  // it to the long
  const uint64_t mask = ((1l << length) - 1) << padding;
  uint64_t lower_data = ((*blockStates)[skip_longs] & mask) >> padding;

  // Sometimes the length of the index does not fall entirely into a long, so
  // here we check if there is overflow and extract it too
  const int64_t overflow = padding + length - 64;
  if (overflow > 0) {
    const uint64_t upper_data =
        (*blockStates)[skip_longs + 1] & ((1l << overflow) - 1);
    lower_data = lower_data | upper_data << (length - overflow);
  }

  // Lower data now contains the index in the palette
  return lower_data;
}
