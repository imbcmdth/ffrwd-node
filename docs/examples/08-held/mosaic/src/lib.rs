use ffrwd_frame::{planes, Filter, Norm, Rect, Rgba};
use ffrwd_node::{Anchor, Bound, Init, Input, Node, Out, Output, Result, Shape, Tick};
use serde::Deserialize;

/// What `planes` divides by to hand back eight-bit values unchanged.
const EIGHT_BITS: Norm = Norm {
    mean: [0.0; 3],
    std: [1.0 / 255.0; 3],
};

const DOWN: [u8; 4] = [48, 48, 48, 255];

#[derive(Deserialize)]
struct Params {
    columns: usize,
    width: u32,
    height: u32,
}

struct Tile {
    id: u32,
    width: usize,
    height: usize,
    cell: Rect,
}

struct Mosaic {
    tiles: Vec<Tile>,
    width: usize,
    height: usize,
}

impl Mosaic {
    /// `pixels`, a `tile`'s picture, resized into its cell of `canvas`.
    fn put(&self, canvas: &mut [u8], tile: &Tile, pixels: &[u8]) -> Result<()> {
        let picture = Rgba::new(pixels, tile.width, tile.height)?;
        let (w, h) = (tile.cell.width(), tile.cell.height());
        let whole = Rect::whole(tile.width, tile.height);
        let rgb = planes(&picture, whole, w, h, Filter::Bilinear, EIGHT_BITS);
        for y in 0..h {
            for x in 0..w {
                let at = ((tile.cell.y0 + y) * self.width + tile.cell.x0 + x) * 4;
                for channel in 0..3 {
                    let value = rgb[channel * w * h + y * w + x];
                    canvas[at + channel] = value.round().clamp(0.0, 255.0) as u8;
                }
            }
        }
        Ok(())
    }

    fn fill(&self, canvas: &mut [u8], cell: Rect, colour: [u8; 4]) {
        for y in cell.y0..cell.y1 {
            let row = &mut canvas[(y * self.width + cell.x0) * 4..(y * self.width + cell.x1) * 4];
            for pixel in row.as_chunks_mut::<4>().0 {
                *pixel = colour;
            }
        }
    }
}

impl Node for Mosaic {
    const NAME: &'static str = "mosaic";
    const VERSION: &'static str = "0.1.0";
    const PARAMS_SCHEMA: &'static str = r#"{"type":"object","properties":{"columns":{"type":"integer","minimum":1,"default":2},"width":{"type":"integer","minimum":16,"default":1280},"height":{"type":"integer","minimum":16,"default":720}},"additionalProperties":false}"#;
    type Params = Params;

    fn shape(params: &Params, _: &Bound) -> Result<Shape> {
        Ok(Shape::new()
            .input(
                Input::video("v")
                    .many()
                    .hold()
                    .anchor(Anchor::SharedClock)
                    .pixel_formats(&["rgba"]),
            )
            .output(
                Output::video("v")
                    .size(params.width, params.height)
                    .pixel_format("rgba"),
            )
            .rate_of("v")
            .pure())
    }

    fn init(params: Params, init: &Init) -> Result<Mosaic> {
        let (width, height) = (params.width as usize, params.height as usize);
        let streams = init.streams("v");
        let columns = params.columns.min(streams.len()).max(1);
        let rows = streams.len().div_ceil(columns).max(1);
        let mut tiles = Vec::new();
        for (n, stream) in streams.iter().enumerate() {
            let video = stream.video_format().ok_or("`v` takes pictures")?;
            let (column, row) = (n % columns, n / columns);
            tiles.push(Tile {
                id: stream.id,
                width: video.width as usize,
                height: video.height as usize,
                cell: Rect {
                    x0: column * width / columns,
                    y0: row * height / rows,
                    x1: (column + 1) * width / columns,
                    y1: (row + 1) * height / rows,
                },
            });
        }
        Ok(Mosaic {
            tiles,
            width,
            height,
        })
    }

    fn process(&mut self, tick: &Tick, out: &mut Out) -> Result<()> {
        let mut canvas = [0, 0, 0, 255].repeat(self.width * self.height);
        let mut shown = false;
        for tile in &self.tiles {
            match tick.frame(tile.id) {
                Some(frame) => {
                    self.put(&mut canvas, tile, &tick.fetch(tile.id, frame.index))?;
                    shown = true;
                }
                None if tick.feed(tile.id).is_none() => self.fill(&mut canvas, tile.cell, DOWN),
                None => {}
            }
        }
        if !shown && tick.last() {
            return Ok(());
        }
        Ok(out.frame("v", tick.pts(), Some(1), canvas)?)
    }
}

ffrwd_node::export!(Mosaic);
