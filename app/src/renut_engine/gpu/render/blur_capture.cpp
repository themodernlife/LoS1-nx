#include "renut_engine/gpu/render/blur_capture.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include <fmt/format.h>

#include "renut_engine/gpu/backend/backend.h"
#include "renut_engine/gpu/backend/video.h"
#include "renut_engine/gpu/log.h"

namespace renut::gpu {
namespace {

constexpr uint32_t kSamplesPerTrigger = 8;
constexpr uint32_t kMaxSamples = 32;
constexpr uint64_t kMaxBytes = 512ull * 1024 * 1024;
constexpr uint32_t kSlots[] = {0, 1, 2, 3};

struct CapturedTexture {
  uint32_t sampler = 0;
  uint32_t width = 0, height = 0, row_texels = 0;
  uint32_t descriptor = 0, guest_va = 0, view_swizzle = 0;
  uint32_t shader_channels[4] = {};
  bool swap_red_blue = false, gpu_written = false;
  uint32_t host_format = 0;
  std::string status;
  std::string storage;
  std::string filename;
  std::unique_ptr<plume::RenderBuffer> readback;
  std::vector<uint8_t> pixels;
};

struct Capture {
  BlurCaptureDraw draw;
  uint32_t sample = 0;
  std::filesystem::path directory;
  std::array<CapturedTexture, 4> textures;
};

struct CaptureState {
  std::filesystem::path directory, run_directory;
  std::chrono::steady_clock::time_point next_poll{};
  uint64_t next_frame = 0, bytes = 0;
  uint32_t remaining = 0, samples = 0;
  std::array<std::vector<Capture>, kFramesInFlight> pending;
};

CaptureState &Captures() {
  static CaptureState state = [] {
    CaptureState s;
    if (const char *directory = std::getenv("LOS_BLUR_CAPTURE"); directory && *directory) {
      s.directory = std::filesystem::path(directory);
      if (!s.directory.is_absolute()) {
        RGPU_WARN("[blurcapture] LOS_BLUR_CAPTURE must be an absolute output directory; disabled");
        s.directory.clear();
      } else {
        RGPU_INFO("[blurcapture] armed: create {}/trigger for 8 samples every 6 frames (32 total maximum)", directory);
      }
    }
    return s;
  }();
  return state;
}

std::string FourWords(const uint32_t words[4]) {
  return fmt::format("[\"{:08X}\",\"{:08X}\",\"{:08X}\",\"{:08X}\"]",
                     words[0], words[1], words[2], words[3]);
}

bool FindView(const HostTexture &texture, uint32_t descriptor, uint32_t &swizzle) {
  if (texture.slot == descriptor) {
    swizzle = texture.view_swizzle;
    return true;
  }
  for (const auto &view : texture.swizzle_views) {
    if (view.slot == descriptor) {
      swizzle = view.swizzle;
      return true;
    }
  }
  return false;
}

void WriteCapture(Capture &capture) {
  const auto &draw = capture.draw;
  std::string json = fmt::format(
      "{{\n  \"schema\":1,\n  \"sample\":{},\n  \"frame\":{},\n"
      "  \"pixel_shader\":\"7838FCCDD9A82460\",\n  \"target\":{{\"width\":{},\"height\":{},\"scale\":{}}},\n"
      "  \"pixel_constants_bits\":{{",
      capture.sample, draw.frame, draw.width, draw.height, draw.scale);
  for (uint32_t reg = 0; reg < 16; ++reg)
    json += fmt::format("{}\"c{}\":{}", reg ? "," : "", reg, FourWords(draw.constants[reg]));
  json += fmt::format(
      ",\"c253\":{}}},\n  \"storage\":\"tightly packed rows, top to bottom; see per-texture storage; RGBA alpha preserved\",\n"
      "  \"shader_channels_encoding\":\"0=stored R,1=G,2=B,3=A,4=zero,5=one\",\n  \"textures\":[\n",
      FourWords(draw.c253));

  for (uint32_t index = 0; index < capture.textures.size(); ++index) {
    CapturedTexture &texture = capture.textures[index];
    if (!texture.pixels.empty()) {
      std::ofstream out(capture.directory / texture.filename, std::ios::binary);
      out.write(reinterpret_cast<const char *>(texture.pixels.data()), std::streamsize(texture.pixels.size()));
      out.close();
      texture.status = out ? "ok" : "write_failed";
    }
    const auto &fetch = draw.fetches[texture.sampler];
    json += fmt::format(
        "    {{\"sampler\":{},\"status\":\"{}\",\"file\":\"{}\",\"storage\":\"{}\",\"width\":{},\"height\":{},"
        "\"guest_texture_va\":\"{:08X}\",\"host_texture_va\":\"{:08X}\",\"descriptor\":{},"
        "\"texture_word\":\"{:08X}\",\"host_format\":{},\"gpu_written\":{},"
        "\"view_swizzle\":\"{:03X}\",\"view_swap_red_blue\":{},\"shader_channels\":[{},{},{},{}],"
        "\"fetch_words\":[\"{:08X}\",\"{:08X}\",\"{:08X}\",\"{:08X}\",\"{:08X}\",\"{:08X}\"]}}{}\n",
        texture.sampler, texture.status, texture.filename, texture.storage, texture.width, texture.height,
        draw.texture_va[texture.sampler], texture.guest_va, texture.descriptor,
        draw.texture_words[texture.sampler], texture.host_format, texture.gpu_written,
        texture.view_swizzle, texture.swap_red_blue, texture.shader_channels[0], texture.shader_channels[1],
        texture.shader_channels[2], texture.shader_channels[3], fetch.dword[0], fetch.dword[1], fetch.dword[2],
        fetch.dword[3], fetch.dword[4], fetch.dword[5], index + 1 == capture.textures.size() ? "" : ",");
  }
  json += "  ]\n}\n";
  const std::string filename = fmt::format("sample-{:02}-frame-{:08}.json", capture.sample, draw.frame);
  std::ofstream metadata(capture.directory / filename);
  metadata << json;
  metadata.close();
  if (!metadata)
    RGPU_WARN("[blurcapture] metadata write failed for {}", filename);
  else
    RGPU_INFO("[blurcapture] wrote sample {} frame {} to {}", capture.sample, draw.frame,
              capture.directory.string());
}

// CPU data only reaches this worker. Its destructor drains and joins before the
// earlier-created capture state is destroyed, and the thread never uses the GPU.
class CaptureWriter {
 public:
  CaptureWriter() : thread_([this] { Run(); }) {}
  ~CaptureWriter() {
    {
      std::lock_guard lock(mutex_);
      stopping_ = true;
    }
    ready_.notify_one();
    thread_.join();
  }

