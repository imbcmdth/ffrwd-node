// The bindings' exports and the tick import, converted to and from the
// library's own types. Built into a module only; the host build has no use
// for it.

#include <cstdlib>
#include <cstring>

#include "ffrwd/node.hpp"
#include "node_module.h"

namespace ffrwd::detail {

namespace {

std::string text(const node_module_string_t& value) {
    return std::string(reinterpret_cast<const char*>(value.ptr), value.len);
}

template <class T>
T* allocate(std::size_t count) {
    return count == 0 ? nullptr : static_cast<T*>(std::calloc(count, sizeof(T)));
}

node_module_string_t wit_string(std::string_view value) {
    node_module_string_t out{nullptr, value.size()};
    if (!value.empty()) {
        out.ptr = allocate<std::uint8_t>(value.size());
        std::memcpy(out.ptr, value.data(), value.size());
    }
    return out;
}

node_module_list_string_t wit_strings(const std::vector<std::string>& values) {
    node_module_list_string_t out{allocate<node_module_string_t>(values.size()), values.size()};
    for (std::size_t n = 0; n < values.size(); ++n) out.ptr[n] = wit_string(values[n]);
    return out;
}

node_module_list_u32_t wit_u32s(const std::vector<std::uint32_t>& values) {
    node_module_list_u32_t out{allocate<std::uint32_t>(values.size()), values.size()};
    for (std::size_t n = 0; n < values.size(); ++n) out.ptr[n] = values[n];
    return out;
}

node_module_option_string_t wit_option(const std::optional<std::string>& value) {
    node_module_option_string_t out{};
    out.is_some = value.has_value();
    if (value) out.val = wit_string(*value);
    return out;
}

node_module_option_f64_t wit_option(const std::optional<double>& value) {
    return {value.has_value(), value.value_or(0.0)};
}

node_module_option_s64_t wit_option(const std::optional<std::int64_t>& value) {
    return {value.has_value(), value.value_or(0)};
}

ffrwd_av_types_rational_t wit_rational(Rational value) { return {value.num, value.den}; }

Rational rational(const ffrwd_av_types_rational_t& value) { return {value.num, value.den}; }

std::optional<std::string> option(const node_module_option_string_t& value) {
    if (!value.is_some) return std::nullopt;
    return text(value.val);
}

std::optional<double> option(const node_module_option_f64_t& value) {
    if (!value.is_some) return std::nullopt;
    return value.val;
}

std::optional<std::int64_t> option(const node_module_option_s64_t& value) {
    if (!value.is_some) return std::nullopt;
    return value.val;
}

std::vector<std::string> strings(const node_module_list_string_t& values) {
    std::vector<std::string> out;
    for (std::size_t n = 0; n < values.len; ++n) out.push_back(text(values.ptr[n]));
    return out;
}

Tags tags(const node_module_list_tuple2_string_string_t& values) {
    Tags out;
    for (std::size_t n = 0; n < values.len; ++n) out.emplace_back(text(values.ptr[n].f0), text(values.ptr[n].f1));
    return out;
}

std::optional<ColorInfo> color(const ffrwd_av_types_option_color_info_t& value) {
    if (!value.is_some) return std::nullopt;
    return ColorInfo{text(value.val.range), text(value.val.primaries), text(value.val.trc), text(value.val.space)};
}

ffrwd_av_types_option_color_info_t wit_color(const std::optional<ColorInfo>& value) {
    ffrwd_av_types_option_color_info_t out{};
    out.is_some = value.has_value();
    if (value)
        out.val = {wit_string(value->range), wit_string(value->primaries), wit_string(value->trc),
                   wit_string(value->space)};
    return out;
}

StreamInfo stream_info(const ffrwd_av_types_stream_info_t& value) {
    StreamInfo info(rational(value.time_base));
    info.index = value.index;
    info.kind = text(value.kind);
    info.codec = text(value.codec);
    info.duration = option(value.duration);
    info.tags = tags(value.tags);
    return info;
}

VideoFormat video_format(const ffrwd_av_types_video_format_t& value) {
    return {value.width, value.height, text(value.pix_fmt), color(value.color)};
}

AudioFormat audio_format(const ffrwd_av_types_audio_format_t& value) {
    return {value.sample_rate, value.channels, text(value.sample_fmt), option(value.channel_layout)};
}

ffrwd_av_types_video_format_t wit_video_format(const VideoFormat& value) {
    return {value.width, value.height, wit_string(value.pix_fmt), wit_color(value.color)};
}

ffrwd_av_types_audio_format_t wit_audio_format(const AudioFormat& value) {
    return {value.sample_rate, value.channels, wit_string(value.sample_fmt), wit_option(value.channel_layout)};
}

CodedStream coded_stream(const ffrwd_av_types_coded_stream_t& value) {
    CodedStream coded;
    coded.codec = text(value.codec);
    coded.time_base = rational(value.time_base);
    switch (value.format.tag) {
        case FFRWD_AV_TYPES_CODED_FORMAT_VIDEO: {
            const auto& video = value.format.val.video;
            CodedVideo out{video.width, video.height, std::nullopt, color(video.color)};
            if (video.sample_aspect_ratio.is_some) out.sample_aspect_ratio = rational(video.sample_aspect_ratio.val);
            coded.format = out;
            break;
        }
        case FFRWD_AV_TYPES_CODED_FORMAT_AUDIO: {
            const auto& audio = value.format.val.audio;
            coded.format = CodedAudio{audio.sample_rate, audio.channels, option(audio.channel_layout)};
            break;
        }
        default:
            coded.format = CodedData{};
    }
    coded.extradata.assign(value.extradata.ptr, value.extradata.ptr + value.extradata.len);
    if (value.profile.is_some) coded.profile = value.profile.val;
    if (value.level.is_some) coded.level = value.level.val;
    return coded;
}

ffrwd_av_types_coded_stream_t wit_coded_stream(const CodedStream& value) {
    ffrwd_av_types_coded_stream_t out{};
    out.codec = wit_string(value.codec);
    out.time_base = wit_rational(value.time_base);
    if (const auto* video = std::get_if<CodedVideo>(&value.format)) {
        out.format.tag = FFRWD_AV_TYPES_CODED_FORMAT_VIDEO;
        auto& wit = out.format.val.video;
        wit.width = video->width;
        wit.height = video->height;
        wit.sample_aspect_ratio.is_some = video->sample_aspect_ratio.has_value();
        if (video->sample_aspect_ratio) wit.sample_aspect_ratio.val = wit_rational(*video->sample_aspect_ratio);
        wit.color = wit_color(video->color);
    } else if (const auto* audio = std::get_if<CodedAudio>(&value.format)) {
        out.format.tag = FFRWD_AV_TYPES_CODED_FORMAT_AUDIO;
        out.format.val.audio = {audio->sample_rate, audio->channels, wit_option(audio->channel_layout)};
    } else {
        out.format.tag = FFRWD_AV_TYPES_CODED_FORMAT_DATA;
    }
    out.extradata = {allocate<std::uint8_t>(value.extradata.size()), value.extradata.size()};
    if (!value.extradata.empty()) std::memcpy(out.extradata.ptr, value.extradata.data(), value.extradata.size());
    out.profile = {value.profile.has_value(), value.profile.value_or(0)};
    out.level = {value.level.has_value(), value.level.value_or(0)};
    return out;
}

Packet packet(ffrwd_av_types_packet_t& value) {
    Packet out;
    out.pts = value.pts;
    out.dts = option(value.dts);
    out.duration = option(value.duration);
    out.keyframe = value.keyframe;
    out.data = Bytes::adopt(value.data.ptr, value.data.len);
    value.data = {nullptr, 0};
    return out;
}

ffrwd_av_types_packet_t wit_packet(Packet& value) {
    ffrwd_av_types_packet_t out{};
    out.pts = value.pts;
    out.dts = wit_option(value.dts);
    out.duration = wit_option(value.duration);
    out.keyframe = value.keyframe;
    std::size_t size = value.data.size();
    out.data = {value.data.release(), size};
    return out;
}

Feed feed(const ffrwd_av_node_types_feed_t& value) {
    Feed out;
    out.start.tags = tags(value.start.tags);
    out.start.first_pts = value.start.first_pts;
    out.start.at = value.start.at;
    out.start.known = value.start.known;
    out.ends = option(value.ends);
    return out;
}

StreamHint stream_hint(const ffrwd_av_node_types_stream_hint_t& value) {
    StreamHint hint;
    if (value.rate.is_some) hint.rate = rational(value.rate.val);
    return hint;
}

BoundStream bound_stream(const ffrwd_av_node_types_bound_stream_t& value) {
    BoundStream stream;
    stream.port = text(value.port);
    stream.id = value.id;
    stream.info = stream_info(value.info);
    if (value.format.is_some) {
        const auto& format = value.format.val;
        switch (format.tag) {
            case FFRWD_AV_NODE_TYPES_OUTPUT_FORMAT_VIDEO: stream.format = video_format(format.val.video); break;
            case FFRWD_AV_NODE_TYPES_OUTPUT_FORMAT_AUDIO: stream.format = audio_format(format.val.audio); break;
            case FFRWD_AV_NODE_TYPES_OUTPUT_FORMAT_DATA: stream.format = DataFormat{text(format.val.data)}; break;
            case FFRWD_AV_NODE_TYPES_OUTPUT_FORMAT_PACKETS: stream.format = coded_stream(format.val.packets); break;
            default: break;
        }
    }
    stream.rendition.name = option(value.rendition.name);
    if (value.rendition.bandwidth.is_some) stream.rendition.bandwidth = value.rendition.bandwidth.val;
    stream.rendition.codecs = option(value.rendition.codecs);
    stream.rendition.language = option(value.rendition.language);
    if (value.row.is_some) stream.row = value.row.val;
    stream.decode_delay = value.decode_delay;
    stream.latency = option(value.latency);
    stream.hint = stream_hint(value.hint);
    return stream;
}

ffrwd_av_node_types_port_kind_t port_kind(Kind kind) {
    switch (kind) {
        case Kind::Video: return FFRWD_AV_NODE_TYPES_PORT_KIND_VIDEO;
        case Kind::Audio: return FFRWD_AV_NODE_TYPES_PORT_KIND_AUDIO;
        case Kind::Data: return FFRWD_AV_NODE_TYPES_PORT_KIND_DATA;
        case Kind::Packets: return FFRWD_AV_NODE_TYPES_PORT_KIND_PACKETS;
    }
    return FFRWD_AV_NODE_TYPES_PORT_KIND_VIDEO;
}

ffrwd_av_node_types_anchor_t wit_anchor(const Anchor& anchor) {
    ffrwd_av_node_types_anchor_t out{};
    switch (anchor.kind) {
        case Anchor::Kind::SharedClock: out.tag = FFRWD_AV_NODE_TYPES_ANCHOR_SHARED_CLOCK; break;
        case Anchor::Kind::FirstFrame: out.tag = FFRWD_AV_NODE_TYPES_ANCHOR_FIRST_FRAME; break;
        case Anchor::Kind::Tagged:
            out.tag = FFRWD_AV_NODE_TYPES_ANCHOR_TAGGED;
            out.val.tagged = wit_string(anchor.tag);
            break;
    }
    return out;
}

ffrwd_av_node_types_input_port_t input_port(const InputPort& input) {
    ffrwd_av_node_types_input_port_t out{};
    out.name = wit_string(input.name);
    out.kind = port_kind(input.kind);
    out.required = input.required;
    out.many = input.many;
    if (const auto* hold = std::get_if<Hold>(&input.pairing)) {
        out.pairing.tag = FFRWD_AV_NODE_TYPES_PAIRING_HOLD;
        auto& wit = out.pairing.val.hold;
        wit.anchor = wit_anchor(hold->anchor);
        wit.lead = hold->lead;
        wit.linger = wit_option(hold->linger);
        wit.timeout = wit_option(hold->timeout);
        wit.group = wit_option(hold->group);
        wit.port_param = wit_option(hold->port_param);
    } else if (const auto* interval = std::get_if<Interval>(&input.pairing)) {
        out.pairing.tag = FFRWD_AV_NODE_TYPES_PAIRING_INTERVAL;
        auto& wit = out.pairing.val.interval;
        wit.latency = wit_option(interval->latency);
        wit.ahead = interval->ahead;
        wit.anchor = wit_anchor(interval->anchor);
        wit.group = wit_option(interval->group);
    } else if (std::holds_alternative<Arrival>(input.pairing)) {
        out.pairing.tag = FFRWD_AV_NODE_TYPES_PAIRING_ARRIVAL;
    } else {
        out.pairing.tag = FFRWD_AV_NODE_TYPES_PAIRING_LOCKSTEP;
    }
    switch (input.rows) {
        case RowsUse::Ignore: out.rows = FFRWD_AV_NODE_TYPES_ROWS_USE_IGNORE; break;
        case RowsUse::PerFrame: out.rows = FFRWD_AV_NODE_TYPES_ROWS_USE_PER_FRAME; break;
        case RowsUse::State: out.rows = FFRWD_AV_NODE_TYPES_ROWS_USE_STATE; break;
    }
    out.window = input.window;
    out.stride = input.stride;
    out.accepts.pixel_formats = wit_strings(input.accepts.pixel_formats);
    out.accepts.sample_formats = wit_strings(input.accepts.sample_formats);
    out.accepts.sample_rates = wit_u32s(input.accepts.sample_rates);
    out.accepts.channel_counts = wit_u32s(input.accepts.channel_counts);
    out.accepts.codecs = wit_strings(input.accepts.codecs);
    switch (input.accepts.wants) {
        case Wants::All: out.accepts.wants = FFRWD_AV_TYPES_WANTS_ALL; break;
        case Wants::Keyframes: out.accepts.wants = FFRWD_AV_TYPES_WANTS_KEYFRAMES; break;
        case Wants::First: out.accepts.wants = FFRWD_AV_TYPES_WANTS_FIRST; break;
        case Wants::Timing: out.accepts.wants = FFRWD_AV_TYPES_WANTS_TIMING; break;
    }
    out.accepts.like = wit_option(input.accepts.like);
    out.schema = wit_option(input.schema);
    return out;
}

ffrwd_av_node_types_output_port_t output_port(const OutputPort& output) {
    ffrwd_av_node_types_output_port_t out{};
    out.name = wit_string(output.name);
    out.kind = port_kind(output.kind);
    auto& format = out.format;
    if (output.like) {
        format.is_some = true;
        format.val.tag = FFRWD_AV_NODE_TYPES_OUTPUT_FORMAT_LIKE;
        format.val.val.like = {wit_string(output.like->port.value_or("")), wit_option(output.like->pixel_format),
                               wit_option(output.like->sample_format)};
    } else if (output.format) {
        format.is_some = true;
        if (const auto* video = std::get_if<VideoFormat>(&*output.format)) {
            format.val.tag = FFRWD_AV_NODE_TYPES_OUTPUT_FORMAT_VIDEO;
            format.val.val.video = wit_video_format(*video);
        } else if (const auto* audio = std::get_if<AudioFormat>(&*output.format)) {
            format.val.tag = FFRWD_AV_NODE_TYPES_OUTPUT_FORMAT_AUDIO;
            format.val.val.audio = wit_audio_format(*audio);
        } else if (const auto* data = std::get_if<DataFormat>(&*output.format)) {
            format.val.tag = FFRWD_AV_NODE_TYPES_OUTPUT_FORMAT_DATA;
            format.val.val.data = wit_string(data->codec);
        } else if (const auto* coded = std::get_if<CodedStream>(&*output.format)) {
            format.val.tag = FFRWD_AV_NODE_TYPES_OUTPUT_FORMAT_PACKETS;
            format.val.val.packets = wit_coded_stream(*coded);
        }
    }
    out.time_base.is_some = output.time_base.has_value();
    if (output.time_base) out.time_base.val = wit_rational(*output.time_base);
    out.latency = output.latency;
    out.schema = wit_option(output.schema);
    out.row.is_some = output.row.has_value();
    out.row.val = output.row.value_or(0);
    return out;
}

ffrwd_av_node_types_node_shape_t node_shape(const NodeShape& shape) {
    ffrwd_av_node_types_node_shape_t out{};
    out.inputs = {allocate<ffrwd_av_node_types_input_port_t>(shape.inputs.size()), shape.inputs.size()};
    for (std::size_t n = 0; n < shape.inputs.size(); ++n) out.inputs.ptr[n] = input_port(shape.inputs[n]);
    out.outputs = {allocate<ffrwd_av_node_types_output_port_t>(shape.outputs.size()), shape.outputs.size()};
    for (std::size_t n = 0; n < shape.outputs.size(); ++n) out.outputs.ptr[n] = output_port(shape.outputs[n]);
    Clock clock = shape.clock.value_or(Clock::self_clocked());
    switch (clock.kind) {
        case Clock::Kind::Input:
            out.clock.tag = FFRWD_AV_NODE_TYPES_CLOCK_INPUT;
            out.clock.val.input = wit_string(clock.port);
            break;
        case Clock::Kind::Rate:
            out.clock.tag = FFRWD_AV_NODE_TYPES_CLOCK_RATE;
            out.clock.val.rate = wit_rational(clock.rate);
            break;
        case Clock::Kind::RateOf:
            out.clock.tag = FFRWD_AV_NODE_TYPES_CLOCK_RATE_OF;
            out.clock.val.rate_of = wit_string(clock.port);
            break;
        case Clock::Kind::SelfClocked:
            out.clock.tag = FFRWD_AV_NODE_TYPES_CLOCK_SELF_CLOCKED;
            break;
    }
    out.pure = shape.pure;
    out.one_to_one = shape.one_to_one;
    out.bounded = shape.bounded;
    out.relation = wit_strings(shape.relation);
    return out;
}

exports_ffrwd_av_node_emitted_t wit_emitted(Emitted& emitted) {
    exports_ffrwd_av_node_emitted_t out{};
    std::size_t count = emitted.items.size();
    out.items = {allocate<exports_ffrwd_av_node_emission_t>(count), count};
    for (std::size_t n = 0; n < count; ++n) {
        Emission& item = emitted.items[n];
        auto& wit = out.items.ptr[n];
        wit.port = wit_string(item.port);
        auto& payload = wit.payload;
        if (auto* frame = std::get_if<FramePayload>(&item.payload)) {
            payload.tag = EXPORTS_FFRWD_AV_NODE_PAYLOAD_FRAME;
            std::size_t size = frame->data.size();
            payload.val.frame = {frame->pts, wit_option(frame->duration), {frame->data.release(), size}};
        } else if (auto* same = std::get_if<SamePayload>(&item.payload)) {
            payload.tag = EXPORTS_FFRWD_AV_NODE_PAYLOAD_SAME;
            payload.val.same = {same->pts, wit_option(same->duration), same->id, same->index};
        } else if (auto* message = std::get_if<MessagePayload>(&item.payload)) {
            payload.tag = EXPORTS_FFRWD_AV_NODE_PAYLOAD_MESSAGE;
            node_module_string_t data = wit_string(message->data);
            payload.val.message = {message->pts, {data.ptr, data.len}};
        } else if (auto* packet = std::get_if<Packet>(&item.payload)) {
            payload.tag = EXPORTS_FFRWD_AV_NODE_PAYLOAD_PACKET;
            payload.val.packet = wit_packet(*packet);
        }
    }
    out.rows = wit_strings(emitted.reports);
    out.finished = emitted.finished;
    return out;
}

class HostTick final : public Source {
public:
    explicit HostTick(ffrwd_av_node_tick_borrow_tick_t tick) : tick_(tick) {}

