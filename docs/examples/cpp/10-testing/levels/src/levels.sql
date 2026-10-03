-- Stretches the picture's levels so `black` becomes 0 and `white` 255.
CREATE FUNCTION levels(v video_stream, black number DEFAULT 16, white number DEFAULT 235)
RETURNS video_stream
  AS 'build/levels.wasm', 'levels' LANGUAGE wasm;
