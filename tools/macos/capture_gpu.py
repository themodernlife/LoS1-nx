#!/usr/bin/env python3
"""Launch isolated MoltenVK capture sessions, or trigger a running session."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import time

REPO = Path(__file__).resolve().parents[2]


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def frame_count(value):
    count = int(value)
    if not 1 <= count <= 120:
        raise argparse.ArgumentTypeError("frame count must be between 1 and 120")
    return count


def trigger(args):
    session = json.loads(args.session.read_text())
    os.kill(session["pid"], 0)
    if Path(session["trace"]).exists():
        raise SystemExit("Trace already exists. Start another session or archive the completed trace.")
    fifo = Path(session["fifo"])
    if not stat.S_ISFIFO(fifo.stat().st_mode):
        raise SystemExit("Capture pipe is not a FIFO")
    fd = os.open(fifo, os.O_WRONLY | os.O_NONBLOCK)
    try:
        if os.write(fd, bytes(args.frames)) != args.frames:
            raise SystemExit("Incomplete capture request")
    finally:
        os.close(fd)
    print(f"Requested {args.frames} frames: {session['trace']}")


def launch(args):
    executable = args.app.resolve() / "Contents/MacOS/los"
    if not executable.is_file() or not args.profile.is_dir():
        raise SystemExit("Provide an existing game app and user-data profile")
    run = REPO / "out/gpu-captures" / datetime.datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    run.mkdir(parents=True)
    # Retain the starting checkpoint separately from the game's writable copy.
    shutil.copytree(args.profile, run / "checkpoint")
    shutil.copytree(run / "checkpoint", run / "user-data")
    helper = run / "capture-hotkey"
    subprocess.run(["xcrun", "clang", "-fobjc-arc", "-framework", "Cocoa", "-framework",
                    "Carbon", str(Path(__file__).with_name("capture_hotkey.m")),
                    "-o", str(helper)], check=True)
    trace = run / "scene.gputrace"
    env = os.environ.copy()
    env.update(MTL_CAPTURE_ENABLED="1", METAL_CAPTURE_ENABLED="1",
               MVK_CONFIG_AUTO_GPU_CAPTURE_SCOPE="3",
               MVK_CONFIG_AUTO_GPU_CAPTURE_OUTPUT_FILE=str(trace))
    argv = [str(executable), "--sa2_renderer=renut", "--no-fullscreen",
            f"--user_data_root={run / 'user-data'}"]
    with (run / "game.log").open("w") as log:
        game = subprocess.Popen(argv, cwd=executable.parent, env=env, stdout=log,
                                stderr=subprocess.STDOUT, start_new_session=True)
    fifo = None
    for _ in range(100):
        if game.poll() is not None:
            raise SystemExit(f"Game exited; see {run / 'game.log'}")
        files = subprocess.run(["/usr/sbin/lsof", "-a", "-p", str(game.pid), "-Fn"],
                               capture_output=True, text=True).stdout.splitlines()
        pipes = {line[1:] for line in files if line.startswith("n") and
                 "/MoltenVKCapturePipe-" in line}
        if len(pipes) == 1:
            fifo = pipes.pop()
            break
        if len(pipes) > 1:
            raise SystemExit(f"Multiple capture devices; inspect {run / 'game.log'}")
        time.sleep(0.2)
    if fifo is None:
        raise SystemExit(f"No on-demand capture pipe; game PID {game.pid}, log {run / 'game.log'}")
    with (run / "hotkey.log").open("w") as log:
        hotkey = subprocess.Popen([str(helper), fifo, str(trace), str(args.frames), str(game.pid)],
                                  stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
    session = dict(pid=game.pid, hotkey_pid=hotkey.pid, fifo=fifo, trace=str(trace),
                   args=argv, executable_sha256=digest(executable), frames=args.frames,
                   checkpoint_source=str(args.profile.resolve()),
                   checkpoint_sha256={str(p.relative_to(run / "checkpoint")): digest(p)
                                      for p in (run / "checkpoint").rglob("*") if p.is_file()},
                   capture_environment={k: env[k] for k in (
                       "MTL_CAPTURE_ENABLED", "METAL_CAPTURE_ENABLED",
                       "MVK_CONFIG_AUTO_GPU_CAPTURE_SCOPE", "MVK_CONFIG_AUTO_GPU_CAPTURE_OUTPUT_FILE")})
    (run / "session.json").write_text(json.dumps(session, indent=2) + "\n")
    time.sleep(0.5)
    print(run / "session.json")
    print((run / "hotkey.log").read_text())
    if hotkey.poll() is not None:
        raise SystemExit("Hotkey helper failed; CLI trigger remains available")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    start = sub.add_parser("launch")
    start.add_argument("--app", type=Path, required=True)
    start.add_argument("--profile", type=Path, required=True)
    start.add_argument("--frames", type=frame_count, default=60)
    start.set_defaults(func=launch)
    capture = sub.add_parser("trigger")
    capture.add_argument("session", type=Path)
    capture.add_argument("--frames", type=frame_count, default=60)
    capture.set_defaults(func=trigger)
    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
