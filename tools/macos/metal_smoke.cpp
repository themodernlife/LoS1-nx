// Verifies the actual Renut SPIR-V -> MSL path, descriptor arrays, per-draw
// constant offsets, deferred/rectangle clears, adaptive blur, and plume readback.
#include "renut_engine/gpu/backend/metal_backend.h"
#include <plume_render_interface_builders.h>
#include "metal_smoke_vs.h"
#include "metal_smoke_ps.h"
#include "metal_blur_smoke_ps.h"
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
  auto blur_ps = renut::gpu::CreateMetalShader(device.get(), smoke_blur_ps, sizeof(smoke_blur_ps), "smoke_blur_ps");
  if (!vs || !ps || !blur_ps) return 2;
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
  constexpr uint32_t constant_bytes = 6 * 256;
  auto constants = device->createBuffer(RenderBufferDesc::UploadBuffer(constant_bytes, RenderBufferFlag::CONSTANT));
  auto *data = static_cast<uint8_t *>(constants->map());
  std::memset(data, 0, constant_bytes);
  const float white[4] = {1, 1, 1, 1};
  for (uint32_t i = 0; i < 2; ++i) {
    std::memcpy(data + 256 * (i + 1), white, sizeof(white));
    std::memcpy(data + 256 * (i + 1) + 16, &i, sizeof(i));
  }
  for (uint32_t mode = 0; mode < 3; ++mode) {
    const float step_x = mode == 1 ? 4.0f / width : mode == 2 ? 4.0f : 0.0f;
    const float parameters[4] = {step_x, mode == 2 ? 4.0f : 0.0f, 1.0f / width, 1.0f / height};
    const uint32_t texture_index = mode == 2 ? 1 : 0;
    std::memcpy(data + 256 * (mode + 3), parameters, sizeof(parameters));
    std::memcpy(data + 256 * (mode + 3) + 16, &texture_index, sizeof(texture_index));
  }
  constants->unmap();
  RenderDescriptorSetBuilder constant_layout;
  constant_layout.begin(); constant_layout.addConstantBufferDynamic(3); constant_layout.end();
  auto constant_set = constant_layout.create(device.get());
  constant_set->setBuffer(0, constants.get(), 32);
  RenderDescriptorSetBuilder sampler_layout;
  sampler_layout.begin(); sampler_layout.addSampler(0); sampler_layout.end();
  auto sampler_set = sampler_layout.create(device.get());
  RenderSamplerDesc sampler_description;
  sampler_description.minFilter = sampler_description.magFilter = RenderFilter::LINEAR;
  sampler_description.addressU = sampler_description.addressV = sampler_description.addressW = RenderTextureAddressMode::CLAMP;
  auto linear_clamp = device->createSampler(sampler_description);
  if (!sampler_set || !linear_clamp) return 2;
  sampler_set->setSampler(0, linear_clamp.get());
  RenderPipelineLayoutBuilder layout_builder;
  layout_builder.begin(false, false);
  for (uint32_t i = 0; i < 5; ++i)
    layout_builder.addDescriptorSet(i == 3 ? sampler_layout : texture_layout);
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

  // A rectangle clear uses an internal draw. The next draw must recover the
  // requested pipeline, viewport, scissor and depth state even when the caller
  // does not rebind them. Keep one encoder across draw -> clear -> draw: a copy
  // or framebuffer change between them would hide stale cached Metal state.
  for (uint32_t mode = 0; mode < 3; ++mode) {
    auto color_target = device->createTexture(RenderTextureDesc::ColorTarget(width, height, format));
    auto depth_target = mode ? device->createTexture(
        RenderTextureDesc::DepthTarget(width, height, RenderFormat::D32_FLOAT)) : nullptr;
    const RenderTexture *color_attachment = color_target.get();
    RenderFramebufferDesc clear_fb(&color_attachment, 1, depth_target.get());
    auto clear_framebuffer = device->createFramebuffer(clear_fb);
    auto clear_description = description;
    if (mode) {
      clear_description.depthTargetFormat = RenderFormat::D32_FLOAT;
      clear_description.depthEnabled = true;
      clear_description.depthWriteEnabled = true;
      clear_description.depthFunction = RenderComparisonFunction::LESS;
    }
    auto clear_pipeline = device->createGraphicsPipeline(clear_description);
    if (!renut::gpu::MetalPipelineCreated(clear_pipeline.get())) return 3;
    commands->begin();
    commands->setFramebuffer(clear_framebuffer.get());
    commands->clearColor(0, RenderColor(0, 0, 0, 1));
    if (mode) commands->clearDepthStencil(true, false, 1, 0);
    commands->setGraphicsPipelineLayout(layout.get());
    commands->setPipeline(clear_pipeline.get());
    commands->setGraphicsDescriptorSet(texture_set.get(), 0);
    uint32_t offset = 256;
    commands->setGraphicsDescriptorSetDynamic(constant_set.get(), 5, &offset, 1);
    commands->setViewports(RenderViewport(8, 8, 48, 48));
    commands->setScissors(RenderRect(16, 16, 48, 48));
    commands->drawInstanced(3, 1, 0, 0);
    const RenderRect whole(0, 0, width, height);
    if (mode == 0) {
      commands->clearColor(0, RenderColor(0, 1, 0, 1), &whole, 1);
    } else {
      commands->clearDepthStencil(true, false, mode == 1 ? 1.0f : 0.0f, 0, &whole, 1);
      offset = 512;
      commands->setGraphicsDescriptorSetDynamic(constant_set.get(), 5, &offset, 1);
    }
    commands->drawInstanced(3, 1, 0, 0);
    commands->copyTextureRegion(
        RenderTextureCopyLocation::PlacedFootprint(readback.get(), format, width, height, 1, width),
        RenderTextureCopyLocation::Subresource(color_target.get()));
    commands->end();
    queue->executeCommandLists(commands.get(), fence.get());
    queue->waitForCommandFence(fence.get());
    pixels = static_cast<const uint8_t *>(readback->map());
    const uint8_t background[4] = {0, uint8_t(mode == 0 ? 255 : 0), 0, 255};
    size_t clear_failures = 0;
    for (uint32_t y = 0; y < height; ++y)
      for (uint32_t x = 0; x < width; ++x) {
        const bool inside = x >= 16 && x < 48 && y >= 16 && y < 48;
        const auto *expected = inside ? colors[mode == 1 ? 1 : 0] : background;
        clear_failures += std::memcmp(pixels + (y * width + x) * 4, expected, 4) != 0;
      }
    readback->unmap();
    const char *test = mode == 0 ? "color rectangle clear" :
                       mode == 1 ? "depth rectangle clear/pass" : "depth rectangle clear/reject";
    std::cout << test << ": " << clear_failures << " pixel failures\n";
    failures += clear_failures;
  }
  // A one-pixel white column exposes the old sparse kernel's separated copies.
  // At four pixels per original tap, the shared helper must cover all 41 pixels
  // of the same shutter interval, while zero velocity leaves the column sharp.
  // The constant image also checks normalization and clamp sampling at the cap.
  constexpr uint32_t image_bytes = width * height * 4;
  auto blur_upload = device->createBuffer(RenderBufferDesc::UploadBuffer(2 * image_bytes));
  auto *blur_texels = static_cast<uint8_t *>(blur_upload->map());
  for (uint32_t y = 0; y < height; ++y)
    for (uint32_t x = 0; x < width; ++x) {
      auto *column = blur_texels + (y * width + x) * 4;
      column[0] = column[1] = column[2] = x == 32 ? 255 : 0;
      column[3] = 255;
      std::memcpy(blur_texels + image_bytes + (y * width + x) * 4, colors[0], 4);
    }
  blur_upload->unmap();
  std::array<std::unique_ptr<RenderTexture>, 2> blur_textures;
  auto blur_texture_set = texture_layout.create(device.get());
  for (uint32_t i = 0; i < blur_textures.size(); ++i) {
    blur_textures[i] = device->createTexture(RenderTextureDesc::Texture2D(width, height, 1, format));
    blur_texture_set->setTexture(i, blur_textures[i].get(), RenderTextureLayout::SHADER_READ);
  }
  auto blur_description = description;
  blur_description.pixelShader = blur_ps.get();
  auto blur_pipeline = device->createGraphicsPipeline(blur_description);
  if (!renut::gpu::MetalPipelineCreated(blur_pipeline.get())) return 3;
  for (uint32_t mode = 0; mode < 3; ++mode) {
    commands->begin();
    if (mode == 0) {
      for (uint32_t i = 0; i < blur_textures.size(); ++i) {
        commands->barriers(RenderBarrierStage::COPY,
            RenderTextureBarrier(blur_textures[i].get(), RenderTextureLayout::COPY_DEST));
        commands->copyTextureRegion(RenderTextureCopyLocation::Subresource(blur_textures[i].get()),
            RenderTextureCopyLocation::PlacedFootprint(blur_upload.get(), format, width, height, 1, width,
                                                       i * image_bytes));
        commands->barriers(RenderBarrierStage::GRAPHICS,
            RenderTextureBarrier(blur_textures[i].get(), RenderTextureLayout::SHADER_READ));
      }
    }
    commands->setFramebuffer(framebuffer.get());
    commands->clearColor(0, RenderColor(0, 0, 0, 0));
    commands->setGraphicsPipelineLayout(layout.get());
    commands->setPipeline(blur_pipeline.get());
    commands->setGraphicsDescriptorSet(blur_texture_set.get(), 0);
    commands->setGraphicsDescriptorSet(sampler_set.get(), 3);
    const uint32_t offset = 256 * (mode + 3);
    commands->setGraphicsDescriptorSetDynamic(constant_set.get(), 5, &offset, 1);
    commands->setViewports(RenderViewport(0, 0, width, height));
    commands->setScissors(RenderRect(0, 0, width, height));
    commands->drawInstanced(3, 1, 0, 0);
    commands->copyTextureRegion(
        RenderTextureCopyLocation::PlacedFootprint(readback.get(), format, width, height, 1, width),
        RenderTextureCopyLocation::Subresource(target.get()));
    commands->end();
    queue->executeCommandLists(commands.get(), fence.get());
    queue->waitForCommandFence(fence.get());
    pixels = static_cast<const uint8_t *>(readback->map());
    size_t blur_failures = 0;
    for (uint32_t y = 0; y < height; ++y)
      for (uint32_t x = 0; x < width; ++x) {
        const auto *pixel = pixels + (y * width + x) * 4;
        bool failed = pixel[3] != 255;
        for (uint32_t channel = 0; channel < 3; ++channel) {
          if (mode == 0) {
            failed |= pixel[channel] != (x == 32 ? 255 : 0);
          } else {
            // 255/41 rounds to 6; permit one quantization level of error.
            const int expected = mode == 2 ? colors[0][channel] : x >= 12 && x <= 52 ? 6 : 0;
            failed |= int(pixel[channel]) < expected - 1 || int(pixel[channel]) > expected + 1;
          }
        }
        blur_failures += failed;
      }
    readback->unmap();
    const char *test = mode == 0 ? "adaptive blur stationary column" :
                       mode == 1 ? "adaptive blur continuous 41-pixel trail" : "adaptive blur constant at cap/clamp";
    std::cout << test << ": " << blur_failures << " pixel failures\n";
    failures += blur_failures;
  }
  std::cout << "Pixel verification: " << 8 * width * height << " pixels, " << failures << " failures\n";
  return failures ? 4 : 0;
}
