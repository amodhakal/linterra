#include "terrain.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <random>

#include <onnxruntime_cxx_api.h>

namespace {

constexpr const char* BASE_MODEL_PATH = "./models/terrain-diffusion/base_model_sim.onnx";
constexpr const char* COARSE_MODEL_PATH = "./models/terrain-diffusion/coarse_model.onnx";
constexpr const char* DECODER_MODEL_PATH = "./models/terrain-diffusion/decoder_model.onnx";

constexpr int BASE_RES = 64;
constexpr int COARSE_RES = 64;
constexpr int DECODER_RES = 512;
constexpr int BASE_CHANNELS = 5;
constexpr int COARSE_CHANNELS_IN = 11;
constexpr int COARSE_CHANNELS_OUT = 6;
constexpr int DECODER_CHANNELS = 5;
constexpr int DIFFUSION_STEPS = 20;
constexpr int COND_DIM = 58;

class PCG32 {
public:
    explicit PCG32(uint64_t seed) {
        m_State = seed ? seed : 0x853c49e6748fea9bULL;
        m_Inc = 0xda3e39cb94b95bdbULL;
        (void)next();
    }

    uint32_t next() {
        uint64_t old = m_State;
        m_State = old * 0x5851f42d4c957f2dULL + m_Inc;
        uint32_t xorshifted = static_cast<uint32_t>(((old ^ (old >> 18)) >> 27));
        uint32_t rot = static_cast<uint32_t>(old >> 59);
        return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
    }

    float nextFloat() {
        return static_cast<float>(next()) / 4294967296.0f;
    }

private:
    uint64_t m_State;
    uint64_t m_Inc;
};

}  // namespace

TerrainGenerator::TerrainGenerator() = default;

TerrainGenerator::~TerrainGenerator() {
    shutdown();
}

void TerrainGenerator::init() {
    if (m_Initialized) return;

    std::cout << "[Terrain] Initializing ONNX Runtime..." << std::endl;

    m_Env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "TerrainGen");

    Ort::SessionOptions sessionOptions;
    sessionOptions.SetIntraOpNumThreads(4);
    sessionOptions.SetInterOpNumThreads(4);
    sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

    std::cout << "[Terrain] Loading Base model..." << std::endl;
    m_BaseSession = std::unique_ptr<Ort::Session>(
        new Ort::Session(*m_Env, BASE_MODEL_PATH, sessionOptions));

    std::cout << "[Terrain] Loading Coarse model..." << std::endl;
    m_CoarseSession = std::unique_ptr<Ort::Session>(
        new Ort::Session(*m_Env, COARSE_MODEL_PATH, sessionOptions));

    std::cout << "[Terrain] Loading Decoder model..." << std::endl;
    m_DecoderSession = std::unique_ptr<Ort::Session>(
        new Ort::Session(*m_Env, DECODER_MODEL_PATH, sessionOptions));

    m_BaseInputNames = {"x", "noise_labels", "cond_0"};
    m_BaseOutputNames = {"output"};
    m_CoarseInputNames = {"x", "noise_labels", "cond_0", "cond_1", "cond_2", "cond_3", "cond_4"};
    m_CoarseOutputNames = {"output"};
    m_DecoderInputNames = {"x", "noise_labels"};
    m_DecoderOutputNames = {"output"};

    m_MemInfo = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeDefault);

    m_Initialized = true;
    std::cout << "[Terrain] ONNX Runtime initialized successfully" << std::endl;
}

void TerrainGenerator::shutdown() {
    m_BaseSession.reset();
    m_CoarseSession.reset();
    m_DecoderSession.reset();
    m_Env.reset();
    m_Initialized = false;
}

void TerrainGenerator::generateNoise(float* output, size_t count, uint32_t seed) {
    PCG32 rng(seed);
    for (size_t i = 0; i < count; ++i) {
        output[i] = rng.nextFloat() * 2.0f - 1.0f;
    }
}

