//! The bindings and the conversions between them and the crate's own types.
//! What [`export!`](crate::export) expands to reaches in here.

use std::any::Any;
use std::cell::RefCell;
use std::marker::PhantomData;

pub mod bindings {
    wit_bindgen::generate!({
        path: "wit",
        world: "ffrwd:av/node-module@0.19.0",
        pub_export_macro: true,
        default_bindings_module: "ffrwd_node::__glue::bindings",
    });
}

use bindings::exports::ffrwd::av::node as wit_node;
use bindings::ffrwd::av::node_tick as wit_tick;
use bindings::ffrwd::av::node_types as nt;
use bindings::ffrwd::av::types as wt;

use crate::node::{Node, Runner};
use crate::out::{Emitted, Payload};
use crate::shape::{
    Accepts, Anchor, Clock, Input, Interval, Kind, Output, Pairing, RowsUse, Shape, Wants,
};
use crate::tick::Source;
use crate::types::{
    AudioFormat, BoundStream, CodedAudio, CodedFormat, CodedStream, CodedVideo, ColorInfo, Feed,
    FeedStart, Format, Frame, Message, Packet, RenditionMeta, StreamInfo, TimedRows, VideoFormat,
};
use crate::Rational;

thread_local! {
    static INSTANCE: RefCell<Option<Box<dyn Any>>> = const { RefCell::new(None) };
}

/// The `Guest` the bindings export, for node `N`.
pub struct Glue<N>(PhantomData<N>);

impl<N: Node> wit_node::Guest for Glue<N> {
    fn describe() -> wt::Meta {
        let meta = Runner::<N>::describe();
        wt::Meta {
            name: meta.name,
            version: meta.version,
            params_schema: meta.params_schema,
            rows_schema: meta.rows_schema,
            pixel_formats: Vec::new(),
            sample_formats: Vec::new(),
            sample_rates: Vec::new(),
            channel_counts: Vec::new(),
            rows_language: meta.rows_language,
        }
    }

    fn shape(params: String, bound: Vec<String>) -> Result<nt::NodeShape, String> {
        Runner::<N>::shape(&params, &bound).map(node_shape)
    }

    fn init(
        bound: Vec<nt::BoundStream>,
        latched: Vec<String>,
        params: String,
    ) -> Result<(), String> {
        let bound = bound.into_iter().map(bound_stream).collect();
        let runner = Runner::<N>::init(bound, latched, &params)?;
        INSTANCE.with(|instance| *instance.borrow_mut() = Some(Box::new(runner)));
        Ok(())
    }

    fn set_params(params: String) -> Result<(), String> {
        with_runner::<N, _>(|runner| runner.set_params(&params))
    }

    fn process(tick: &wit_tick::Tick) -> Result<wit_node::Emitted, String> {
        with_runner::<N, _>(|runner| runner.process_source(tick)).map(emitted)
    }
}

fn with_runner<N: Node, T>(
    call: impl FnOnce(&mut Runner<N>) -> Result<T, String>,
) -> Result<T, String> {
    INSTANCE.with(|instance| {
        let mut instance = instance.borrow_mut();
        let runner = instance
            .as_mut()
            .and_then(|runner| runner.downcast_mut::<Runner<N>>())
            .ok_or_else(|| format!("{} was called before init", N::NAME))?;
        call(runner)
    })
}

impl Source for wit_tick::Tick {
    fn pts(&self) -> i64 {
        wit_tick::Tick::pts(self)
    }

    fn time_base(&self) -> Rational {
        rational(wit_tick::Tick::time_base(self))
    }

    fn last(&self) -> bool {
        wit_tick::Tick::last(self)
    }

    fn streams(&self, port: &str) -> Vec<u32> {
        wit_tick::Tick::streams(self, port)
    }

    fn info(&self, id: u32) -> StreamInfo {
        stream_info(wit_tick::Tick::info(self, id))
    }

    fn feed(&self, id: u32) -> Option<Feed> {
        wit_tick::Tick::feed(self, id).map(|feed| Feed {
            start: FeedStart {
                tags: feed.start.tags,
                first_pts: feed.start.first_pts,
                at: feed.start.at,
            },
            ends: feed.ends,
        })
    }

    fn frames(&self, id: u32) -> Vec<Frame> {
        wit_tick::Tick::frames(self, id)
            .into_iter()
            .map(|frame| Frame {
                pts: frame.pts,
                index: frame.index,
                duration: frame.duration,
                rows: frame.rows,
            })
            .collect()
    }

    fn fetch(&self, id: u32, index: u32) -> Vec<u8> {
        wit_tick::Tick::fetch(self, id, index)
    }

    fn messages(&self, id: u32) -> Vec<Message> {
        wit_tick::Tick::messages(self, id)
            .into_iter()
            .map(|message| Message {
                pts: message.pts,
                data: message.data,
            })
            .collect()
    }

    fn packets(&self, id: u32) -> Vec<Packet> {
        wit_tick::Tick::packets(self, id)
            .into_iter()
            .map(packet)
            .collect()
    }

