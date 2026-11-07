#include "renut_engine/gpu/backend/metal_backend.h"

#include <plume_metal.h>
#include <spirv_msl.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>

namespace plume {
std::unique_ptr<RenderInterface> CreateMetalInterface();
}

namespace sc = SPIRV_CROSS_NAMESPACE;

namespace renut::gpu {
namespace {
struct AutoreleasePool {
  NS::AutoreleasePool *pool = NS::AutoreleasePool::alloc()->init();
  ~AutoreleasePool() { pool->release(); }
};

// Plume's Tier 2 argument buffers use one 64-bit resource ID/address per
// descriptor slot. MSL id attributes alone do not reserve gaps in that ABI.
std::string PadArgumentBuffers(const std::string &source,
                              const std::map<std::pair<uint32_t, uint32_t>, uint32_t> &counts) {
  std::istringstream input(source);
  std::ostringstream output;
  const std::regex set_re(R"(struct spvDescriptorSetBuffer([0-9]+))");
  const std::regex id_re(R"(\[\[id\(([0-9]+)\)\]\])");
  std::string line;
  int set = -1;
  uint32_t next = 0;
  while (std::getline(input, line)) {
    std::smatch match;
    if (std::regex_search(line, match, set_re)) {
      set = std::stoi(match[1]);
      next = 0;
    } else if (set >= 0 && line == "};") {
      set = -1;
    } else if (set >= 0 && std::regex_search(line, match, id_re)) {
      const uint32_t id = std::stoul(match[1]);
      if (id < next)
        throw std::runtime_error("overlapping Metal argument buffer bindings");
      while (next < id) {
        output << "    constant uint* los_padding_" << next << " [[id(" << next << ")]];\n";
        ++next;
      }
      const auto count = counts.find({uint32_t(set), id});
      next = id + (count == counts.end() ? 1 : count->second);
    }
    output << line << '\n';
  }
  return output.str();
}
}

std::unique_ptr<plume::RenderInterface> CreateNativeMetalInterface() {
  return plume::CreateMetalInterface();
}

std::unique_ptr<plume::RenderShader> CreateMetalShader(plume::RenderDevice *device,
    const void *spirv, size_t size, const char *name) {
  if (!device || !spirv || size < 20 || size % 4)
    return nullptr;
  try {
    AutoreleasePool pool;
    sc::CompilerMSL compiler(static_cast<const uint32_t *>(spirv), size / 4);
    const auto stage = compiler.get_execution_model();
    if (stage == spv::ExecutionModelGeometry)
      throw std::runtime_error("geometry shaders require primitive expansion on Metal");
    sc::CompilerMSL::Options options;
    options.msl_version = sc::CompilerMSL::Options::make_msl_version(3, 0);
    options.argument_buffers = true;
    options.argument_buffers_tier = sc::CompilerMSL::Options::ArgumentBuffersTier::Tier2;
    options.texture_buffer_native = true;
    options.enable_decoration_binding = true;
    compiler.set_msl_options(options);
    auto common = compiler.get_common_options();
    // Our existing DXC SPIR-V cache flips clip-space Y for Vulkan. Restore it
    // for Metal, which uses the D3D clip-space convention.
    common.vertex.flip_vert_y = true;
    compiler.set_common_options(common);

    for (uint32_t set = 0; set < 8; ++set) {
      sc::MSLResourceBinding binding;
      binding.stage = stage;
      binding.desc_set = set;
      binding.binding = sc::kArgumentBufferBinding;
      binding.msl_buffer = set;
      compiler.add_msl_resource_binding(binding);
    }
    sc::MSLResourceBinding push;
    push.stage = stage;
    push.desc_set = sc::kPushConstDescSet;
    push.binding = sc::kPushConstBinding;
    push.msl_buffer = 8;
    compiler.add_msl_resource_binding(push);

    std::map<std::pair<uint32_t, uint32_t>, uint32_t> counts;
    const auto resources = compiler.get_shader_resources();
    auto bind = [&](const auto &list) {
      for (const auto &resource : list) {
        const auto &type = compiler.get_type(resource.type_id);
        const uint32_t set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
        const uint32_t slot = compiler.get_decoration(resource.id, spv::DecorationBinding);
        uint32_t count = type.array.empty() ? 1 : type.array.front();
        if (!count)
          count = type.basetype == sc::SPIRType::Sampler ? 256 : 8192;
        sc::MSLResourceBinding binding;
        binding.stage = stage;
        binding.desc_set = set;
        binding.binding = slot;
        binding.basetype = type.basetype;
        binding.count = count;
        binding.msl_buffer = slot;
        binding.msl_texture = slot;
        binding.msl_sampler = slot;
        compiler.add_msl_resource_binding(binding);
        counts[{set, slot}] = count;
      }
    };
    bind(resources.uniform_buffers);
    bind(resources.storage_buffers);
    bind(resources.separate_images);
    bind(resources.separate_samplers);
    bind(resources.storage_images);

    const auto source = PadArgumentBuffers(compiler.compile(), counts);
    const auto entry = compiler.get_cleansed_entry_point_name("main", stage);
    NS::Error *error = nullptr;
    auto options_mtl = MTL::CompileOptions::alloc()->init();
    options_mtl->setLanguageVersion(MTL::LanguageVersion3_0);
    options_mtl->setFastMathEnabled(false);
    auto library = static_cast<plume::MetalDevice *>(device)->mtl->newLibrary(
        NS::String::string(source.c_str(), NS::UTF8StringEncoding), options_mtl, &error);
    options_mtl->release();
    if (!library) {
      std::fprintf(stderr, "Metal shader %s: %s\n", name,
          error ? error->localizedDescription()->utf8String() : "compiler returned no library");
      if (const char *dump = std::getenv("LOS_METAL_SHADER_DUMP")) {
        std::filesystem::create_directories(dump);
        std::ofstream(std::filesystem::path(dump) / (std::string(name) + ".metal")) << source;
      }
      return nullptr;
    }
    auto shader = std::make_unique<plume::MetalShader>(library, entry.c_str());
    library->release();
    shader->setName(name);
    return shader;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "Metal shader %s: %s\n", name, error.what());
    return nullptr;
  }
}

bool MetalTextureCreated(const plume::RenderTexture *texture) {
  return texture && static_cast<const plume::MetalTexture *>(texture)->mtl;
}

bool MetalPipelineCreated(const plume::RenderPipeline *pipeline) {
  return pipeline && static_cast<const plume::MetalGraphicsPipeline *>(pipeline)->state.renderPipelineState;
}

GpuMemoryUsage QueryMetalMemory(plume::RenderDevice *device) {
  const auto native = static_cast<plume::MetalDevice *>(device)->mtl;
  GpuMemoryUsage usage;
  usage.valid = true;
  usage.local_used = native->currentAllocatedSize();
  usage.local_budget = native->recommendedMaxWorkingSetSize();
  return usage;
}

bool SetMetalBlendConstant(plume::RenderCommandList *cmd, const float rgba[4]) {
  static_cast<plume::MetalCommandList *>(cmd)->setBlendConstants(rgba);
  return true;
}
}
