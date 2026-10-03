#include "ffrwd/out.hpp"

#include <algorithm>

namespace ffrwd {

namespace {

const char* kind_name(Kind kind) {
    switch (kind) {
        case Kind::Video: return "video";
        case Kind::Audio: return "audio";
        case Kind::Data: return "data";
        case Kind::Packets: return "packets";
    }
    return "?";
}

std::string quoted(std::string_view name) { return "`" + std::string(name) + "`"; }

}  // namespace

std::vector<std::pair<std::int64_t, std::string>> Emitted::messages(std::string_view port) const {
    std::vector<std::pair<std::int64_t, std::string>> found;
    for (const Emission& item : items)
        if (item.port == port)
            if (const auto* message = std::get_if<MessagePayload>(&item.payload))
                found.emplace_back(message->pts, message->data);
    return found;
}

std::vector<const Payload*> Emitted::on(std::string_view port) const {
    std::vector<const Payload*> found;
    for (const Emission& item : items)
        if (item.port == port) found.push_back(&item.payload);
    return found;
}

Out::Out(const NodeShape& shape) {
    for (const OutputPort& output : shape.outputs)
        ports_.push_back({output.name, output.kind, output.time_base, std::nullopt, std::nullopt});
}

Result<Out::Port*> Out::port(std::string_view name, std::initializer_list<Kind> kinds) {
    auto found = std::find_if(ports_.begin(), ports_.end(), [&](const Port& port) { return port.name == name; });
    if (found == ports_.end()) return fail(quoted(name) + " is not an output of this node");
    if (std::find(kinds.begin(), kinds.end(), found->kind) == kinds.end())
        return fail(quoted(name) + " is a " + kind_name(found->kind) + " output");
    return &*found;
}

Status Out::stamp(std::string_view name, std::initializer_list<Kind> kinds, std::int64_t pts) {
    FFRWD_LET(found, port(name, kinds));
    if (found->last_pts && pts < *found->last_pts)
        return fail(quoted(name) + " would go back from pts " + std::to_string(*found->last_pts) +
                    " to " + std::to_string(pts) + "; a port's pts never decrease");
    found->last_pts = pts;
    return {};
}

void Out::push(std::string_view port, Payload payload) {
    emitted_.items.push_back({std::string(port), std::move(payload)});
}

Result<Rational> Out::time_base(std::string_view port) const {
    for (const Port& candidate : ports_)
        if (candidate.name == port) return candidate.time_base.value_or(clock_);
    return fail(quoted(port) + " is not an output of this node");
}

Result<std::int64_t> Out::pts(std::string_view port, double seconds) const {
    FFRWD_LET(base, time_base(port));
    return base.pts(seconds);
}

std::optional<std::int64_t> Out::last(std::string_view port) const {
    for (const Port& candidate : ports_)
        if (candidate.name == port) return candidate.last_pts;
    return std::nullopt;
}

Status Out::frame(std::string_view port, std::int64_t pts, std::optional<std::int64_t> duration,
                  Bytes data) {
    FFRWD_TRY(stamp(port, {Kind::Video, Kind::Audio}, pts));
    push(port, FramePayload{pts, duration, std::move(data)});
    return {};
}

Status Out::same(std::string_view port, std::int64_t pts, std::optional<std::int64_t> duration,
                 std::uint32_t id, std::uint32_t index) {
    if (auto input = timing_.find(id); input != timing_.end())
        return fail("a frame of " + quoted(input->second) + " cannot leave on " + quoted(port) +
                    ": " + quoted(input->second) +
                    " is read for its timing alone, and the host carries none of its bytes");
    FFRWD_TRY(stamp(port, {Kind::Video, Kind::Audio}, pts));
    push(port, SamePayload{pts, duration, id, index});
    return {};
}

Status Out::message(std::string_view port, std::int64_t pts, std::string data) {
    FFRWD_TRY(stamp(port, {Kind::Data}, pts));
    push(port, MessagePayload{pts, std::move(data)});
    return {};
}

Status Out::cue(std::string_view port, const Cue& cue) {
    FFRWD_LET(at, pts(port, cue.start_t));
    return row(port, at, cue);
}

Status Out::packet(std::string_view port, Packet packet) {
    FFRWD_LET(found, this->port(port, {Kind::Packets}));
    if (packet.dts) {
        if (found->last_dts && *packet.dts < *found->last_dts)
            return fail(quoted(port) + " would go back from dts " + std::to_string(*found->last_dts) +
                        " to " + std::to_string(*packet.dts) + "; packets keep decode order");
        found->last_dts = packet.dts;
    }
    push(port, std::move(packet));
    return {};
}

}  // namespace ffrwd
