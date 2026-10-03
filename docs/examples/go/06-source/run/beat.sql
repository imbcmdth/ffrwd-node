CREATE FUNCTION beat(every number DEFAULT 1, width number DEFAULT 320,
                     height number DEFAULT 240)
RETURNS source
  AS 'beat.wasm', 'beat' LANGUAGE wasm;

COPY (
  SELECT s.video[1]
  FROM beat(0.5) s
  WHERE s.t < 5
) TO 'beat.mp4' WITH (video_codec 'libx264')
