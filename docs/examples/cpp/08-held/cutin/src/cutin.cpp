#include <algorithm>
#include <optional>
#include <string>

#include "ffrwd/node.hpp"

/// The tag a feeder puts on its stream to say its pts are programme time.
constexpr std::string_view TIMED = "smart_timed";

struct Params {
    double lead;
    double linger;
    double timeout;
    FFRWD_FIELDS(lead, linger, timeout)
};

/// One change in what the host says of the feed, or a note the feeder
/// wrote beside its picture.
struct Presence {
    std::string event;
    double t = 0.0;
    double at = 0.0;
    std::optional<std::string> text;
    FFRWD_FIELDS(event, t, at, text)
};

/// What a feeder writes beside its picture.
struct Note {
    std::string text;
    FFRWD_FIELDS(text)
};

struct Cutin : ffrwd::Node<Cutin, Params> {
    static constexpr std::string_view name = "cutin";
    static constexpr std::string_view version = "0.1.0";
    static constexpr std::string_view params_schema =
        R"({"type":"object","properties":{"port":{"type":"integer","minimum":1,"maximum":65535,"default":9000},"lead":{"type":"number","minimum":0,"maximum":60,"default":0.5},"linger":{"type":"number","minimum":0,"maximum":60,"default":0},"timeout":{"type":"number","minimum":0,"maximum":60,"default":1}},"additionalProperties":false})";

    std::uint32_t v = 0;
    std::size_t width = 0;
    std::optional<std::uint32_t> feed;
    std::optional<std::uint32_t> notes;
    /// One frame of the programme, in its time base.
    std::int64_t step = 1;

    static ffrwd::Result<ffrwd::Shape> shape(const Params& params, const ffrwd::Bound&) {
        auto feed = ffrwd::Input::video("feed")
                        .optional()
                        .hold()
                        .anchor(ffrwd::Anchor::tagged(std::string(TIMED)))
                        .lead(params.lead)
                        .group("cam")
                        .port_param("port")
                        .like("v")
                        .pixel_formats({"rgba"});
        if (params.linger > 0.0) feed.linger(params.linger);
        if (params.timeout > 0.0) feed.timeout(params.timeout);
        return ffrwd::Shape()
            .input(ffrwd::Input::video("v").clock().pixel_formats({"rgba"}))
            .input(std::move(feed))
            .input(ffrwd::Input::rows("notes").optional().interval().group("cam").schema<Note>())
            .output(ffrwd::Output::like("v"))
            .output(ffrwd::Output::rows("presence").schema<Presence>())
            .pure()
            .one_to_one();
    }

    static ffrwd::Result<Cutin> init(Params, const ffrwd::Init& init) {
        FFRWD_LET(v, init.stream("v"));
        const ffrwd::VideoFormat* video = v.video_format();
        if (!video) return ffrwd::fail("`v` is a video input");
        Cutin node;
        node.v = v.id;
        node.width = video->width;
        if (const ffrwd::BoundStream* feed = init.optional("feed")) node.feed = feed->id;
        if (const ffrwd::BoundStream* notes = init.optional("notes")) node.notes = notes->id;
        if (v.hint.rate)
            node.step = std::max<std::int64_t>(v.info.time_base.pts(v.hint.rate->duration(1)), 1);
        return node;
    }

    ffrwd::Status process(const ffrwd::Tick& tick, ffrwd::Out& out) {
        auto frame = tick.frame(v);
        if (!frame) return {};
        ffrwd::Rational clock = tick.time_base();
        std::int64_t pts = frame->pts, step = std::max<std::int64_t>(frame->duration.value_or(this->step), 1);
        auto say = [&](std::string_view event, std::int64_t at) {
            Presence row{std::string(event), clock.seconds(pts), clock.seconds(at), std::nullopt};
            return out.row("presence", pts, row);
        };
        std::optional<double> countdown;
        if (feed) {
            if (auto current = tick.feed(*feed)) {
                const ffrwd::FeedStart& start = current->start;
                if (start.known == pts && start.known < start.at) FFRWD_TRY(say("coming", start.at));
                if (pts <= start.at && start.at < pts + step) FFRWD_TRY(say("on", start.at));
                if (pts < start.at) countdown = double(start.at - pts) / double(start.at - start.known);
            }
            for (const ffrwd::Feed& ended : tick.ended_feeds(*feed)) {
                if (ended.ends && *ended.ends < pts && pts - step <= *ended.ends)
                    FFRWD_TRY(say("off", *ended.ends + step));
            }
        }
        if (notes) {
            ffrwd::Rational base = tick.info(*notes).time_base;
            for (const ffrwd::Message& message : tick.messages(*notes)) {
                std::int64_t at = std::max(base.rescale(message.pts, clock), pts);
                FFRWD_LET(note, message.row<Note>());
                Presence row{"note", clock.seconds(pts), clock.seconds(at), note.text};
                FFRWD_TRY(out.row("presence", at, row));
            }
        }
        if (feed) {
            if (auto shown = tick.frame(*feed))
                return out.same("v", pts, frame->duration, *feed, shown->index);
        }
        if (!countdown) return out.pass("v", v, *frame);
        double left = *countdown;
        ffrwd::Bytes pixels = tick.fetch(v, frame->index);
        auto bar = std::size_t(double(width) * left);
        std::size_t rows = pixels.size() / (width * 4);
        for (std::size_t row = rows - std::min<std::size_t>(rows, 8); row < rows; ++row)
            for (std::size_t x = 0; x < bar; ++x) {
                std::uint8_t* pixel = pixels.data() + (row * width + x) * 4;
                pixel[0] = 220, pixel[1] = 40, pixel[2] = 40, pixel[3] = 255;
            }
        return out.frame("v", pts, frame->duration, std::move(pixels));
    }
};

FFRWD_EXPORT(Cutin);
