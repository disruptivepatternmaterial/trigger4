#!/usr/bin/env bash
# Build and upload the trigger4p AtomS3 firmware.
# Run from this firmware dir: ./scripts/build_and_upload_atoms3.sh [--no-upload] [--clean] [--port /dev/cu.usbmodemXXXX]

set -e
cd "$(dirname "$0")/.."

if command -v pio &>/dev/null; then
  PIO=pio
elif [ -x "$HOME/.platformio/penv/bin/pio" ]; then
  PIO="$HOME/.platformio/penv/bin/pio"
else
  PIO="python3 -m platformio"
fi
echo "Using: $PIO"

UPLOAD=1
CLEAN=0
PORT=""
while [ $# -gt 0 ]; do
  case "$1" in
    --no-upload) UPLOAD=0 ;;
    --clean)     CLEAN=1 ;;
    --port)      PORT="$2"; shift ;;
  esac
  shift
done

if [ "$CLEAN" -eq 1 ]; then
  echo "=== Clean ==="
  $PIO run -e m5atoms3 -t clean 2>/dev/null || true
fi

echo "=== Building (m5atoms3) ==="
$PIO run -e m5atoms3

if [ "$UPLOAD" -eq 1 ]; then
  echo "=== Uploading ==="
  if [ -n "$PORT" ]; then
    $PIO run -e m5atoms3 -t upload --upload-port "$PORT"
  else
    $PIO run -e m5atoms3 -t upload
  fi
else
  echo "=== Skipping upload (--no-upload) ==="
fi

echo "=== Done ==="
