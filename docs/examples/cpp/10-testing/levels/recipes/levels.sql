-- Stretch a file's picture to full range, its audio carried through untouched.
-- variables: source (input media path), dest (output path)
-- example: ffrwd run levels -v source=in.mp4 -v dest=out.mp4
COPY (
  SELECT acme.levels.levels(f.video[1]), f.audio[1]
  FROM input(:'source') f
) TO :'dest'
