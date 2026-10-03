CREATE FUNCTION mosaic(v video_stream[], columns number DEFAULT 2,
                       width number DEFAULT 1280, height number DEFAULT 720)
RETURNS video_stream
  AS 'mosaic.wasm', 'mosaic' LANGUAGE wasm;

COPY (
  SELECT mosaic(ARRAY[a.video[1], b.video[1], c.video[1]], 3, 960, 240)
  FROM input('av.mp4') a, input('av2.mp4') b, input('testsrc.mp4') c
) TO 'mosaic.mp4' WITH (video_codec 'libx264')
