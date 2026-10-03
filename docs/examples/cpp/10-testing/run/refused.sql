CREATE FUNCTION levels(v video_stream, black number DEFAULT 16, white number DEFAULT 235)
RETURNS video_stream
  AS 'levels.wasm', 'levels' LANGUAGE wasm;

COPY (
  SELECT levels(f.video[1], 200, 100)
  FROM input('av.mp4') f
) TO 'levelled.mp4' WITH (video_codec 'libx264')
