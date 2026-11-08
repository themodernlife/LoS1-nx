#pragma once

#include <cstdint>

#include "renut_engine/gpu/render/textures.h"

namespace renut::gpu {

// Opt-in diagnostic, under State().mutex. LOS_BLUR_CAPTURE names an output
// directory; creating its "trigger" file requests eight samples six frames
// apart. At most 32 samples are recorded in one process.
bool BlurCaptureDueLocked(uint64_t frame);

struct BlurCaptureDraw {
  uint64_t frame = 0;
  uint32_t width = 0, height = 0;
  float scale = 1;
  uint32_t constants[16][4] = {};  // c0..15, host-endian IEEE float bits
  uint32_t c253[4] = {};          // including the shader's z/w literals
  TextureFetch fetches[4];
  uint32_t texture_va[4] = {};
  uint32_t texture_words[4] = {};
};

// Copies already-bound s0/s1/s3 RGBA8 and s2 R32_FLOAT storage without rebinding or submitting. The
// caller must run this before binding its draw framebuffer, and invalidate its
// framebuffer cache if true is returned (copies may end a render pass).
bool QueueBlurCaptureLocked(const BlurCaptureDraw &draw, const BindEffects effects[32]);

// Called only after the frame slot's existing fence has completed. Owns and
// releases each readback buffer here, so none is mapped while GPU work uses it.
void CompleteBlurCapturesLocked(uint32_t frame_slot);

} // namespace renut::gpu
