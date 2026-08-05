#define NS_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION

#include <Metal/Metal.hpp>
#include <SDL.h>
#include <SDL_syswm.h>

int main() {
  SDL_Init(SDL_INIT_VIDEO);
  SDL_Window *window =
      SDL_CreateWindow("Linterra", 100, 100, 800, 600, SDL_WINDOW_SHOWN);

  MTL::Device *device = MTL::CreateSystemDefaultDevice();
  if (!device) {
    SDL_Log("No Metal device found");
    return 1;
  }

  SDL_Log("Metal device: %s", device->name()->utf8String());
  SDL_Quit();
  device->release();
  return 0;
}
