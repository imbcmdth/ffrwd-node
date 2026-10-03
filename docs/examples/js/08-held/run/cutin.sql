CREATE FUNCTION cutin(v video_stream, feed video_stream DEFAULT NULL,
                      port number DEFAULT 9000, lead number DEFAULT 0.5,
                      linger number DEFAULT 0, timeout number DEFAULT 1)
RETURNS STRUCT(v video_stream,
               presence STRUCT(event text, t number, at number, text text)[])
  AS 'cutin.wasm', 'cutin' LANGUAGE wasm;

COPY (
  SELECT cutin(f.video[1], port => 9100).v, f.audio[1]
  FROM input('av.mp4') f
) TO 'cutin.mp4' WITH (video_codec 'libx264', audio_codec 'aac')
