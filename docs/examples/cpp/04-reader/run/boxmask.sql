CREATE FUNCTION glow(v video_stream, threshold number DEFAULT 230, gap number DEFAULT 2)
RETURNS STRUCT(start_t number, id number, x number, y number, w number, h number)[]
  AS 'glow.wasm', 'glow' LANGUAGE wasm;

CREATE FUNCTION boxmask(v video_stream, boxes STRUCT(x number, y number, w number, h number)[])
RETURNS video_stream
  AS 'boxmask.wasm', 'boxmask' LANGUAGE wasm;

COPY (
  SELECT boxmask(f.video[1], glow(f.video[1]))
  FROM input('testsrc.mp4') f
) TO 'mask.mkv' WITH (video_codec 'ffv1')
