CREATE FUNCTION tally() RETURNS sink
  AS 'tally.wasm', 'tally' LANGUAGE wasm;

COPY (
  SELECT f.video[1], f.audio[1]
  FROM input('av.mp4') f
) TO tally()
