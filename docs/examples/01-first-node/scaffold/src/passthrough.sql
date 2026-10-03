-- The wasm module this package ships, declared as a function a query calls.
-- The path is the cargo build output, relative to this package's root.
CREATE FUNCTION passthrough(v video_stream) RETURNS video_stream
  AS 'target/wasm32-wasip2/release/invert.wasm', 'passthrough' LANGUAGE wasm;
