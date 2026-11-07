// Verifies the actual Renut SPIR-V -> MSL path, descriptor arrays, per-draw
// constant offsets, deferred clears, GPU completion, and plume readback.
#include "renut_engine/gpu/backend/metal_backend.h"
#include <plume_render_interface_builders.h>
#include "metal_smoke_vs.h"
#include "metal_smoke_ps.h"
#include <array>
#include <cstring>
#include <iostream>

int main() {
  using namespace plume;
  std::cout.setf(std::ios::unitbuf);
  auto api = renut::gpu::CreateNativeMetalInterface();
  auto device = api ? api->createDevice() : nullptr;
  if (!device) return 1;
  std::cout << "Metal device: " << device->getDescription().name << '\n';
  auto vs = renut::gpu::CreateMetalShader(device.get(), smoke_vs, sizeof(smoke_vs), "smoke_vs");
  auto ps = renut::gpu::CreateMetalShader(device.get(), smoke_ps, sizeof(smoke_ps), "smoke_ps");
  if (!vs || !ps) return 2;
  auto queue = device->createCommandQueue(RenderCommandListType::DIRECT);
  auto commands = queue->createCommandList();
  auto fence = device->createCommandFence();
  constexpr uint32_t width = 64, height = 64;
  constexpr auto format = RenderFormat::R8G8B8A8_UNORM;
  auto target = device->createTexture(RenderTextureDesc::ColorTarget(width, height, format));
  const RenderTexture *attachment = target.get();
  RenderFramebufferDesc fb;
  fb.colorAttachments = &attachment;
  fb.colorAttachmentsCount = 1;
  auto framebuffer = device->createFramebuffer(fb);

  auto upload = device->createBuffer(RenderBufferDesc::UploadBuffer(512));
  auto *texels = static_cast<uint8_t *>(upload->map());
  const uint8_t colors[2][4] = {{255, 64, 32, 255}, {32, 64, 255, 255}};
  std::memcpy(texels, colors[0], 4);
  std::memcpy(texels + 256, colors[1], 4);
  upload->unmap();
  std::array<std::unique_ptr<RenderTexture>, 2> textures;
  RenderDescriptorSetBuilder texture_layout;
  texture_layout.begin(); texture_layout.addTexture(0, 2); texture_layout.end();
  auto texture_set = texture_layout.create(device.get());
  for (uint32_t i = 0; i < 2; ++i) {
    textures[i] = device->createTexture(RenderTextureDesc::Texture2D(1, 1, 1, format));
    texture_set->setTexture(i, textures[i].get(), RenderTextureLayout::SHADER_READ);
  }
  auto constants = device->createBuffer(RenderBufferDesc::UploadBuffer(768, RenderBufferFlag::CONSTANT));
  auto *data = static_cast<uint8_t *>(constants->map());
  std::memset(data, 0, 768);
  const float white[4] = {1, 1, 1, 1};
  for (uint32_t i = 0; i < 2; ++i) {
    std::memcpy(data + 256 * (i + 1), white, sizeof(white));
    std::memcpy(data + 256 * (i + 1) + 16, &i, sizeof(i));
  }
  constants->unmap();
  RenderDescriptorSetBuilder constant_layout;
  constant_layout.begin(); constant_layout.addConstantBufferDynamic(3); constant_layout.end();
  auto constant_set = constant_layout.create(device.get());
  constant_set->setBuffer(0, constants.get(), 32);
  RenderPipelineLayoutBuilder layout_builder;
  layout_builder.begin(false, false);
  for (uint32_t i = 0; i < 5; ++i) layout_builder.addDescriptorSet(texture_layout);
  layout_builder.addDescriptorSet(constant_layout);
  layout_builder.end();
  auto layout = layout_builder.create(device.get());
  RenderGraphicsPipelineDesc description;
  description.pipelineLayout = layout.get();
  description.vertexShader = vs.get(); description.pixelShader = ps.get();
  description.renderTargetFormat[0] = format;
  description.renderTargetBlend[0] = RenderBlendDesc::Copy();
  description.renderTargetCount = 1;
  description.primitiveTopology = RenderPrimitiveTopology::TRIANGLE_LIST;
  auto pipeline = device->createGraphicsPipeline(description);
  if (!renut::gpu::MetalPipelineCreated(pipeline.get())) return 3;
  auto readback = device->createBuffer(RenderBufferDesc::ReadbackBuffer(width * height * 4));
  auto clear_readback = device->createBuffer(RenderBufferDesc::ReadbackBuffer(width * height * 4));
  commands->begin();
  for (uint32_t i = 0; i < 2; ++i) {
    commands->copyTextureRegion(RenderTextureCopyLocation::Subresource(textures[i].get()),
        RenderTextureCopyLocation::PlacedFootprint(upload.get(), format, 1, 1, 1, 64, i * 256), 0, 0, 0);
  }
  commands->setFramebuffer(framebuffer.get());
  commands->clearColor(0, RenderColor(0.25f, 0.5f, 0.75f, 1));
  commands->copyTextureRegion(
      RenderTextureCopyLocation::PlacedFootprint(clear_readback.get(), format, width, height, 1, width),
      RenderTextureCopyLocation::Subresource(target.get()), 0, 0, 0);
  commands->setGraphicsPipelineLayout(layout.get());
  commands->setPipeline(pipeline.get());
  commands->setGraphicsDescriptorSet(texture_set.get(), 0);
  commands->setViewports(RenderViewport(0, 0, width, height));
  for (uint32_t i = 0; i < 2; ++i) {
    uint32_t offset = 256 * (i + 1);
    commands->setGraphicsDescriptorSetDynamic(constant_set.get(), 5, &offset, 1);
    commands->setScissors(RenderRect(i * width / 2, 0, (i + 1) * width / 2, height));
    commands->drawInstanced(3, 1, 0, 0);
  }
  commands->copyTextureRegion(
      RenderTextureCopyLocation::PlacedFootprint(readback.get(), format, width, height, 1, width),
      RenderTextureCopyLocation::Subresource(target.get()), 0, 0, 0);
  commands->end();
  queue->executeCommandLists(commands.get(), fence.get());
  queue->waitForCommandFence(fence.get());
  const auto *pixels = static_cast<const uint8_t *>(readback->map());
  size_t failures = 0;
  for (uint32_t y = 0; y < height; ++y)
    for (uint32_t x = 0; x < width; ++x)
      failures += std::memcmp(pixels + (y * width + x) * 4, colors[x >= width / 2], 4) != 0;
  readback->unmap();
  const uint8_t clear_color[4] = {64, 128, 191, 255};
  pixels = static_cast<const uint8_t *>(clear_readback->map());
  for (uint32_t i = 0; i < width * height; ++i)
    failures += std::memcmp(pixels + i * 4, clear_color, 4) != 0;
  clear_readback->unmap();
  std::cout << "Pixel verification: " << 2 * width * height << " pixels (clear and draw), " << failures << " failures\n";
  return failures ? 4 : 0;
}
