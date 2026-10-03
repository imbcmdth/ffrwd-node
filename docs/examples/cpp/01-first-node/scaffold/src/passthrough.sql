-- The wasm module this package ships, declared as a function a query calls.
-- The path is build.sh's output, relative to this package's root.
CREATE FUNCTION passthrough(v video_stream) RETURNS video_stream
  AS 'build/invert.wasm', 'passthrough' LANGUAGE wasm;
