# Test media

Synthetic clips for the media library tests (#97). They are made with ffmpeg's `testsrc`
pattern, so there is no church or copyrighted content in them. Each is a few KB. They're
committed because CI has no ffmpeg. `.gitignore` keeps all other video out of the repo.

To regenerate them, run this from this folder:

```sh
ffmpeg -hide_banner -y -f lavfi -i "testsrc=size=320x180:rate=30:duration=2" \
    -c:v libx264 -pix_fmt yuv420p -preset veryslow -crf 35 -movflags +faststart -an \
    h264-320x180-30fps-2s.mp4
ffmpeg -hide_banner -y -f lavfi -i "testsrc=size=160x90:rate=25:duration=1" \
    -c:v mpeg4 -q:v 20 -an mpeg4-160x90-25fps-1s.mp4
```

- `h264-320x180-30fps-2s.mp4`: H.264, 320×180, 30 fps, 2 s. This is the normal case.
- `mpeg4-160x90-25fps-1s.mp4`: MPEG-4 Part 2, 160×90, 25 fps, 1 s. No Mac decodes it in
  hardware, so importing it must warn.
