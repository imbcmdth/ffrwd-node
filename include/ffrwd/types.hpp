#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "bytes.hpp"
#include "fields.hpp"
#include "time.hpp"

namespace ffrwd {

using Tags = std::vector<std::pair<std::string, std::string>>;

/// A stream's colorimetry, in ffmpeg's names: `tv` or `pc`, `bt709` and the
/// like, "unknown" where the wire does not settle a field.
struct ColorInfo {
    std::string range;
    std::string primaries;
    std::string trc;
    std::string space;

    friend bool operator==(const ColorInfo&, const ColorInfo&) = default;
};

/// The frames of a video stream: square pixels, tightly packed.
struct VideoFormat {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::string pix_fmt;
    std::optional<ColorInfo> color;

    friend bool operator==(const VideoFormat&, const VideoFormat&) = default;
};

/// The samples of an audio stream, interleaved.
struct AudioFormat {
    std::uint32_t sample_rate = 0;
    std::uint32_t channels = 0;
    /// "f32" or "s16".
    std::string sample_fmt;
    std::optional<std::string> channel_layout;

    friend bool operator==(const AudioFormat&, const AudioFormat&) = default;
};

/// The stream a bound input reads, as the host knows it.
struct StreamInfo {
    std::uint32_t index = 0;
    std::string kind;
    std::string codec;
    /// Seconds, where known.
    std::optional<double> duration;
    Tags tags;
    /// What the stream's timestamps count.
    Rational time_base{1, 1};

    StreamInfo() = default;
    /// A stream with nothing known about it but its time base.
    explicit StreamInfo(Rational time_base) : time_base(time_base) {}

    /// The value of tag `name`, if the stream carries it.
    std::optional<std::string_view> tag(std::string_view name) const {
        for (const auto& [key, value] : tags)
            if (key == name) return value;
        return std::nullopt;
    }
};

/// What the source read of one relation row.
struct RenditionMeta {
    std::optional<std::string> name;
    std::optional<std::uint64_t> bandwidth;
    std::optional<std::string> codecs;
    std::optional<std::string> language;
};

struct CodedVideo {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::optional<Rational> sample_aspect_ratio;
    std::optional<ColorInfo> color;
};

struct CodedAudio {
    std::uint32_t sample_rate = 0;
    std::uint32_t channels = 0;
    std::optional<std::string> channel_layout;
};

struct CodedData {};

using CodedFormat = std::variant<CodedVideo, CodedAudio, CodedData>;

/// One encoded stream: what a packets port carries.
struct CodedStream {
    std::string codec;
    Rational time_base{1, 1};
    CodedFormat format = CodedData{};
    std::vector<std::uint8_t> extradata;
    std::optional<std::int32_t> profile;
    std::optional<std::int32_t> level;
};

/// One encoded packet, exactly as the encoder emitted it.
struct Packet {
    std::int64_t pts = 0;
    std::optional<std::int64_t> dts;
    std::optional<std::int64_t> duration;
    bool keyframe = false;
    Bytes data;

    Packet clone() const { return {pts, dts, duration, keyframe, data.clone()}; }
};

/// The codec of a data stream, "json" today.
struct DataFormat {
    std::string codec = "json";
};

/// A stream's format, resolved.
using Format = std::variant<VideoFormat, AudioFormat, DataFormat, CodedStream>;

/// What the compiler knows of one stream a call binds, before the run.
struct StreamHint {
    /// Video: the frame rate. Audio: the sample rate over 1. None where
    /// nothing settles it before the run: a self-clocked source's output, a
    /// feed by port.
    std::optional<Rational> rate;

    friend bool operator==(const StreamHint&, const StreamHint&) = default;
};

/// One stream bound to an input port at `init`.
struct BoundStream {
    std::string port;
    /// What every tick call names this stream by.
    std::uint32_t id = 0;
    StreamInfo info;
    std::optional<Format> format;
    RenditionMeta rendition;
    std::optional<std::uint32_t> row;
    std::uint32_t decode_delay = 0;
    std::optional<double> latency;
    /// The hint `shape` was asked with for this stream.
    StreamHint hint;

