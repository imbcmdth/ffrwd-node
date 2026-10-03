CREATE FUNCTION still(v video_stream, shortest number DEFAULT 1, longest number DEFAULT 10,
                      tolerance number DEFAULT 2)
RETURNS STRUCT(start_t number, end_t number)[]
  AS 'still.wasm', 'still' LANGUAGE wasm;

COPY (
  SELECT still(f.video[1], longest => 2)
  FROM input('smptebars.mp4') f
) TO 'stills.ndjson'
