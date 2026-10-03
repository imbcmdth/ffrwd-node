CREATE FUNCTION cutin(v video_stream, feed video_stream DEFAULT NULL,
                      port number DEFAULT 9000, lead number DEFAULT 0.5,
                      linger number DEFAULT 0, timeout number DEFAULT 1)
RETURNS STRUCT(v video_stream,
               presence STRUCT(event text, t number, at number, text text)[])
  AS 'cutin.wasm', 'cutin' LANGUAGE wasm;

COPY (
  SELECT cutin(p.video[1], c.video[1], lead => 1).presence
  FROM input('av.mp4') p, input('testsrc.mp4') c
  WHERE c.t <= 1.5
) TO 'presence.ndjson'