    fn earlier_rows(&self, id: u32) -> Vec<TimedRows> {
        wit_tick::Tick::earlier_rows(self, id)
            .into_iter()
            .map(|timed| TimedRows {
                pts: timed.pts,
                rows: timed.rows,
            })
            .collect()
    }
}

fn rational(value: wt::Rational) -> Rational {
    Rational::new(value.num, value.den)
}

fn wit_rational(value: Rational) -> wt::Rational {
    wt::Rational {
        num: value.num,
        den: value.den,
    }
}

fn color(value: wt::ColorInfo) -> ColorInfo {
    ColorInfo {
        range: value.range,
        primaries: value.primaries,
        trc: value.trc,
        space: value.space,
    }
}

fn wit_color(value: ColorInfo) -> wt::ColorInfo {
    wt::ColorInfo {
        range: value.range,
        primaries: value.primaries,
        trc: value.trc,
        space: value.space,
    }
}

fn stream_info(value: wt::StreamInfo) -> StreamInfo {
    StreamInfo {
        index: value.index,
        kind: value.kind,
        codec: value.codec,
        duration: value.duration,
        tags: value.tags,
        time_base: rational(value.time_base),
    }
}

fn packet(value: wt::Packet) -> Packet {
    Packet {
        pts: value.pts,
        dts: value.dts,
        duration: value.duration,
        keyframe: value.keyframe,
        data: value.data,
    }
}

fn wit_packet(value: Packet) -> wt::Packet {
    wt::Packet {
        pts: value.pts,
        dts: value.dts,
        duration: value.duration,
        keyframe: value.keyframe,
        data: value.data,
    }
}

fn coded_stream(value: wt::CodedStream) -> CodedStream {
    CodedStream {
        codec: value.codec,
        time_base: rational(value.time_base),
        format: match value.format {
            wt::CodedFormat::Video(video) => CodedFormat::Video(CodedVideo {
                width: video.width,
                height: video.height,
                sample_aspect_ratio: video.sample_aspect_ratio.map(rational),
                color: video.color.map(color),
            }),
            wt::CodedFormat::Audio(audio) => CodedFormat::Audio(CodedAudio {
                sample_rate: audio.sample_rate,
                channels: audio.channels,
                channel_layout: audio.channel_layout,
            }),
            wt::CodedFormat::Data => CodedFormat::Data,
        },
        extradata: value.extradata,
        profile: value.profile,
        level: value.level,
    }
}

fn wit_coded_stream(value: CodedStream) -> wt::CodedStream {
    wt::CodedStream {
        codec: value.codec,
        time_base: wit_rational(value.time_base),
        format: match value.format {
            CodedFormat::Video(video) => wt::CodedFormat::Video(wt::CodedVideo {
                width: video.width,
                height: video.height,
                sample_aspect_ratio: video.sample_aspect_ratio.map(wit_rational),
                color: video.color.map(wit_color),
            }),
            CodedFormat::Audio(audio) => wt::CodedFormat::Audio(wt::CodedAudio {
                sample_rate: audio.sample_rate,
                channels: audio.channels,
                channel_layout: audio.channel_layout,
            }),
            CodedFormat::Data => wt::CodedFormat::Data,
        },
        extradata: value.extradata,
        profile: value.profile,
        level: value.level,
    }
}

fn video_format(value: wt::VideoFormat) -> VideoFormat {
    VideoFormat {
        width: value.width,
        height: value.height,
        pix_fmt: value.pix_fmt,
        color: value.color.map(color),
    }
}

fn wit_video_format(value: VideoFormat) -> wt::VideoFormat {
    wt::VideoFormat {
        width: value.width,
        height: value.height,
        pix_fmt: value.pix_fmt,
        color: value.color.map(wit_color),
    }
}

fn audio_format(value: wt::AudioFormat) -> AudioFormat {
    AudioFormat {
        sample_rate: value.sample_rate,
        channels: value.channels,
        sample_fmt: value.sample_fmt,
        channel_layout: value.channel_layout,
    }
}

fn wit_audio_format(value: AudioFormat) -> wt::AudioFormat {
    wt::AudioFormat {
        sample_rate: value.sample_rate,
        channels: value.channels,
        sample_fmt: value.sample_fmt,
        channel_layout: value.channel_layout,
    }
}

fn bound_stream(value: nt::BoundStream) -> BoundStream {
    BoundStream {
        port: value.port,
        id: value.id,
        info: stream_info(value.info),
        format: value.format.and_then(|format| match format {
            nt::OutputFormat::Video(video) => Some(Format::Video(video_format(video))),
            nt::OutputFormat::Audio(audio) => Some(Format::Audio(audio_format(audio))),
            nt::OutputFormat::Data(codec) => Some(Format::Data(codec)),
            nt::OutputFormat::Packets(coded) => Some(Format::Packets(coded_stream(coded))),
            nt::OutputFormat::Like(_) => None,
        }),
        rendition: RenditionMeta {
            name: value.rendition.name,
            bandwidth: value.rendition.bandwidth,
            codecs: value.rendition.codecs,
            language: value.rendition.language,
        },
        row: value.row,
        decode_delay: value.decode_delay,
        latency: value.latency,
    }
}

