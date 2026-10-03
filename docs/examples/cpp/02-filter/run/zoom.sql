CREATE FUNCTION zoom(v video_stream, amount number DEFAULT 2,
                     x number DEFAULT 0.5, y number DEFAULT 0.5)
RETURNS video_stream
  AS 'zoom.wasm', 'zoom' LANGUAGE wasm;

COPY (
  SELECT zoom(f.video[1], 3, x => 0.25), f.audio[1]
  FROM input('av.mp4') f
) TO 'zoomed.mp4' WITH (video_codec 'libx264', audio_codec 'aac')
