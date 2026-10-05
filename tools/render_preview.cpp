#include "../firmware/sloth_pet/pet_renderer.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr,
        "Usage: render_preview output.ppm [idle|feed|play|sleep|dance] [ms] "
        "[charging|battery|low|usb|unknown] [sloth|cat|frog|conure] "
        "[jungle|meadow|night|nyc|space|island|undersea] [name] [show|hide] [joke] "
        "[reaction -1..11] [reaction elapsed ms]\n");
    return 1;
  }
  sloth::Snapshot state = {};
  state.fullness = 78;
  state.happiness = 90;
  state.energy = 62;
  int action = 0;
  unsigned selected = 0;
  const char* message = "LIFE IN THE SLOW LANE";
  if (argc > 2 && argv[2][0] == 'f') {
    action = 1; message = "A TASTY LITTLE SNACK";
  } else if (argc > 2 && argv[2][0] == 'p') {
    action = 2; selected = 1; message = "YOU MAKE ME HAPPY!";
  } else if (argc > 2 && argv[2][0] == 's') {
    state.sleeping = 1; selected = 2; message = "REST +1 EVERY 3 SEC";
  } else if (argc > 2 && argv[2][0] == 'd') {
    action = 3; selected = 1; message = "TIME FOR A LITTLE DANCE!";
  }
  const uint32_t animationMs = argc > 3
      ? static_cast<uint32_t>(std::strtoul(argv[3], NULL, 10)) : 1050;
  sloth::Hud hud;
  hud.timeText = "8:42 AM";
  hud.zoneText = "EDT";
  hud.batteryPercent = 76;
  hud.batteryPresent = true;
  hud.charging = true;
  hud.externalPower = true;
  if (argc > 4 && argv[4][0] == 'b') {
    hud.timeText = "12:59 PM";
    hud.zoneText = "EST";
    hud.batteryPercent = 100;
    hud.charging = hud.externalPower = false;
  } else if (argc > 4 && argv[4][0] == 'l') {
    hud.batteryPercent = 12;
    hud.charging = hud.externalPower = false;
  } else if (argc > 4 && argv[4][0] == 'u') {
    if (argv[4][1] == 's') {
      hud.batteryPercent = -1;
      hud.batteryPresent = hud.charging = false;
    } else {
      hud = sloth::Hud();
    }
  }
  sloth::Settings settings;
  if (argc > 5 && (!std::strcmp(argv[5], "conure") || !std::strcmp(argv[5], "sun-conure")))
    settings.animal = sloth::Animal::SunConure;
  else if (argc > 5 && argv[5][0] == 'c') settings.animal = sloth::Animal::Cat;
  else if (argc > 5 && argv[5][0] == 'f') settings.animal = sloth::Animal::Frog;
  if (argc > 6) {
    const char* scene = argv[6];
    if (!std::strcmp(scene, "jungle")) settings.scene = sloth::Scene::Jungle;
    else if (!std::strcmp(scene, "meadow")) settings.scene = sloth::Scene::Meadow;
    else if (!std::strcmp(scene, "night")) settings.scene = sloth::Scene::Night;
    else if (!std::strcmp(scene, "nyc") || !std::strcmp(scene, "NYC")) settings.scene = sloth::Scene::NYC;
    else if (!std::strcmp(scene, "space")) settings.scene = sloth::Scene::Space;
    else if (!std::strcmp(scene, "island")) settings.scene = sloth::Scene::Island;
    else if (!std::strcmp(scene, "undersea") || !std::strcmp(scene, "Under the Sea"))
      settings.scene = sloth::Scene::UnderSea;
    else {
      std::fprintf(stderr, "Unknown scene: %s\n", scene);
      return 1;
    }
  }
  if (argc > 7) {
    unsigned i = 0;
    for (; i < sizeof(settings.name) - 1 && argv[7][i]; ++i) settings.name[i] = argv[7][i];
    settings.name[i] = '\0';
  }
  if (argc > 8 && argv[8][0] == 'h') settings.showSubtitle = false;
  uint16_t pixels[240 * 240];
  const int reaction = argc > 10 ? static_cast<int>(std::strtol(argv[10], NULL, 10)) : -1;
  const uint32_t reactionMs = argc > 11
      ? static_cast<uint32_t>(std::strtoul(argv[11], NULL, 10)) : 900;
  sloth::drawPet(pixels, state, selected, message, animationMs, action, hud, settings,
                argc > 9 ? argv[9] : nullptr, reaction, reactionMs);
  FILE* file = std::fopen(argv[1], "wb");
  if (!file) return 2;
  std::fprintf(file, "P6\n480 480\n255\n");
  for (int y = 0; y < 480; ++y) {
    for (int x = 0; x < 480; ++x) {
      const uint16_t pixel = pixels[(y / 2) * 240 + x / 2];
      const unsigned r = (pixel >> 11) & 31;
      const unsigned g = (pixel >> 5) & 63;
      const unsigned b = pixel & 31;
      const unsigned char rgb[] = {
        static_cast<unsigned char>((r << 3) | (r >> 2)),
        static_cast<unsigned char>((g << 2) | (g >> 4)),
        static_cast<unsigned char>((b << 3) | (b >> 2))
      };
      std::fwrite(rgb, 1, 3, file);
    }
  }
  return std::fclose(file) == 0 ? 0 : 3;
}
