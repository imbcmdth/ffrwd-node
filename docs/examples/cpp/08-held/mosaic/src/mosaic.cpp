#include <algorithm>
#include <array>
#include <vector>

#include "ffrwd/node.hpp"
#include "resize.hpp"

constexpr std::array<std::uint8_t, 4> DOWN{48, 48, 48, 255};

struct Params {
    std::size_t columns;
    std::uint32_t width;
    std::uint32_t height;
    FFRWD_FIELDS(columns, width, height)
};

struct Tile {
    std::uint32_t id = 0;
    std::size_t width = 0;
    std::size_t height = 0;
    resize::Rect cell;
};

struct Mosaic : ffrwd::Node<Mosaic, Params> {
    static constexpr std::string_view name = "mosaic";
    static constexpr std::string_view version = "0.1.0";
    static constexpr std::string_view params_schema =
        R"({"type":"object","properties":{"columns":{"type":"integer","minimum":1,"default":2},"width":{"type":"integer","minimum":16,"default":1280},"height":{"type":"integer","minimum":16,"default":720}},"additionalProperties":false})";

    std::vector<Tile> tiles;
    std::size_t width = 0;
    std::size_t height = 0;

    /// `pixels`, a `tile`'s picture, resized into its cell of `canvas`.
    ffrwd::Status put(ffrwd::Bytes& canvas, const Tile& tile, const ffrwd::Bytes& pixels) const {
        if (auto wrong = resize::misfit(pixels.size(), tile.width, tile.height)) return ffrwd::fail(*wrong);
        std::size_t w = tile.cell.width(), h = tile.cell.height();
        auto whole = resize::Rect::whole(tile.width, tile.height);
        auto rgb = resize::bilinear(pixels.data(), tile.width, tile.height, whole, w, h);
        for (std::size_t y = 0; y < h; ++y)
            for (std::size_t x = 0; x < w; ++x) {
                std::size_t at = ((tile.cell.y0 + y) * width + tile.cell.x0 + x) * 4;
                for (std::size_t channel = 0; channel < 3; ++channel)
                    canvas[at + channel] = rgb[(y * w + x) * 3 + channel];
            }
        return {};
    }

    void fill(ffrwd::Bytes& canvas, resize::Rect cell, std::array<std::uint8_t, 4> colour) const {
        for (std::size_t y = cell.y0; y < cell.y1; ++y)
            for (std::size_t x = cell.x0; x < cell.x1; ++x)
                std::copy(colour.begin(), colour.end(), canvas.data() + (y * width + x) * 4);
    }

    static ffrwd::Result<ffrwd::Shape> shape(const Params& params, const ffrwd::Bound&) {
        return ffrwd::Shape()
            .input(ffrwd::Input::video("v")
                       .many()
                       .hold()
                       .anchor(ffrwd::Anchor::shared_clock())
                       .pixel_formats({"rgba"}))
            .output(ffrwd::Output::video("v").size(params.width, params.height).pixel_format("rgba"))
            .rate_of("v")
            .pure();
    }

    static ffrwd::Result<Mosaic> init(Params params, const ffrwd::Init& init) {
        std::size_t width = params.width, height = params.height;
        auto streams = init.streams("v");
        std::size_t columns = std::max<std::size_t>(std::min(params.columns, streams.size()), 1);
        std::size_t rows = std::max<std::size_t>((streams.size() + columns - 1) / columns, 1);
        Mosaic node;
        for (std::size_t n = 0; n < streams.size(); ++n) {
            const ffrwd::VideoFormat* video = streams[n]->video_format();
            if (!video) return ffrwd::fail("`v` takes pictures");
            std::size_t column = n % columns, row = n / columns;
            node.tiles.push_back(Tile{
                streams[n]->id,
                video->width,
                video->height,
                {column * width / columns, row * height / rows, (column + 1) * width / columns,
                 (row + 1) * height / rows},
            });
        }
        node.width = width;
        node.height = height;
        return node;
    }

    ffrwd::Status process(const ffrwd::Tick& tick, ffrwd::Out& out) {
        ffrwd::Bytes canvas(width * height * 4);
        for (std::size_t at = 3; at < canvas.size(); at += 4) canvas[at] = 255;
        bool shown = false;
        for (const Tile& tile : tiles) {
            if (auto frame = tick.frame(tile.id)) {
                FFRWD_TRY(put(canvas, tile, tick.fetch(tile.id, frame->index)));
                shown = true;
            } else if (!tick.feed(tile.id)) {
                fill(canvas, tile.cell, DOWN);
            }
        }
        if (!shown && tick.last()) return {};
        return out.frame("v", tick.pts(), 1, std::move(canvas));
    }
};

FFRWD_EXPORT(Mosaic);