    BoundStream() = default;

    /// A stream on `port` with nothing known about it but its time base.
    BoundStream(std::string port, std::uint32_t id, Rational time_base)
        : port(std::move(port)), id(id), info(time_base) {}

    /// A video stream of `width` x `height` in `pix_fmt`.
    static BoundStream video(std::string port, std::uint32_t id, std::uint32_t width,
                             std::uint32_t height, std::string pix_fmt, Rational time_base) {
        BoundStream stream(std::move(port), id, time_base);
        stream.info.kind = "video";
        stream.format = VideoFormat{width, height, std::move(pix_fmt), std::nullopt};
        return stream;
    }

    /// An audio stream at `sample_rate` with `channels` interleaved, in
    /// `sample_fmt`, counted in samples, its hint at that rate.
    static BoundStream audio(std::string port, std::uint32_t id, std::uint32_t sample_rate,
                             std::uint32_t channels, std::string sample_fmt) {
        BoundStream stream(std::move(port), id, Rational(1, std::int32_t(sample_rate)));
        stream.hint.rate = Rational(std::int32_t(sample_rate), 1);
        stream.info.kind = "audio";
        stream.format = AudioFormat{sample_rate, channels, std::move(sample_fmt), std::nullopt};
        return stream;
    }

    /// A data stream of JSON rows.
    static BoundStream rows(std::string port, std::uint32_t id, Rational time_base) {
        BoundStream stream(std::move(port), id, time_base);
        stream.info.kind = "data";
        stream.info.codec = "json";
        stream.format = DataFormat{"json"};
        return stream;
    }

    /// The stream at `rate`, as the compiler told `shape`: a video stream's
    /// frame rate.
    template <class Self>
    Self&& rate(this Self&& self, Rational rate) {
        self.hint.rate = rate;
        return std::forward<Self>(self);
    }

    /// The stream's frames, when it is a video stream.
    const VideoFormat* video_format() const {
        return format ? std::get_if<VideoFormat>(&*format) : nullptr;
    }

    /// The stream's samples, when it is an audio stream.
    const AudioFormat* audio_format() const {
        return format ? std::get_if<AudioFormat>(&*format) : nullptr;
    }
};

/// One frame as a tick sees it, without its bytes: a picture, or the tick's
/// samples as one run.
struct Frame {
    /// In the stream's time base; for audio, the first sample's.
    std::int64_t pts = 0;
    /// Its place in the stream's frames this tick: what `fetch` and `same`
    /// name it by.
    std::uint32_t index = 0;
    std::optional<std::int64_t> duration;
    /// The rows that rode in with it, each a JSON object.
    std::vector<std::string> rows;

    /// The rows riding this frame, each read as a `T`.
    template <class T>
    Result<std::vector<T>> read_rows() const {
        std::vector<T> read;
        for (const std::string& row : rows) {
            FFRWD_LET(value, parse<T>(row));
            read.push_back(std::move(value));
        }
        return read;
    }
};

/// One message on a data input: for codec "json", one row.
struct Message {
    std::int64_t pts = 0;
    std::string data;

    /// The message read as a `T`.
    template <class T>
    Result<T> row() const {
        return parse<T>(data);
    }
};

/// The rows a state input received on one tick an instance did not process.
struct TimedRows {
    std::int64_t pts = 0;
    std::vector<std::string> rows;
};

/// Where a hold input's current source starts.
struct FeedStart {
    Tags tags;
    /// Its first frame's pts, in its own time base.
    std::int64_t first_pts = 0;
    /// The clock time that first frame stands at, in the clock's time base.
    std::int64_t at = 0;
    /// The clock time of the tick the start was fixed on: seconds before
    /// `at` for a timed source, `at` itself when fixed on the tick it
    /// showed. What a countdown counts from.
    std::int64_t known = 0;
};

/// A hold input's current source, from the tick its first frame shows on to
/// the last its last frame shows on.
struct Feed {
    FeedStart start;
    /// The clock time of the last tick it shows on, once the host can tell.
    std::optional<std::int64_t> ends;
};

}  // namespace ffrwd