fn port_kind(kind: Kind) -> nt::PortKind {
    match kind {
        Kind::Video => nt::PortKind::Video,
        Kind::Audio => nt::PortKind::Audio,
        Kind::Data => nt::PortKind::Data,
        Kind::Packets => nt::PortKind::Packets,
    }
}

fn input_port(input: Input) -> nt::InputPort {
    let pairing = match input.pairing {
        Pairing::Lockstep => nt::Pairing::Lockstep,
        Pairing::Arrival => nt::Pairing::Arrival,
        Pairing::Hold(hold) => nt::Pairing::Hold(nt::Hold {
            anchor: match hold.anchor {
                Anchor::SharedClock => nt::Anchor::SharedClock,
                Anchor::FirstFrame => nt::Anchor::FirstFrame,
                Anchor::Tagged(tag) => nt::Anchor::Tagged(tag),
            },
            lead: hold.lead,
            linger: hold.linger,
            timeout: hold.timeout,
            group: hold.group,
            port_param: hold.port_param,
        }),
        Pairing::Interval(Interval { latency, ahead }) => {
            nt::Pairing::Interval(nt::Interval { latency, ahead })
        }
    };
    let Accepts {
        pixel_formats,
        sample_formats,
        sample_rates,
        channel_counts,
        codecs,
        wants,
        like,
    } = input.accepts;
    nt::InputPort {
        name: input.name,
        kind: port_kind(input.kind),
        required: input.required,
        many: input.many,
        pairing,
        rows: match input.rows {
            RowsUse::Ignore => nt::RowsUse::Ignore,
            RowsUse::PerFrame => nt::RowsUse::PerFrame,
            RowsUse::State => nt::RowsUse::State,
        },
        window: input.window,
        stride: input.stride,
        accepts: nt::Accepts {
            pixel_formats,
            sample_formats,
            sample_rates,
            channel_counts,
            codecs,
            wants: match wants {
                Wants::All => wt::Wants::All,
                Wants::Keyframes => wt::Wants::Keyframes,
                Wants::First => wt::Wants::First,
            },
            like,
        },
        schema: input.schema,
    }
}

fn output_port(output: Output) -> nt::OutputPort {
    let format = match (output.like, output.format) {
        (Some(like), _) => Some(nt::OutputFormat::Like(nt::LikeInput {
            port: like.port.unwrap_or_default(),
            pixel_format: like.pixel_format,
            sample_format: like.sample_format,
        })),
        (None, Some(Format::Video(video))) => {
            Some(nt::OutputFormat::Video(wit_video_format(video)))
        }
        (None, Some(Format::Audio(audio))) => {
            Some(nt::OutputFormat::Audio(wit_audio_format(audio)))
        }
        (None, Some(Format::Data(codec))) => Some(nt::OutputFormat::Data(codec)),
        (None, Some(Format::Packets(coded))) => {
            Some(nt::OutputFormat::Packets(wit_coded_stream(coded)))
        }
        (None, None) => None,
    };
    nt::OutputPort {
        name: output.name,
        kind: port_kind(output.kind),
        format,
        time_base: output.time_base.map(wit_rational),
        latency: output.latency,
        schema: output.schema,
        row: output.row,
    }
}

fn node_shape(shape: Shape) -> nt::NodeShape {
    nt::NodeShape {
        inputs: shape.inputs.into_iter().map(input_port).collect(),
        outputs: shape.outputs.into_iter().map(output_port).collect(),
        clock: match shape.clock {
            Some(Clock::Input(name)) => nt::Clock::Input(name),
            Some(Clock::Rate(rate)) => nt::Clock::Rate(wit_rational(rate)),
            Some(Clock::RateOf(name)) => nt::Clock::RateOf(name),
            Some(Clock::SelfClocked) | None => nt::Clock::SelfClocked,
        },
        pure: shape.pure,
        one_to_one: shape.one_to_one,
        bounded: shape.bounded,
        relation: shape.relation,
    }
}

fn emitted(value: Emitted) -> wit_node::Emitted {
    wit_node::Emitted {
        items: value
            .items
            .into_iter()
            .map(|item| wit_node::Emission {
                port: item.port,
                payload: match item.payload {
                    Payload::Frame {
                        pts,
                        duration,
                        data,
                    } => wit_node::Payload::Frame(wt::RawFrame {
                        pts,
                        duration,
                        data,
                    }),
                    Payload::Same {
                        pts,
                        duration,
                        id,
                        index,
                    } => wit_node::Payload::Same(wit_node::SameFrame {
                        pts,
                        duration,
                        id,
                        index,
                    }),
                    Payload::Message { pts, data } => {
                        wit_node::Payload::Message(nt::Message { pts, data })
                    }
                    Payload::Packet(value) => wit_node::Payload::Packet(wit_packet(value)),
                },
            })
            .collect(),
        rows: value.reports,
        finished: value.finished,
    }
}