void TerrainGenerator::upscaleBilinear(const float* input, float* output, int channels, int h, int w, int outH, int outW) {
    float scaleX = static_cast<float>(w - 1) / (outW - 1);
    float scaleY = static_cast<float>(h - 1) / (outH - 1);

    for (int c = 0; c < channels; ++c) {
        for (int oy = 0; oy < outH; ++oy) {
            for (int ox = 0; ox < outW; ++ox) {
                float sx = ox * scaleX;
                float sy = oy * scaleY;

                int x0 = static_cast<int>(sx);
                int y0 = static_cast<int>(sy);
                int x1 = std::min(x0 + 1, w - 1);
                int y1 = std::min(y0 + 1, h - 1);

                float fx = sx - x0;
                float fy = sy - y0;

                float v00 = input[(c * h + y0) * w + x0];
                float v10 = input[(c * h + y0) * w + x1];
                float v01 = input[(c * h + y1) * w + x0];
                float v11 = input[(c * h + y1) * w + x1];

                float top = v00 + (v10 - v00) * fx;
                float bottom = v01 + (v11 - v01) * fx;
                float value = top + (bottom - top) * fy;

                output[(c * outH + oy) * outW + ox] = value;
            }
        }
    }
}

void TerrainGenerator::runBaseStage(float* latentOut, const float* conditioning, uint32_t seed) {
    const int64_t xShape[] = {1, BASE_CHANNELS, BASE_RES, BASE_RES};
    const int64_t labelShape[] = {1};
    const int64_t condShape[] = {1, COND_DIM};

    std::vector<float> xData(BASE_CHANNELS * BASE_RES * BASE_RES);
    std::vector<float> labelData(1);
    std::vector<float> condData(COND_DIM);

    std::vector<float> currentLatent(BASE_CHANNELS * BASE_RES * BASE_RES);
    std::vector<float> noise(BASE_CHANNELS * BASE_RES * BASE_RES);

    generateNoise(currentLatent.data(), currentLatent.size(), seed);

    float sigmaMin = 0.0292f;
    float sigmaMax = 14.6146f;

    for (int step = 0; step < DIFFUSION_STEPS; ++step) {
        float t = static_cast<float>(step) / DIFFUSION_STEPS;
        float sigma = sigmaMax * std::pow(sigmaMin / sigmaMax, t);

        for (size_t i = 0; i < xData.size(); ++i) {
            xData[i] = currentLatent[i];
        }

        labelData[0] = sigma;
        for (int i = 0; i < COND_DIM; ++i) {
            condData[i] = conditioning[i];
        }

        std::vector<Ort::Value> inputs;
        inputs.reserve(3);
        inputs.emplace_back(Ort::Value::CreateTensor<float>(m_MemInfo, xData.data(), xData.size(), xShape, 4));
        inputs.emplace_back(Ort::Value::CreateTensor<float>(m_MemInfo, labelData.data(), labelData.size(), labelShape, 1));
        inputs.emplace_back(Ort::Value::CreateTensor<float>(m_MemInfo, condData.data(), condData.size(), condShape, 2));

        auto outputTensors = m_BaseSession->Run(
            Ort::RunOptions{nullptr},
            m_BaseInputNames.data(), inputs.data(), 3,
            m_BaseOutputNames.data(), 1);

        auto& outputTensor = outputTensors[0];
        auto* outputData = outputTensor.GetTensorMutableData<float>();
        size_t outputSize = outputTensor.GetTensorTypeAndShapeInfo().GetElementCount();

        float sigmaNext = (step < DIFFUSION_STEPS - 1) ?
            sigmaMax * std::pow(sigmaMin / sigmaMax, static_cast<float>(step + 1) / DIFFUSION_STEPS) : 0.0f;

        float dt = sigmaNext - sigma;
        if (std::abs(dt) < 1e-6f) dt = -sigma;

        generateNoise(noise.data(), noise.size(), seed + step + 1);

        for (size_t i = 0; i < currentLatent.size(); ++i) {
            float predNoise = outputData[i];
            float noiseTerm = noise[i] * dt;
            currentLatent[i] = currentLatent[i] - predNoise * sigma + noiseTerm;
        }

        if (step % 5 == 0) {
            std::cout << "[Terrain] Base stage: " << (static_cast<float>(step + 1) / DIFFUSION_STEPS * 100.0f) << "%" << std::endl;
        }
    }

    std::memcpy(latentOut, currentLatent.data(), currentLatent.size() * sizeof(float));
}