    std::int64_t pts() const override { return ffrwd_av_node_tick_method_tick_pts(tick_); }
    std::uint64_t ordinal() const override { return ffrwd_av_node_tick_method_tick_ordinal(tick_); }

    Rational time_base() const override {
        ffrwd_av_node_tick_rational_t value;
        ffrwd_av_node_tick_method_tick_time_base(tick_, &value);
        return rational(value);
    }

    bool last() const override { return ffrwd_av_node_tick_method_tick_last(tick_); }

    std::vector<std::uint32_t> streams(std::string_view port) const override {
        node_module_string_t name{reinterpret_cast<std::uint8_t*>(const_cast<char*>(port.data())), port.size()};
        node_module_list_u32_t ids;
        ffrwd_av_node_tick_method_tick_streams(tick_, &name, &ids);
        std::vector<std::uint32_t> out(ids.ptr, ids.ptr + ids.len);
        node_module_list_u32_free(&ids);
        return out;
    }

    StreamInfo info(std::uint32_t id) const override {
        ffrwd_av_node_tick_stream_info_t value;
        ffrwd_av_node_tick_method_tick_info(tick_, id, &value);
        StreamInfo out = stream_info(value);
        ffrwd_av_node_tick_stream_info_free(&value);
        return out;
    }

    std::optional<Feed> feed(std::uint32_t id) const override {
        ffrwd_av_node_tick_feed_t value;
        if (!ffrwd_av_node_tick_method_tick_feed(tick_, id, &value)) return std::nullopt;
        Feed out = ffrwd::detail::feed(value);
        ffrwd_av_node_tick_feed_free(&value);
        return out;
    }

