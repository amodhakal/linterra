#define NS_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#include <Metal/Metal.hpp>
#include <QuartzCore/CAMetalLayer.hpp>

#include <SDL.h>
#include <SDL_metal.h>
#include <simd/simd.h>

int main() {
  SDL_Init(SDL_INIT_VIDEO);

  SDL_Window *window = SDL_CreateWindow(
      "linterra", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 800, 600,
      SDL_WINDOW_METAL | SDL_WINDOW_RESIZABLE);
  if (!window) {
    SDL_Log("Failed to create window: %s", SDL_GetError());
    return 1;
  }

  SDL_MetalView metalView = SDL_Metal_CreateView(window);
  CA::MetalLayer *layer = (CA::MetalLayer *)SDL_Metal_GetLayer(metalView);

  MTL::Device *device = MTL::CreateSystemDefaultDevice();
  if (!device) {
    SDL_Log("No Metal device found");
    return 1;
  }
  SDL_Log("Metal device: %s", device->name()->utf8String());

  layer->setDevice(device);
  layer->setPixelFormat(MTL::PixelFormatBGRA8Unorm);

  MTL::CommandQueue *commandQueue = device->newCommandQueue();

  // --- Load shader library ---
  NS::Error *error = nullptr;
  NS::String *libPath =
      NS::String::string("shaders.metallib", NS::UTF8StringEncoding);
  MTL::Library *library = device->newLibrary(libPath, &error);
  if (!library) {
    SDL_Log("Failed to load shader library: %s",
            error->localizedDescription()->utf8String());
    return 1;
  }

  MTL::Function *vertexFn = library->newFunction(
      NS::String::string("vertex_main", NS::UTF8StringEncoding));
  MTL::Function *fragmentFn = library->newFunction(
      NS::String::string("fragment_main", NS::UTF8StringEncoding));
  if (!vertexFn || !fragmentFn) {
    SDL_Log("Failed to find shader functions in library");
    return 1;
  }

  MTL::RenderPipelineDescriptor *pipelineDesc =
      MTL::RenderPipelineDescriptor::alloc()->init();
  pipelineDesc->setVertexFunction(vertexFn);
  pipelineDesc->setFragmentFunction(fragmentFn);
  pipelineDesc->colorAttachments()->object(0)->setPixelFormat(
      MTL::PixelFormatBGRA8Unorm);

  MTL::RenderPipelineState *pipelineState =
      device->newRenderPipelineState(pipelineDesc, &error);
  if (!pipelineState) {
    SDL_Log("Failed to create pipeline state: %s",
            error->localizedDescription()->utf8String());
    return 1;
  }

  pipelineDesc->release();
  vertexFn->release();
  fragmentFn->release();
  library->release();

  // --- Triangle data ---
  simd::float2 positions[] = {{0.0f, 0.5f}, {-0.5f, -0.5f}, {0.5f, -0.5f}};
  simd::float4 colors[] = {{1.0f, 0.0f, 0.0f, 1.0f},
                           {0.0f, 1.0f, 0.0f, 1.0f},
                           {0.0f, 0.0f, 1.0f, 1.0f}};

  MTL::Buffer *positionBuffer = device->newBuffer(
      positions, sizeof(positions), MTL::ResourceStorageModeShared);
  MTL::Buffer *colorBuffer =
      device->newBuffer(colors, sizeof(colors), MTL::ResourceStorageModeShared);
  if (!positionBuffer || !colorBuffer) {
    SDL_Log("Failed to create vertex buffers");
    return 1;
  }

  // --- Main loop ---
  bool running = true;
  SDL_Event event;
  while (running) {
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT)
        running = false;
    }

    CA::MetalDrawable *drawable = layer->nextDrawable();
    if (!drawable)
      continue;

    MTL::RenderPassDescriptor *pass =
        MTL::RenderPassDescriptor::renderPassDescriptor();
    auto colorAttachment = pass->colorAttachments()->object(0);
    colorAttachment->setTexture(drawable->texture());
    colorAttachment->setLoadAction(MTL::LoadActionClear);
    colorAttachment->setClearColor(MTL::ClearColor(0.1, 0.2, 0.4, 1.0));
    colorAttachment->setStoreAction(MTL::StoreActionStore);

    MTL::CommandBuffer *cmdBuffer = commandQueue->commandBuffer();
    MTL::RenderCommandEncoder *encoder = cmdBuffer->renderCommandEncoder(pass);

    encoder->setRenderPipelineState(pipelineState);
    encoder->setVertexBuffer(positionBuffer, 0, 0);
    encoder->setVertexBuffer(colorBuffer, 0, 1);
    encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, (NS::UInteger)0,
                            (NS::UInteger)3);

    encoder->endEncoding();
    cmdBuffer->presentDrawable(drawable);
    cmdBuffer->commit();
  }

  // --- Cleanup ---
  positionBuffer->release();
  colorBuffer->release();
  pipelineState->release();
  commandQueue->release();
  device->release();
  SDL_Metal_DestroyView(metalView);
  SDL_DestroyWindow(window);
  SDL_Quit();

  return 0;
}
