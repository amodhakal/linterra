#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include <onnxruntime_cxx_api.h>

#include "config.h"

namespace Ort {
class Env;
class Session;
class SessionOptions;
class Value;
}

class TerrainGenerator {
public:
    TerrainGenerator();
    ~TerrainGenerator();

    void init();
    void shutdown();

    std::vector<float> generate(const glm::vec2& position, uint32_t seed);
    bool isReady() const { return m_Initialized; }

private:
    struct ModelSession;
    struct ModelInfo;

    void runBaseStage(float* latentOut, const float* conditioning, uint32_t seed);
    void runCoarseStage(float* latentOut, const float* baseLatent, uint32_t seed);
    void runDecoder(float* terrainOut, const float* coarseLatent);

    void generateNoise(float* output, size_t count, uint32_t seed);
    void upscaleBilinear(const float* input, float* output, int channels, int h, int w, int outH, int outW);

    std::unique_ptr<Ort::Env> m_Env;
    std::unique_ptr<Ort::Session> m_BaseSession;
    std::unique_ptr<Ort::Session> m_CoarseSession;
    std::unique_ptr<Ort::Session> m_DecoderSession;
    Ort::MemoryInfo m_MemInfo{nullptr};

    std::vector<const char*> m_BaseInputNames;
    std::vector<const char*> m_BaseOutputNames;
    std::vector<const char*> m_CoarseInputNames;
    std::vector<const char*> m_CoarseOutputNames;
    std::vector<const char*> m_DecoderInputNames;
    std::vector<const char*> m_DecoderOutputNames;

    static constexpr int BASE_RES = 64;
    static constexpr int COARSE_RES = 64;
    static constexpr int DECODER_RES = 512;
    static constexpr int BASE_CHANNELS = 5;
    static constexpr int COARSE_CHANNELS_IN = 11;
    static constexpr int COARSE_CHANNELS_OUT = 6;
    static constexpr int DECODER_CHANNELS = 5;
    static constexpr int DIFFUSION_STEPS = 20;

    bool m_Initialized = false;
    bool m_WarmedUp = false;
};

extern TerrainGenerator g_TerrainGenerator;