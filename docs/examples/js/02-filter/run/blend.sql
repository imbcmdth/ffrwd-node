CREATE FUNCTION zoom(v video_stream, amount number DEFAULT 2,
                     x number DEFAULT 0.5, y number DEFAULT 0.5)
RETURNS video_stream
  AS 'zoom.wasm', 'zoom' LANGUAGE wasm;

CREATE FUNCTION blend(v video_stream, over video_stream, mix number DEFAULT 0.5)
RETURNS video_stream
  AS 'blend.wasm', 'blend' LANGUAGE wasm;

COPY (
  SELECT blend(f.video[1], zoom(f.video[1], 4), mix => 0.3)
  FROM input('testsrc.mp4') f
) TO 'blended.mp4' WITH (video_codec 'libx264')
