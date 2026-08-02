// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/vector_math.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/shader/generator/pica_fs_config.h"
#include "video_core/shader/generator/shader_uniforms.h"

namespace Memory {
class MemorySystem;
}

namespace Pica {
class PicaCore;
}

namespace VideoCore {

class RasterizerAccelerated : public RasterizerInterface {
public:
    explicit RasterizerAccelerated(Memory::MemorySystem& memory, Pica::PicaCore& pica);
    virtual ~RasterizerAccelerated() = default;

    void AddTriangle(const Pica::OutputVertex& v0, const Pica::OutputVertex& v1,
                     const Pica::OutputVertex& v2) override;

protected:
    /// Sync vertex and framgent uniforms from PICA registers
    void SyncDrawUniforms();

    /// Sync all rarely-changing state once at startup
    void SyncEntireState() override;

    /// Sync per-draw fixed-function state (called by SyncEntireState)
    virtual void SyncFixedState() = 0;

    // Individual state sync helpers
    void SyncDepthScale();
    void SyncDepthOffset();
    void SyncFogColor();
    void SyncProcTexNoise();
    void SyncProcTexBias();
    void SyncAlphaTest();
    void SyncCombinerColor();
    void SyncTevConstColor(std::size_t tev_index,
                           const Pica::TexturingRegs::TevStageConfig& tev_stage);
    void SyncGlobalAmbient();
    void SyncLightSpecular0(int light_index);
    void SyncLightSpecular1(int light_index);
    void SyncLightDiffuse(int light_index);
    void SyncLightAmbient(int light_index);
    void SyncLightPosition(int light_index);
    void SyncLightSpotDirection(int light_index);
    void SyncLightDistanceAttenuationBias(int light_index);
    void SyncLightDistanceAttenuationScale(int light_index);
    void SyncShadowBias();
    void SyncShadowTextureBias();
    void SyncTextureLodBias(int tex_index);
    void SyncTextureBorderColor(int tex_index);
    void SyncClipPlane();

protected:
    /// Structure that the hardware rendered vertices are composed of
    struct HardwareVertex {
        HardwareVertex() = default;
        HardwareVertex(const Pica::OutputVertex& v, bool flip_quaternion);

        Common::Vec4f position;
        Common::Vec4f color;
        Common::Vec2f tex_coord0;
        Common::Vec2f tex_coord1;
        Common::Vec2f tex_coord2;
        float tex_coord0_w;
        Common::Vec4f normquat;
        Common::Vec3f view;
    };

    struct VertexArrayInfo {
        u32 vs_input_index_min;
        u32 vs_input_index_max;
        u32 vs_input_size;

        bool Invalid() const {
            return vs_input_index_min == 0 && vs_input_index_max == 0 && vs_input_size == 0;
        }
    };

    /// Retrieve the range and the size of the input vertex
    VertexArrayInfo AnalyzeVertexArray(bool is_indexed, u32 stride_alignment = 1);

protected:
    Memory::MemorySystem& memory;
    Pica::PicaCore& pica;
    Pica::RegsInternal& regs;
    std::vector<HardwareVertex> vertex_batch;
    Pica::Shader::UserConfig user_config{};
    Pica::Shader::Generator::VSUniformData vs_data{};
    Pica::Shader::Generator::FSUniformData fs_data{};
    bool vs_data_dirty = true;
    bool fs_data_dirty = true;
};

} // namespace VideoCore
