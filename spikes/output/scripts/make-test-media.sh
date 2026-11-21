#!/usr/bin/env bash
# Generates the P1 test clips (plan 6.1) into spikes/output/media/, which is git-ignored.
# Synthetic patterns only, so no licensed media is needed or committed.
# testsrc shows a running counter, so a phone photo of two outputs shows whether they're in sync.
set -euo pipefail

cd "$(dirname "$0")/.."
mkdir -p media
seconds="${1:-20}"

make_clip() { # name size rate codec-args...
    local name="$1" size="$2" rate="$3"
    shift 3
    if [[ -f "media/$name" ]]; then
        echo "exists: media/$name"
        return
    fi
    echo "making: media/$name"
    ffmpeg -hide_banner -loglevel error -f lavfi -i "testsrc=size=${size}:rate=${rate}:duration=${seconds}" \
        -an "$@" "media/$name"
}

make_clip 1080p30-h264.mp4 1920x1080 30 -c:v libx264 -preset medium -crf 20 -pix_fmt yuv420p -movflags +faststart
make_clip 1080p60-h264.mp4 1920x1080 60 -c:v libx264 -preset medium -crf 20 -pix_fmt yuv420p -movflags +faststart
# hvc1 tag: AVFoundation and VideoToolbox won't play HEVC tagged hev1 in an MP4.
make_clip 4k30-hevc.mp4 3840x2160 30 -c:v libx265 -preset fast -crf 24 -pix_fmt yuv420p -tag:v hvc1 -movflags +faststart
make_clip 1080p30-prores.mov 1920x1080 30 -c:v prores_ks -profile:v 2 -pix_fmt yuv422p10le

ls -lh media
