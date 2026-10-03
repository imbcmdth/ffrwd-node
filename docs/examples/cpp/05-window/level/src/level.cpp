#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "ffrwd/node.hpp"

struct Params {
    double window;
    std::optional<double> hop;
    FFRWD_FIELDS(window, hop)
};

/// How loud `samples` are, as a cue's text: their RMS in dB of full scale.
std::string loudness(const std::vector<float>& samples) {
    double power = 0.0;
    for (float sample : samples) power += double(sample) * double(sample);
    power /= double(std::max<std::size_t>(samples.size(), 1));
    double db = 10.0 * std::log10(power);
    if (db < -90.0) return "silence";
    char text[32];
    std::snprintf(text, sizeof text, "%.0f dB", db);
    return text;
}

struct Level : ffrwd::Node<Level, Params> {
    static constexpr std::string_view name = "level";
    static constexpr std::string_view version = "0.1.0";
    static constexpr std::string_view params_schema =
        R"({"type":"object","properties":{"window":{"type":"number","exclusiveMinimum":0,"maximum":60,"default":2},"hop":{"type":["number","null"],"exclusiveMinimum":0,"maximum":60}},"additionalProperties":false})";

    std::uint32_t a = 0;
    std::size_t channels = 0;
    double rate = 0.0;

    static ffrwd::Result<ffrwd::Shape> shape(const Params& params, const ffrwd::Bound& bound) {
        auto rate = bound.rate_of("a");
        if (!rate)
            return ffrwd::fail("level counts its window in samples, and the call gives `a` no sample rate");
        auto window = std::uint32_t(rate->count(params.window));
        auto stride = std::uint32_t(rate->count(params.hop.value_or(params.window)));
        return ffrwd::Shape()
            .input(ffrwd::Input::audio("a").clock().window(window, stride).sample_formats({"f32"}))
            .output(ffrwd::Output::rows("cues").schema<ffrwd::Cue>())
            .pure();
    }

    static ffrwd::Result<Level> init(Params, const ffrwd::Init& init) {
        FFRWD_LET(a, init.stream("a"));
        const ffrwd::AudioFormat* audio = a.audio_format();
        if (!audio) return ffrwd::fail("`a` is an audio input");
        Level node;
        node.a = a.id;
        node.channels = std::max<std::uint32_t>(audio->channels, 1);
        node.rate = audio->sample_rate;
        return node;
    }

    ffrwd::Status process(const ffrwd::Tick& tick, ffrwd::Out& out) {
        auto run = tick.frame(a);
        if (!run) return {};
        ffrwd::Bytes bytes = tick.fetch(a, run->index);
        std::vector<float> samples(bytes.size() / 4);
        std::memcpy(samples.data(), bytes.data(), samples.size() * 4);
        double start = tick.time_base().seconds(run->pts);
        double end = start + double(samples.size() / channels) / rate;
        return out.row("cues", run->pts, ffrwd::Cue{start, end, loudness(samples)});
    }
};

FFRWD_EXPORT(Level);
