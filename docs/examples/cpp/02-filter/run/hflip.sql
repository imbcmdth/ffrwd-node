CREATE FUNCTION blend(v video_stream, over video_stream, mix number DEFAULT 0.5)
RETURNS video_stream
  AS 'blend.wasm', 'blend' LANGUAGE wasm;

COPY (
  SELECT blend(f.video[1], hflip(f.video[1]))
  FROM input('testsrc.mp4') f
) TO 'mirrored.mp4' WITH (video_codec 'libx264')
