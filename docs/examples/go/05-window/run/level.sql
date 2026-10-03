CREATE FUNCTION level(a audio_stream, window number DEFAULT 2, hop number DEFAULT NULL)
RETURNS cue[]
  AS 'level.wasm', 'level' LANGUAGE wasm;

COPY (
  SELECT f.video[1], f.audio[1], level(f.audio[1], 1, 0.5)
  FROM input('av.mp4') f
) TO 'levels.mkv' WITH (video_codec 'copy', audio_codec 'copy')
