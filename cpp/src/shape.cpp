#include "ffrwd/shape.hpp"

#include <algorithm>
#include <set>

namespace ffrwd {

namespace {

const std::vector<StreamHint> no_streams;

const char* kind_name(Kind kind) {
    switch (kind) {
        case Kind::Video: return "video";
        case Kind::Audio: return "audio";
        case Kind::Data: return "data";
        case Kind::Packets: return "packets";
    }
    return "?";
}

Kind format_kind(const Format& format) {
    switch (format.index()) {
        case 0: return Kind::Video;
        case 1: return Kind::Audio;
        case 2: return Kind::Data;
        default: return Kind::Packets;
    }
}

std::string quoted(std::string_view name) { return "`" + std::string(name) + "`"; }

Status check(const NodeShape& shape, const Bound& bound) {
    if (shape.clock) {
        const Clock& clock = *shape.clock;
        if (clock.kind == Clock::Kind::Input) {
            const InputPort* input = shape.find_input(clock.port);
            if (!input) return fail("the clock is " + quoted(clock.port) + ", which is not an input");
            if (!input->required || input->many || !std::holds_alternative<Lockstep>(input->pairing))
                return fail("the clock " + quoted(clock.port) +
                            " has to be required, single and lockstep");
        } else if (clock.kind == Clock::Kind::RateOf && !shape.find_input(clock.port)) {
            return fail("the rate is " + quoted(clock.port) + "'s, which is not an input");
        } else if (clock.kind == Clock::Kind::Rate && (clock.rate.num <= 0 || clock.rate.den <= 0)) {
            return fail("a rate of " + std::to_string(clock.rate.num) + "/" +
                        std::to_string(clock.rate.den) + " never ticks");
        }
    }
    bool input_clock = shape.clock_input().has_value();
    bool self_clocked = shape.clock && shape.clock->kind == Clock::Kind::SelfClocked;
    for (const InputPort& input : shape.inputs) {
        std::string name = quoted(input.name);
        bool frames = carries_frames(input.kind);
        if (std::holds_alternative<Lockstep>(input.pairing) && !input_clock)
            return fail(name +
                        " is lockstep, and a node without an input clock has nothing to be in "
                        "step with: hold it, or pair it by interval");
        if (std::holds_alternative<Hold>(input.pairing) && !frames)
            return fail(name + " carries messages, so it cannot be held");
        if (std::holds_alternative<Interval>(input.pairing) && frames)
            return fail(name + " carries frames, so it cannot pair by interval");
        if (self_clocked && !std::holds_alternative<Arrival>(input.pairing))
            return fail(name + " feeds a self-clocked node, so it pairs by arrival");
        if (input.kind == Kind::Data && input.rows == RowsUse::Ignore)
            return fail(name + " carries rows, so it cannot ignore them");
        if (input.accepts.wants == Wants::Timing && !frames)
            return fail(name + " carries no frames, so it cannot be read for its timing alone");
        if (const auto* interval = std::get_if<Interval>(&input.pairing); interval && interval->group) {
            const std::string& group = *interval->group;
            bool held = std::any_of(shape.inputs.begin(), shape.inputs.end(), [&](const InputPort& other) {
                const auto* hold = std::get_if<Hold>(&other.pairing);
                return hold && hold->group == group;
            });
            if (!held)
                return fail(name + " arrives on hold group " + quoted(group) +
                            ", and no hold input is in it");
            if (interval->anchor != Anchor::shared_clock())
                return fail(name + " arrives on hold group " + quoted(group) +
                            ", whose first picture fixes its offset, so its anchor is the shared "
                            "clock");
        }
        if (input.stride == 0 || input.stride > input.window)
            return fail(name + " has a window of " + std::to_string(input.window) +
                        " and a stride of " + std::to_string(input.stride) +
                        ": the stride runs from 1 to the window");
        if (input.accepts.like) {
            const std::string& port = *input.accepts.like;
            const InputPort* other = shape.find_input(port);
            bool single = other && !other->many && other->kind == input.kind;
            if (!single || !bound.has(port))
                return fail(name + " is conformed to " + quoted(port) +
                            ", which has to be a single bound input of its kind");
        }
    }
    for (const OutputPort& output : shape.outputs) {
        std::string name = "output " + quoted(output.name);
        if (output.format && format_kind(*output.format) != output.kind)
            return fail(name + " is " + kind_name(output.kind) + " with a " +
                        kind_name(format_kind(*output.format)) + " format");
        if (output.format) {
            if (const auto* video = std::get_if<VideoFormat>(&*output.format); video && video->pix_fmt.empty())
                return fail(name + " has a size and no pixel format");
        }
        if (!output.format && !output.like && !input_clock)
            return fail(name +
                        " takes the clock input's format, and the clock is not an input: give it "
                        "a format");
        if (output.like && output.like->port) {
            if (const InputPort* input = shape.find_input(*output.like->port); input && input->kind != output.kind)
                return fail(name + " is " + kind_name(output.kind) + " and follows " +
                            quoted(input->name) + ", which is " + kind_name(input->kind));
        }
        if (output.row && *output.row >= shape.relation.size())
            return fail(name + " belongs to relation row " + std::to_string(*output.row) +
                        ", and there are " + std::to_string(shape.relation.size()));
    }
    return {};
}

}  // namespace

const InputPort* NodeShape::find_input(std::string_view name) const {
    for (const InputPort& input : inputs)
        if (input.name == name) return &input;
    return nullptr;
}

const OutputPort* NodeShape::find_output(std::string_view name) const {
    for (const OutputPort& output : outputs)
        if (output.name == name) return &output;
    return nullptr;
}

std::optional<std::string_view> NodeShape::clock_input() const {
    if (clock && clock->kind == Clock::Kind::Input) return std::string_view(clock->port);
    return std::nullopt;
}

Result<NodeShape> Shape::resolve(const Bound& bound) const {
    NodeShape shape = shape_;
    std::vector<std::string> clocks;
    for (const InputPort& input : shape.inputs)
        if (input.clock) clocks.push_back(input.name);
    if (clocks.empty()) {
        if (!shape.clock) return fail("the shape has no clock: mark an input `clock()`, or give a rate");
    } else if (clocks.size() == 1) {
        if (!shape.clock) {
            shape.clock = Clock::input(clocks[0]);
        } else if (shape.clock->kind != Clock::Kind::Input || shape.clock->port != clocks[0]) {
            return fail(quoted(clocks[0]) + " is the clock, and so is the shape's rate");
        }
    } else {
        std::string several;
        for (std::size_t n = 0; n < clocks.size(); ++n) several += (n ? " and " : "") + clocks[n];
        return fail("only one input can be the clock, not " + several);
    }

    std::set<std::string> names;
    for (const InputPort& input : shape.inputs)
        if (!names.insert(input.name).second) return fail("two inputs are named " + quoted(input.name));
    names.clear();
    for (const OutputPort& output : shape.outputs)
        if (!names.insert(output.name).second) return fail("two outputs are named " + quoted(output.name));

    std::optional<std::string> clock;
    if (auto name = shape.clock_input()) clock = std::string(*name);
    for (InputPort& input : shape.inputs)
        if (input.accepts.like && !bound.has(*input.accepts.like)) input.accepts.like.reset();

    std::vector<OutputPort> outputs;
    for (OutputPort& output : shape.outputs) {
        if (output.like) {
            std::string port;
            if (output.like->port) {
                port = *output.like->port;
            } else if (clock) {
                port = *clock;
            } else {
                return fail("output " + quoted(output.name) +
                            " takes its format from the clock input, and the clock is not an "
                            "input: give it `following(port)` or a format of its own");
            }
            const InputPort* input = shape.find_input(port);
            if (!input)
                return fail("output " + quoted(output.name) + " follows " + quoted(port) +
                            ", which is not an input");
            if (!bound.has(port)) continue;
            if (input->many)
                return fail("output " + quoted(output.name) + " follows " + quoted(port) +
                            ", which takes many streams");
            if (output.named_like) output.kind = input->kind;
            output.like->port = port;
        }
        outputs.push_back(std::move(output));
    }
    shape.outputs = std::move(outputs);
    FFRWD_TRY(check(shape, bound));
    return shape;
}

Bound::Bound(std::initializer_list<std::string_view> names) {
    for (std::string_view name : names) rebind(name, {std::nullopt});
}

Bound Bound::names(const std::vector<std::string>& names) {
    Bound bound;
    for (const std::string& name : names) {
        if (Binding* binding = bound.find(name)) {
            binding->streams.push_back({});
        } else {
            bound.inputs_.push_back({name, {StreamHint{}}});
        }
    }
    return bound;
}

Bound Bound::of(const std::vector<BoundStream>& streams) {
    Bound bound;
    for (const BoundStream& stream : streams) {
        if (Binding* binding = bound.find(stream.port)) {
            binding->streams.push_back(stream.hint);
        } else {
            bound.inputs_.push_back({stream.port, {stream.hint}});
        }
    }
    return bound;
}

const Binding* Bound::find(std::string_view port) const {
    for (const Binding& binding : inputs_)
        if (binding.input == port) return &binding;
    return nullptr;
}

Binding* Bound::find(std::string_view port) {
    for (Binding& binding : inputs_)
        if (binding.input == port) return &binding;
    return nullptr;
}

void Bound::rebind(std::string_view port, const std::vector<std::optional<Rational>>& rates) {
    std::vector<StreamHint> streams;
    for (const auto& rate : rates) streams.push_back({rate});
    if (Binding* binding = find(port)) {
        binding->streams = std::move(streams);
    } else {
        inputs_.push_back({std::string(port), std::move(streams)});
    }
}

void Bound::set_rate(std::string_view port, Rational rate) {
    if (!has(port)) rebind(port, {std::nullopt});
    for (StreamHint& hint : find(port)->streams) hint.rate = rate;
}

const std::vector<StreamHint>& Bound::streams(std::string_view port) const {
    const Binding* binding = find(port);
    return binding ? binding->streams : no_streams;
}

std::optional<Rational> Bound::rate_of(std::string_view port) const {
    const auto& hints = streams(port);
    if (hints.empty()) return std::nullopt;
    return hints.front().rate;
}

}  // namespace ffrwd
