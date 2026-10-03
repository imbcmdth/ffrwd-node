-- Run a file's picture through the module, its audio carried through untouched.
-- variables: source (input media path), dest (output path)
-- example: ffrwd run passthrough -v source=in.mp4 -v dest=out.mp4
COPY (
  SELECT acme.invert.passthrough(f.video[1]), f.audio[1]
  FROM input(:'source') f
) TO :'dest'