void TerrainGenerator::runCoarseStage(float* latentOut, const float* baseLatent, uint32_t seed) {
    Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeDefault);

    const int64_t xShape[] = {1, COARSE_CHANNELS_IN, COARSE_RES, COARSE_RES};
    const int64_t labelShape[] = {1};
    const int64_t condShape[] = {1};

    std::vector<float> xData(COARSE_CHANNELS_IN * COARSE_RES * COARSE_RES);
    std::vector<float> labelData(1);
    std::vector<float> condData[5];
    for (int i = 0; i < 5; ++i) condData[i].resize(1);

    std::vector<float> currentLatent(COARSE_CHANNELS_IN * COARSE_RES * COARSE_RES);
    std::vector<float> noise(COARSE_CHANNELS_IN * COARSE_RES * COARSE_RES);

    std::vector<float> upscaledBase(COARSE_CHANNELS_OUT * COARSE_RES * COARSE_RES);
    for (int c = 0; c < COARSE_CHANNELS_OUT; ++c) {
        for (int i = 0; i < COARSE_RES * COARSE_RES; ++i) {
            upscaledBase[c * COARSE_RES * COARSE_RES + i] = baseLatent[c * COARSE_RES * COARSE_RES + i];
        }
    }

    generateNoise(currentLatent.data(), currentLatent.size(), seed + 10000);

    float sigmaMin = 0.0292f;
    float sigmaMax = 14.6146f;

    float baseCond = 0.0f;
    float tempCond = 0.0f;
    float precipCond = 0.0f;
    float snrCond0 = 0.5f;
    float snrCond1 = 0.5f;

    for (int step = 0; step < DIFFUSION_STEPS; ++step) {
        float t = static_cast<float>(step) / DIFFUSION_STEPS;
        float sigma = sigmaMax * std::pow(sigmaMin / sigmaMax, t);

        for (size_t i = 0; i < currentLatent.size(); ++i) {
            xData[i] = currentLatent[i];
        }

        labelData[0] = sigma;
        condData[0][0] = baseCond;
        condData[1][0] = tempCond;
        condData[2][0] = precipCond;
        condData[3][0] = snrCond0;
        condData[4][0] = snrCond1;

        std::vector<Ort::Value> inputs;
        inputs.reserve(7);
        inputs.emplace_back(Ort::Value::CreateTensor<float>(m_MemInfo, xData.data(), xData.size(), xShape, 4));
        inputs.emplace_back(Ort::Value::CreateTensor<float>(m_MemInfo, labelData.data(), labelData.size(), labelShape, 1));
        inputs.emplace_back(Ort::Value::CreateTensor<float>(m_MemInfo, condData[0].data(), condData[0].size(), condShape, 1));
        inputs.emplace_back(Ort::Value::CreateTensor<float>(m_MemInfo, condData[1].data(), condData[1].size(), condShape, 1));
        inputs.emplace_back(Ort::Value::CreateTensor<float>(m_MemInfo, condData[2].data(), condData[2].size(), condShape, 1));
        inputs.emplace_back(Ort::Value::CreateTensor<float>(m_MemInfo, condData[3].data(), condData[3].size(), condShape, 1));
        inputs.emplace_back(Ort::Value::CreateTensor<float>(m_MemInfo, condData[4].data(), condData[4].size(), condShape, 1));

        auto outputTensors = m_CoarseSession->Run(
            Ort::RunOptions{nullptr},
            m_CoarseInputNames.data(), inputs.data(), 7,
            m_CoarseOutputNames.data(), 1);

        auto& outputTensor = outputTensors[0];
        auto* outputData = outputTensor.GetTensorMutableData<float>();
        size_t outputSize = outputTensor.GetTensorTypeAndShapeInfo().GetElementCount();

        float sigmaNext = (step < DIFFUSION_STEPS - 1) ?
            sigmaMax * std::pow(sigmaMin / sigmaMax, static_cast<float>(step + 1) / DIFFUSION_STEPS) : 0.0f;

        float dt = sigmaNext - sigma;
        if (std::abs(dt) < 1e-6f) dt = -sigma;

        generateNoise(noise.data(), noise.size(), seed + 10000 + step + 1);

        for (size_t i = 0; i < currentLatent.size(); ++i) {
            float predNoise = outputData[i];
            float noiseTerm = noise[i] * dt;
            currentLatent[i] = currentLatent[i] - predNoise * sigma + noiseTerm;
        }

        if (step % 5 == 0) {
            std::cout << "[Terrain] Coarse stage: " << (static_cast<float>(step + 1) / DIFFUSION_STEPS * 100.0f) << "%" << std::endl;
        }
    }

    for (int i = 0; i < COARSE_CHANNELS_OUT * COARSE_RES * COARSE_RES; ++i) {
        latentOut[i] = currentLatent[i];
    }
}

