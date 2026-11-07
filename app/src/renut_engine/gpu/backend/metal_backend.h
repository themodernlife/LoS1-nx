#pragma once

#include "renut_engine/gpu/backend/backend.h"

namespace renut::gpu {
std::unique_ptr<plume::RenderInterface> CreateNativeMetalInterface();
std::unique_ptr<plume::RenderShader> CreateMetalShader(plume::RenderDevice *device,
    const void *spirv, size_t size, const char *name);
bool MetalTextureCreated(const plume::RenderTexture *texture);
bool MetalPipelineCreated(const plume::RenderPipeline *pipeline);
GpuMemoryUsage QueryMetalMemory(plume::RenderDevice *device);
bool SetMetalBlendConstant(plume::RenderCommandList *cmd, const float rgba[4]);
}
