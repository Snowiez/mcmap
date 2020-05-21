#include "./VERSION"
#include "./draw_png.h"
#include "./helper.h"
#include "./settings.h"
#include "./worldloader.h"
#include <algorithm>
#include <string>
#include <utility>

using std::string;

void printHelp(char *binary);
void render(IsometricCanvas *canvas, const Terrain::Data &world);

void printHelp(char *binary) {
  printf("\nmcmap - an isometric minecraft map rendering tool.\n"
         "Version " VERSION " %dbit\n\n"
         "Usage: %s <options> WORLDPATH\n\n"
         "  -from X Z        coordinates of the block to start rendering at\n"
         "  -to X Z          coordinates of the block to stop rendering at\n"
         "  -min/max VAL     minimum/maximum Y index of blocks to render\n"
         "  -file NAME       output file; default is 'output.png'\n"
         "  -colors NAME     color file to use; default is 'colors.json'\n"
         "  -nw -ne -se -sw  the orientation of the map\n"
         "  -nowater         do not render water\n"
         "  -nether          render the nether\n"
         "  -end             render the end\n",
         8 * static_cast<int>(sizeof(size_t)), binary);
}

int main(int argc, char **argv) {
  Settings::WorldOptions options;
  Colors::Palette colors;
  Colors::load(options.colorFile, &colors);

  printf("mcmap " VERSION " %dbit\n", 8 * static_cast<int>(sizeof(size_t)));

  // Always same random seed, as this is only used for block noise,
  // which should give the same result for the same input every time
  srand(1337);

  if (argc < 2 || !parseArgs(argc, argv, &options)) {
    printHelp(argv[0]);
    return 1;
  }

  Terrain::Coordinates coords = options.boundaries;

  std::filesystem::path regionDir = options.regionDir();

  Terrain::Coordinates *subCoords = new Terrain::Coordinates[options.splits];
  splitCoords(coords, subCoords, options.splits);

  IsometricCanvas finalCanvas(coords, colors);

#pragma omp parallel shared(subCoords, finalCanvas)
  {
#pragma omp for ordered schedule(static)
    for (size_t i = 0; i < options.splits; i++) {
#define coords subCoords[i]
      Colors::Palette localColors;

      // Load the minecraft terrain to render
      Terrain::Data world(coords);
      world.load(regionDir);

      // Cap the height to avoid having a ridiculous image height
      coords.minY = std::max(coords.minY, world.minHeight());
      coords.maxY = std::min(coords.maxY, world.maxHeight());

      Colors::filter(colors, world.cache, &localColors);

      // Overwrite water if asked to
      // TODO expand this to other blocks
      if (options.hideWater)
        localColors["minecraft:water"] = Colors::Block();

      IsometricCanvas canvas(coords, localColors);
      canvas.drawTerrain(world);

#pragma omp ordered
      { finalCanvas.merge(canvas); }
    }
  }

  PNG::Image(options.outFile, &finalCanvas).save();
  printf("Job complete.\n");
  return 0;
}
