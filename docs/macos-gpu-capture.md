# Capturing the motion-blur scene

Use an existing packaged app and a known checkpoint profile. Xcode and its
command-line tools are required. The bundled MoltenVK must support on-demand
capture (scope 3); the installed 1.4.1 library was verified locally.

1. From the repository, launch a capture session:

   ```sh
   python3 tools/macos/capture_gpu.py launch \
     --app 'out/ReNut Update.app' \
     --profile "$HOME/.local/share/los"
   ```

2. Continue the game to the scene. Press **Control+Option+B once**, just as
   Gabriel starts falling from the horse. The helper beeps when it requests
   capture. It records the next 60 rendered frames; it cannot capture the past.
   Recording can cause substantial stuttering and a large trace.

3. Wait for recording to finish. The launcher prints a session path under
   `out/gpu-captures/`. Its `game.log` reports the capture destination, and
   `scene.gputrace` contains GPU commands and resources rather than video.
   Open the completed trace in Xcode and click **Replay**. The test trace was
   verified by reaching Xcode's GPU workload summary, not merely by file creation.

The source profile is copied into `checkpoint/` and then into a separate
`user-data/` for play. To retry from the same starting save, quit the capture
game and relaunch with that session's `checkpoint/` as `--profile`. This preserves
the checkpoint but does not guarantee deterministic simulation or timing.

For a short smoke capture or an agent-triggered capture:

```sh
python3 tools/macos/capture_gpu.py trigger /absolute/path/to/session.json --frames 2
```

One trace is allowed per output path. After replay verification and closing the
trace in Xcode, it can be renamed to retain the smoke test and free
`scene.gputrace` for the real scene in the same session. A beep signals the
request, not completion. Check the hotkey log if registration fails.

GPU replay fixes the captured resource contents and commands for inspection and
shader experiments. A C++ renderer change does not alter an existing trace;
capture again to evaluate it. Keep captures local and out of Git because they
contain game resources.

References: [MoltenVK capture configuration](https://github.com/KhronosGroup/MoltenVK/blob/main/Docs/MoltenVK_Configuration_Parameters.md)
and [Xcode GPU replay](https://developer.apple.com/documentation/xcode/replaying-a-gpu-trace-file).

## Denser motion-blur experiment

`LOS_RENUT_DENSE_MOTION_BLUR` is an opt-in build option, off by default. It
changes only pixel shader `7838FCCDD9A82460` from 11 scene samples to 21 uniform
samples over the same endpoints. Motion values, blur distance, game-controlled
enablement, and output tests remain intact. It reduces spacing between samples;
it does not reduce the blur strength. More texture samples can cost GPU time.
The first 65-sample version looked acceptable to the user but caused a very low
frame rate. The 21-sample revision halves the original spacing with fewer reads
than that first experiment; its visual quality and performance need comparison.

Enable it in an existing configured build:

```sh
cmake -S app -B out/mac -DLOS_RENUT_DENSE_MOTION_BLUR=ON
cmake --build out/mac --target los -j 4
```

It uses the existing `BANJO_RENUT_EARLY_ALPHA=ON` HLSL override workflow.
`tools/renut_dense_motion_blur.py` checks the original sampling block's digest
before rewriting the generated HLSL. A changed translator requires review
instead of silently applying the experiment to an unfamiliar shader.

To restore the original 11-sample kernel, configure with
`-DLOS_RENUT_DENSE_MOTION_BLUR=OFF`, rebuild, and repackage the intended app.
Existing packaged apps keep their own embedded shader cache. Capture a new
trace to evaluate the executable's new shader; rebuilding does not change an
already recorded GPU trace.