void TerrainGenerator::runDecoder(float* terrainOut, const float* coarseLatent) {
    const int64_t xShape[] = {1, 5, DECODER_RES, DECODER_RES};
    const int64_t labelShape[] = {1};

    std::vector<float> xData(5 * DECODER_RES * DECODER_RES);

    std::vector<float> upscaled(5 * DECODER_RES * DECODER_RES);
    upscaleBilinear(coarseLatent, upscaled.data(), 5, COARSE_RES, COARSE_RES, DECODER_RES, DECODER_RES);

    for (size_t i = 0; i < xData.size(); ++i) {
        xData[i] = upscaled[i];
    }

    float labelData = 0.0f;

    std::vector<Ort::Value> inputs;
    inputs.reserve(2);
    inputs.emplace_back(Ort::Value::CreateTensor<float>(m_MemInfo, xData.data(), xData.size(), xShape, 4));
    inputs.emplace_back(Ort::Value::CreateTensor<float>(m_MemInfo, &labelData, 1, labelShape, 1));

    auto outputTensors = m_DecoderSession->Run(
        Ort::RunOptions{nullptr},
        m_DecoderInputNames.data(), inputs.data(), 2,
        m_DecoderOutputNames.data(), 1);

    auto& outputTensor = outputTensors[0];
    auto* outputData = outputTensor.GetTensorMutableData<float>();
    size_t outputSize = outputTensor.GetTensorTypeAndShapeInfo().GetElementCount();

    for (size_t i = 0; i < std::min(outputSize, static_cast<size_t>(DECODER_RES * DECODER_RES)); ++i) {
        terrainOut[i] = outputData[i];
    }
}

std::vector<float> TerrainGenerator::generate(const glm::vec2& position, uint32_t seed) {
    if (!m_Initialized) {
        init();
    }

    std::cout << "[Terrain] Generating terrain for position: (" << position.x << ", " << position.y << ") seed: " << seed << std::endl;

    std::vector<float> conditioning(COND_DIM);
    float px = position.x * 0.1f;
    float pz = position.y * 0.1f;
    for (int i = 0; i < 16; ++i) {
        for (int j = 0; j < 4; ++j) {
            int idx = i * 4 + j;
            if (idx < COND_DIM) {
                float freq = 1.0f + static_cast<float>(i);
                conditioning[idx] = sinf(px * freq + static_cast<float>(j)) * cosf(pz * freq);
            }
        }
    }

    std::vector<float> baseLatent(BASE_CHANNELS * BASE_RES * BASE_RES);
    runBaseStage(baseLatent.data(), conditioning.data(), seed);

    std::vector<float> coarseLatent(COARSE_CHANNELS_OUT * COARSE_RES * COARSE_RES);
    runCoarseStage(coarseLatent.data(), baseLatent.data(), seed);

    std::vector<float> terrain(DECODER_RES * DECODER_RES);
    runDecoder(terrain.data(), coarseLatent.data());

    std::cout << "[Terrain] Generation complete" << std::endl;
    return terrain;
}

TerrainGenerator g_TerrainGenerator;