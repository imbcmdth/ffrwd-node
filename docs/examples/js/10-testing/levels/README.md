# acme/levels

`levels(v, black, white)` stretches a picture's levels: `black` becomes 0,
`white` becomes 255, and everything between spreads out over the range.

```
npm install
npm run build
ffrwd run levels -v source=in.mp4 -v dest=out.mp4
```
