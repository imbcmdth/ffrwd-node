CREATE FUNCTION bars(width number DEFAULT 1280, height number DEFAULT 720,
                     fps number DEFAULT 30, seconds number DEFAULT NULL)
RETURNS source
  AS 'bars.wasm', 'bars' LANGUAGE wasm;

COPY (
  SELECT s.video[1]
  FROM bars(640, 360, seconds => 5) s
) TO 'bars.mp4' WITH (video_codec 'libx264')
