//! Write an ffrwd node module in Rust: implement [`Node`] for a type, hand it
//! to [`export!`], and build for `wasm32-wasip2`. The crate carries the
//! `ffrwd:av@0.19.1` bindings and does what every module would otherwise
//! write for itself: the call sequence, params read against their schema,
//! shapes from builders, time in any time base, state rows folded,
//! emissions checked to never go back, and errors as the run's message.
//!
//! Everything but the bindings builds on the host too, so a node's own tests
//! run there through [`mock::Harness`].

mod node;
mod out;
mod params;
mod rows;
mod shape;
mod tick;
mod time;
mod types;

pub mod mock;

#[cfg(target_arch = "wasm32")]
#[doc(hidden)]
pub mod __glue;

pub use node::{Error, Init, Meta, Node, Result, Runner, StateRow};
pub use out::{Emission, Emitted, Out, Payload};
pub use params::{read as read_params, NoParams, NO_PARAMS};
pub use rows::{parse, schema_of, Cue, Cues, Span, Spans};
pub use shape::{
    Accepts, Anchor, Binding, Bound, Clock, Hold, Input, Interval, Kind, Like, Output, Pairing,
    RowsUse, Shape, Wants,
};
pub use tick::Tick;
pub use time::Rational;
pub use types::{
    AudioFormat, BoundStream, CodedAudio, CodedFormat, CodedStream, CodedVideo, ColorInfo, Feed,
    FeedStart, Format, Frame, Message, Packet, RenditionMeta, StreamHint, StreamInfo, TimedRows,
    VideoFormat,
};

/// Exports `$node`, a type implementing [`Node`], as the module's `node`.
/// Once per module, at its crate root. On a target other than wasm32 there
/// is nothing to export, and the node only counts as used, so the module's
/// tests build on the host without warnings.
#[macro_export]
#[rustfmt::skip]
macro_rules! export {
    ($node:ty) => {
        #[cfg(target_arch = "wasm32")]
        const _: () = {
            type FfrwdNode = $crate::__glue::Glue<$node>;
            $crate::__glue::bindings::export!(FfrwdNode with_types_in $crate::__glue::bindings);
        };
        #[cfg(not(target_arch = "wasm32"))]
        const _: () = {
            let _ = $crate::Runner::<$node>::init;
        };
    };
}
