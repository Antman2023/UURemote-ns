`red-16x16.h264` is a synthetic single-frame red image encoded for decoder regression tests. It contains no user or third-party media.

Generated with FFmpeg 9.0.2 and libx264:

```sh
ffmpeg -f lavfi -i color=c=red:s=16x16:r=1 -frames:v 1 -c:v libx264 -preset ultrafast -tune zerolatency -f h264 red-16x16.h264
```

The media test repeats this IDR access unit to exercise the threaded decoder, checks the resulting RGBA pixels, resets the connection generation, and injects allocation/scaler/Opus failures on GNU builds.