  bool Enqueue(Capture &&capture) {
    std::lock_guard lock(mutex_);
    if (queue_.size() >= 2)
      return false;
    queue_.push_back(std::move(capture));
    ready_.notify_one();
    return true;
  }

 private:
  void Run() {
    for (;;) {
      Capture capture;
      {
        std::unique_lock lock(mutex_);
        ready_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
        if (queue_.empty())
          return;
        capture = std::move(queue_.front());
        queue_.pop_front();
      }
      WriteCapture(capture);
    }
  }

  std::mutex mutex_;
  std::condition_variable ready_;
  std::deque<Capture> queue_;
  bool stopping_ = false;
  std::thread thread_;
};

} // namespace

bool BlurCaptureDueLocked(uint64_t frame) {
  auto &capture = Captures();
  if (capture.directory.empty() || capture.samples >= kMaxSamples || IsDeko3D())
    return false;
  if (capture.remaining)
    return frame >= capture.next_frame;
  const auto now = std::chrono::steady_clock::now();
  if (now < capture.next_poll)
    return false;
  capture.next_poll = now + std::chrono::milliseconds(250);
  std::error_code error;
  const auto trigger = capture.directory / "trigger";
  if (!std::filesystem::is_regular_file(trigger, error) || error)
    return false;
  if (capture.run_directory.empty()) {
    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    capture.run_directory = capture.directory / fmt::format("run-{}", stamp);
    if (!std::filesystem::create_directory(capture.run_directory, error) || error) {
      RGPU_WARN("[blurcapture] cannot create capture directory {}", capture.run_directory.string());
      capture.run_directory.clear();
      return false;
    }
  }
  if (!std::filesystem::remove(trigger, error) || error)
    return false;
  capture.remaining = std::min(kSamplesPerTrigger, kMaxSamples - capture.samples);
  capture.next_frame = frame;
  RGPU_INFO("[blurcapture] trigger consumed at frame {}; recording {} samples", frame, capture.remaining);
  return true;
}

bool QueueBlurCaptureLocked(const BlurCaptureDraw &draw, const BindEffects effects[32]) {
  auto &capture = Captures();
  auto &video = State();
  if (!capture.remaining || draw.frame < capture.next_frame || !video.command_list)
    return false;
  Capture batch;
  batch.draw = draw;
  batch.directory = capture.run_directory;
  batch.sample = ++capture.samples;
  --capture.remaining;
  capture.next_frame = draw.frame + 6;
  bool copied = false;
  for (uint32_t index = 0; index < batch.textures.size(); ++index) {
    auto &image = batch.textures[index];
    image.sampler = kSlots[index];
    image.descriptor = draw.texture_words[image.sampler] & ((1u << kResolutionScaleShift) - 1);
    image.status = "no_matching_host_view";
    const BindEffects &fx = effects[image.sampler];
    if (fx.handover_surface || fx.handover_image || fx.words_viewed) {
      image.status = "unsupported_alias";
      continue;
    }
    HostTexture *source = nullptr;
    for (HostTexture *candidate : fx.transition) {
      if (candidate && FindView(*candidate, image.descriptor, image.view_swizzle)) {
        source = candidate;
        break;
      }
    }
    if (!source || !source->texture)
      continue;
    image.guest_va = source->guest_va;
    image.width = source->alloc_width;
    image.height = source->alloc_height;
    image.swap_red_blue = source->view_swap_red_blue;
    image.gpu_written = source->gpu_written;
    image.host_format = uint32_t(source->host_format);
    const bool depth_float = source->host_format == plume::RenderFormat::R32_FLOAT;
    image.storage = depth_float ? "r32_float_little_endian" : "rgba8_unorm";
    for (uint32_t channel = 0; channel < 4; ++channel) {
      uint32_t component = (image.view_swizzle >> (channel * 3)) & 7;
      if (depth_float && component < 4)
        component = 0;
      else if (image.swap_red_blue && (component == 0 || component == 2))
        component ^= 2;
      image.shader_channels[channel] = component < 5 ? component : 5;
    }
    if (source->dimension != TextureDimension::Tex2D || source->stacked ||
        (source->host_format != plume::RenderFormat::R8G8B8A8_UNORM && !depth_float) ||
        !image.width || !image.height) {
      image.status = "unsupported_format_or_dimension";
      continue;
    }
    image.row_texels = (image.width + 63u) & ~63u;
    const uint64_t bytes = uint64_t(image.row_texels) * image.height * 4;
    if (bytes > 32ull * 1024 * 1024 || bytes > kMaxBytes - capture.bytes) {
      image.status = "byte_limit";
      continue;
    }
    image.readback = video.device->createBuffer(plume::RenderBufferDesc::ReadbackBuffer(bytes));
    if (!image.readback) {
      image.status = "allocation_failed";
      continue;
    }
    capture.bytes += bytes;
    image.filename = fmt::format("sample-{:02}-frame-{:08}-s{}.{}", batch.sample, draw.frame, image.sampler,
                                 depth_float ? "f32" : "rgba");
    TransitionTextureLocked(source, plume::RenderTextureLayout::COPY_SOURCE);
    const plume::RenderBox box(0, 0, int32_t(image.width), int32_t(image.height));
    video.command_list->copyTextureRegion(
        plume::RenderTextureCopyLocation::PlacedFootprint(image.readback.get(), source->host_format,
                                                         image.width, image.height, 1, image.row_texels),
        plume::RenderTextureCopyLocation::Subresource(source->texture.get()), 0, 0, 0, &box);
    TransitionTextureLocked(source, plume::RenderTextureLayout::SHADER_READ);
    image.status = "pending";
    copied = true;
  }
  capture.pending[video.frame].push_back(std::move(batch));
  return copied;
}

void CompleteBlurCapturesLocked(uint32_t frame_slot) {
  auto &capture = Captures();
  if (frame_slot >= capture.pending.size() || capture.pending[frame_slot].empty())
    return;
  static CaptureWriter writer;
  for (Capture &batch : capture.pending[frame_slot]) {
    for (CapturedTexture &image : batch.textures) {
      if (!image.readback)
        continue;
      if (const auto *data = static_cast<const uint8_t *>(image.readback->map())) {
        image.pixels.resize(size_t(image.width) * image.height * 4);
        for (uint32_t y = 0; y < image.height; ++y)
          std::memcpy(image.pixels.data() + size_t(y) * image.width * 4,
                      data + size_t(y) * image.row_texels * 4, size_t(image.width) * 4);
        image.readback->unmap();
        image.status = "ok";
      } else {
        image.status = "map_failed";
      }
      image.readback.reset();
    }
    const auto sample = batch.sample;
    if (!writer.Enqueue(std::move(batch)))
      RGPU_WARN("[blurcapture] dropped sample {}: bounded disk writer queue is full", sample);
  }
  capture.pending[frame_slot].clear();
}

} // namespace renut::gpu