    std::vector<Feed> ended_feeds(std::uint32_t id) const override {
        ffrwd_av_node_tick_list_feed_t value;
        ffrwd_av_node_tick_method_tick_ended_feeds(tick_, id, &value);
        std::vector<Feed> out;
        for (std::size_t n = 0; n < value.len; ++n) out.push_back(ffrwd::detail::feed(value.ptr[n]));
        ffrwd_av_node_tick_list_feed_free(&value);
        return out;
    }

    std::vector<Frame> frames(std::uint32_t id) const override {
        ffrwd_av_node_tick_list_frame_t value;
        ffrwd_av_node_tick_method_tick_frames(tick_, id, &value);
        std::vector<Frame> out;
        for (std::size_t n = 0; n < value.len; ++n) {
            const auto& frame = value.ptr[n];
            out.push_back({frame.pts, frame.index, option(frame.duration), strings(frame.rows)});
        }
        ffrwd_av_node_tick_list_frame_free(&value);
        return out;
    }

    Bytes fetch(std::uint32_t id, std::uint32_t index) const override {
        node_module_list_u8_t value;
        ffrwd_av_node_tick_method_tick_fetch(tick_, id, index, &value);
        return Bytes::adopt(value.ptr, value.len);
    }

    std::vector<Message> messages(std::uint32_t id) const override {
        ffrwd_av_node_tick_list_message_t value;
        ffrwd_av_node_tick_method_tick_messages(tick_, id, &value);
        std::vector<Message> out;
        for (std::size_t n = 0; n < value.len; ++n) {
            const auto& message = value.ptr[n];
            out.push_back({message.pts, std::string(reinterpret_cast<const char*>(message.data.ptr), message.data.len)});
        }
        ffrwd_av_node_tick_list_message_free(&value);
        return out;
    }

