#pragma once

// Write an ffrwd node module in C++: derive a type from `ffrwd::Node`, hand
// it to `FFRWD_EXPORT`, and build it with wasi-sdk for wasm32-wasip2. The
// library carries the `ffrwd:av@0.19.1` bindings and does what every module
// would otherwise write for itself: the call sequence, params read against
// their schema, shapes from builders, time in any time base, state rows
// folded, emissions checked to never go back, and errors as the run's
// message. Everything but the bindings builds on the host too, so a node's
// own tests run there through `ffrwd::mock::Harness`.

#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "bytes.hpp"
#include "fields.hpp"
#include "json.hpp"
#include "out.hpp"
#include "params.hpp"
#include "result.hpp"
#include "rows.hpp"
#include "runner.hpp"
#include "shape.hpp"
#include "tick.hpp"
#include "time.hpp"
#include "types.hpp"

namespace ffrwd::detail {

/// The node a module exports, behind the five calls of the `node` interface.
class Module {
public:
    virtual ~Module() = default;
    virtual Meta describe() = 0;
    virtual Result<NodeShape> shape(std::string_view params, const Bound& bound) = 0;
    virtual Status init(std::vector<BoundStream> bound, std::vector<std::string> latched,
                        std::string_view params) = 0;
    virtual Status set_params(std::string_view params) = 0;
    virtual Result<Emitted> process(const Source& tick) = 0;
};

template <NodeType N>
class ModuleOf final : public Module {
public:
    Meta describe() override { return Runner<N>::describe(); }

    Result<NodeShape> shape(std::string_view params, const Bound& bound) override {
        return Runner<N>::shape(params, bound);
    }

    Status init(std::vector<BoundStream> bound, std::vector<std::string> latched,
                std::string_view params) override {
        FFRWD_LET(runner, Runner<N>::init(std::move(bound), std::move(latched), params));
        runner_.emplace(std::move(runner));
        return {};
    }

    Status set_params(std::string_view params) override {
        if (!runner_) return fail(std::string(N::name) + " was called before init");
        return runner_->set_params(params);
    }

    Result<Emitted> process(const Source& tick) override {
        if (!runner_) return fail(std::string(N::name) + " was called before init");
        return runner_->process(tick);
    }

private:
    std::optional<Runner<N>> runner_;
};

/// The module's node, which `FFRWD_EXPORT` defines.
Module& exported();

}  // namespace ffrwd::detail

/// Exports `N`, a type deriving from `ffrwd::Node`, as the module's `node`.
/// Once per module, at file scope. Built for anything but wasm there is
/// nothing to export, and the node is only checked, so the module's tests
/// build on the host.
#if defined(__wasm__)
#define FFRWD_EXPORT(N)                                       \
    ::ffrwd::detail::Module& ::ffrwd::detail::exported() {    \
        static ::ffrwd::detail::ModuleOf<N> module;           \
        return module;                                        \
    }
#else
#define FFRWD_EXPORT(N) static_assert(::ffrwd::NodeType<N>, #N " is a node")
#endif
