#include "ffrwd/node.hpp"

struct Passthrough : ffrwd::Node<Passthrough> {
    static constexpr std::string_view name = "passthrough";
    static constexpr std::string_view version = "0.1.0";

    std::uint32_t v = 0;

    static ffrwd::Result<ffrwd::Shape> shape(const ffrwd::NoParams&, const ffrwd::Bound&) {
        return ffrwd::Shape()
            .input(ffrwd::Input::video("v").clock().pixel_formats({"rgba"}))
            .output(ffrwd::Output::like("v"))
            .pure()
            .one_to_one();
    }

    static ffrwd::Result<Passthrough> init(ffrwd::NoParams, const ffrwd::Init& init) {
        FFRWD_LET(v, init.stream("v"));
        Passthrough node;
        node.v = v.id;
        return node;
    }

    ffrwd::Status process(const ffrwd::Tick& tick, ffrwd::Out& out) {
        auto frame = tick.frame(v);
        if (!frame) return {};
        // Your work goes here: `tick.fetch` reads the picture, `out.frame` sends a new one.
        return out.pass("v", v, *frame);
    }
};

FFRWD_EXPORT(Passthrough);
