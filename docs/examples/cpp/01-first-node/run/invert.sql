CREATE FUNCTION invert(v video_stream) RETURNS video_stream
  AS 'invert.wasm', 'invert' LANGUAGE wasm;

COPY (
  SELECT invert(f.video[1]), f.audio[1]
  FROM input('av.mp4') f
) TO 'inverted.mp4' WITH (video_codec 'libx264', audio_codec 'aac')
