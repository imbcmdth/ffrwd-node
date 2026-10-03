# acme/levels

`levels(v, black, white)` stretches a picture's levels: `black` becomes 0,
`white` becomes 255, and everything between spreads out over the range.

```
cargo build --release --target wasm32-wasip2
ffrwd run levels -v source=in.mp4 -v dest=out.mp4
```