    std::vector<Packet> packets(std::uint32_t id) const override {
        ffrwd_av_node_tick_list_packet_t value;
        ffrwd_av_node_tick_method_tick_packets(tick_, id, &value);
        std::vector<Packet> out;
        for (std::size_t n = 0; n < value.len; ++n) out.push_back(packet(value.ptr[n]));
        ffrwd_av_node_tick_list_packet_free(&value);
        return out;
    }

    std::vector<TimedRows> earlier_rows(std::uint32_t id) const override {
        ffrwd_av_node_tick_list_timed_rows_t value;
        ffrwd_av_node_tick_method_tick_earlier_rows(tick_, id, &value);
        std::vector<TimedRows> out;
        for (std::size_t n = 0; n < value.len; ++n) out.push_back({value.ptr[n].pts, strings(value.ptr[n].rows)});
        ffrwd_av_node_tick_list_timed_rows_free(&value);
        return out;
    }

private:
    ffrwd_av_node_tick_borrow_tick_t tick_;
};

bool refuse(const Error& error, node_module_string_t* err) {
    *err = wit_string(error.message);
    return false;
}

}  // namespace

}  // namespace ffrwd::detail

using namespace ffrwd;
using namespace ffrwd::detail;

extern "C" {

void exports_ffrwd_av_node_describe(exports_ffrwd_av_node_meta_t* ret) {
    Meta meta = exported().describe();
    *ret = {};
    ret->name = wit_string(meta.name);
    ret->version = wit_string(meta.version);
    ret->params_schema = wit_string(meta.params_schema);
    ret->rows_schema = wit_string(meta.rows_schema);
    ret->rows_language = wit_strings(meta.rows_language);
}

bool exports_ffrwd_av_node_shape(node_module_string_t* params, exports_ffrwd_av_node_list_binding_t* bound,
                                 exports_ffrwd_av_node_node_shape_t* ret, node_module_string_t* err) {
    std::string text_params = text(*params);
    std::vector<Binding> bindings;
    for (std::size_t n = 0; n < bound->len; ++n) {
        Binding binding{text(bound->ptr[n].input), {}};
        const auto& streams = bound->ptr[n].streams;
        for (std::size_t k = 0; k < streams.len; ++k) binding.streams.push_back(stream_hint(streams.ptr[k]));
        bindings.push_back(std::move(binding));
    }
    node_module_string_free(params);
    exports_ffrwd_av_node_list_binding_free(bound);
    auto shape = exported().shape(text_params, Bound(std::move(bindings)));
    if (!shape) return refuse(shape.error(), err);
    *ret = node_shape(*shape);
    return true;
}

bool exports_ffrwd_av_node_init(exports_ffrwd_av_node_list_bound_stream_t* bound, node_module_list_string_t* latched,
                                node_module_string_t* params, node_module_string_t* err) {
    std::vector<BoundStream> streams;
    for (std::size_t n = 0; n < bound->len; ++n) streams.push_back(bound_stream(bound->ptr[n]));
    std::vector<std::string> outputs = strings(*latched);
    std::string text_params = text(*params);
    exports_ffrwd_av_node_list_bound_stream_free(bound);
    node_module_list_string_free(latched);
    node_module_string_free(params);
    auto opened = exported().init(std::move(streams), std::move(outputs), text_params);
    if (!opened) return refuse(opened.error(), err);
    return true;
}

bool exports_ffrwd_av_node_set_params(node_module_string_t* params, node_module_string_t* err) {
    std::string text_params = text(*params);
    node_module_string_free(params);
    auto set = exported().set_params(text_params);
    if (!set) return refuse(set.error(), err);
    return true;
}

bool exports_ffrwd_av_node_process(exports_ffrwd_av_node_borrow_tick_t tick, exports_ffrwd_av_node_emitted_t* ret,
                                   node_module_string_t* err) {
    auto emitted = exported().process(HostTick(tick));
    ffrwd_av_node_tick_tick_drop_borrow(tick);
    if (!emitted) return refuse(emitted.error(), err);
    *ret = wit_emitted(*emitted);
    return true;
}

}  // extern "C"
