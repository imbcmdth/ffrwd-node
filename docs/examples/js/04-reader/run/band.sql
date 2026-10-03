CREATE FUNCTION level(a audio_stream, window number DEFAULT 2, hop number DEFAULT NULL)
RETURNS cue[]
  AS 'level.wasm', 'level' LANGUAGE wasm;

CREATE FUNCTION band(v video_stream, cues cue[], fade number DEFAULT 0.5)
RETURNS video_stream
  AS 'band.wasm', 'band' LANGUAGE wasm;

COPY (
  SELECT band(f.video[1], level(f.audio[1])), f.audio[1]
  FROM input('av.mp4') f
) TO 'banded.mp4' WITH (video_codec 'libx264', audio_codec 'aac')
