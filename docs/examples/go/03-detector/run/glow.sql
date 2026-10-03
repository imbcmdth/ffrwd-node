CREATE FUNCTION glow(v video_stream, threshold number DEFAULT 230, gap number DEFAULT 2)
RETURNS STRUCT(start_t number, id number, x number, y number, w number, h number)[]
  AS 'glow.wasm', 'glow' LANGUAGE wasm;

COPY (
  SELECT glow(f.video[1])
  FROM input('testsrc.mp4') f
) TO 'glows.ndjson'
