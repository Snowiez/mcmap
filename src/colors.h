#ifndef COLORS_
#define COLORS_

#include "./helper.h"
#include "./include/json.hpp"
#include "./logger.h"
#include <filesystem>
#include <list>
#include <map>
#include <string>

// Byte order see below. Colors aligned to word boundaries for some speedup
// Brightness is precalculated to speed up calculations later
// Colors are stored twice since BMP and PNG need them in different order
// Noise is supposed to look normal when -noise 10 is given

// First 256 lines correspond to individual block IDs
// ie colors[0] contains color inforamtions for air
//
// Following 256 lines store variant information
// lines colors[256] to color[263] stores stone variants

using nlohmann::json;
using std::list;
using std::map;
using std::string;

#define PRED 0
#define PGREEN 1
#define PBLUE 2
#define PALPHA 3
#define PNOISE 4
#define PBRIGHTNESS 5
#define VINDEX 6
#define BLOCK_TYPE 7

#define GETBRIGHTNESS(c)                                                       \
  (uint8_t) sqrt(double(PRED[c]) * double(PRED[c]) * .236 +                    \
                 double(PGREEN[c]) * double(PGREEN[c]) * .601 +                \
                 double(PBLUE[c]) * double(PBLUE[c]) * .163)

/*bool loadColorsFromFile(const char *file);
bool dumpColorsToFile(const char *file);
bool extractColors(const char *file);
bool loadBiomeColors(const char* path);*/

namespace Colors {

enum BlockTypes {
#define DEFINETYPE(STRING, CALLBACK) CALLBACK,
  FULL = 0,
#include "blocktypes.def"
#undef DEFINETYPE
};

const std::unordered_map<string, Colors::BlockTypes> stringToType = {
    {"Full", Colors::BlockTypes::FULL},
#define DEFINETYPE(STRING, CALLBACK) {STRING, Colors::BlockTypes::CALLBACK},
#include "blocktypes.def"
#undef DEFINETYPE
};

const std::unordered_map<Colors::BlockTypes, string> typeToString = {
    {Colors::BlockTypes::FULL, "Full"},
#define DEFINETYPE(STRING, CALLBACK) {Colors::BlockTypes::CALLBACK, STRING},
#include "blocktypes.def"
#undef DEFINETYPE
};

const std::map<string, list<int>> markerColors = {
    {"white", {250, 250, 250, 70}},
    {"red", {250, 0, 0, 70}},
    {"green", {0, 250, 0, 70}},
    {"blue", {0, 0, 250, 70}},
};

struct Color {
  uint8_t R, G, B;
  uint8_t ALPHA, NOISE, BRIGHTNESS;

  Color() { R = G = B = ALPHA = NOISE = BRIGHTNESS = 0; }

  Color(list<int> values) : Color() {
    uint8_t index = 0;
    // Hacky hacky stuff
    // convert the struct to a uint8_t list to fill its elements
    // as we know uint8_t elements will be contiguous in memory
    for (auto it : values)
      if (index < 6)
        ((uint8_t *)this)[index++] = it;
  }

  inline void modColor(const int mod) {
    R = clamp(R + mod);
    G = clamp(G + mod);
    B = clamp(B + mod);
  }

  bool empty() const { return !(R || G || B || ALPHA); }

  uint8_t brightness() const {
    return (uint8_t)sqrt(double(R) * double(R) * .2126 +
                         double(G) * double(G) * .7152 +
                         double(B) * double(B) * .0722);
  }

  bool operator==(const Colors::Color &other) {
    return (R == other.R && G == other.G && B == other.B);
  }

  bool operator!=(const Colors::Color &other) {
    return !this->operator==(other);
  }
};

struct Block {
  Colors::Color primary, secondary; // 12 bytes
  Colors::BlockTypes type;
  Colors::Color light, dark; // 12 bytes

  Block() : primary(), secondary() { type = Colors::BlockTypes::FULL; }

  Block(const Colors::BlockTypes &bt, list<int> c1)
      : primary(c1), secondary(), light(c1), dark(c1) {
    type = bt;
    light.modColor(-17);
    dark.modColor(-27);
  }

  Block(const Colors::BlockTypes &bt, list<int> c1, list<int> c2)
      : primary(c1), secondary(c2), light(c1), dark(c1) {
    type = bt;
    light.modColor(-17);
    dark.modColor(-27);
  }
};

struct Marker {
  int64_t x, z;
  string color_name;
  Block color;

  Marker() : color_name("white") {
    x = std::numeric_limits<int64_t>::max();
    z = std::numeric_limits<int64_t>::max();
  }

  Marker(size_t x, size_t z, string c) : x(x), z(z), color_name(c) {
    auto marker = markerColors.find(color_name);
    if (marker == markerColors.end()) {
      fprintf(stderr, "Invalid marker color: %s\n", color_name.c_str());
      color_name = "white";
    }

    color = Block(BlockTypes::drawBeam, markerColors.find(color_name)->second);
  };
};

typedef map<string, Colors::Block> Palette;
bool load(const std::filesystem::path &, Palette *);
bool load(const std::filesystem::path &, const std::vector<string> &filter,
          Palette *);
void filter(const Palette &, const std::vector<string> &filter, Palette *);

void to_json(json &j, const Block &b);
void from_json(const json &j, Block &b);

void to_json(json &j, const Palette &p);
void from_json(const json &j, Palette &p);

} // namespace Colors

#endif // COLORS_H_
