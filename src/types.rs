//! The records the host hands a node, as plain Rust: what `init` binds, what a
//! tick holds, what a packet carries. Field for field the WIT's, so its doc
//! comments are the contract.

use crate::Rational;

/// A stream's colorimetry, in ffmpeg's names: `tv` or `pc`, `bt709` and the
/// like, "unknown" where the wire does not settle a field.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct ColorInfo {
    pub range: String,
    pub primaries: String,
    pub trc: String,
    pub space: String,
}

/// The frames of a video stream: square pixels, tightly packed.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct VideoFormat {
    pub width: u32,
    pub height: u32,
    pub pix_fmt: String,
    pub color: Option<ColorInfo>,
}

/// The samples of an audio stream, interleaved.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct AudioFormat {
    pub sample_rate: u32,
    pub channels: u32,
    /// "f32" or "s16".
    pub sample_fmt: String,
    pub channel_layout: Option<String>,
}

/// The stream a bound input reads, as the host knows it.
#[derive(Clone, Debug, PartialEq)]
pub struct StreamInfo {
    pub index: u32,
    pub kind: String,
    pub codec: String,
    /// Seconds, where known.
    pub duration: Option<f64>,
    pub tags: Vec<(String, String)>,
    /// What the stream's timestamps count.
    pub time_base: Rational,
}

impl StreamInfo {
    /// A stream with nothing known about it but its time base.
    pub fn new(time_base: Rational) -> StreamInfo {
        StreamInfo {
            index: 0,
            kind: String::new(),
            codec: String::new(),
            duration: None,
            tags: Vec::new(),
            time_base,
        }
    }

    /// The value of tag `name`, if the stream carries it.
    pub fn tag(&self, name: &str) -> Option<&str> {
        self.tags
            .iter()
            .find(|(key, _)| key == name)
            .map(|(_, value)| value.as_str())
    }
}

/// What the source read of one relation row.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct RenditionMeta {
    pub name: Option<String>,
    pub bandwidth: Option<u64>,
    pub codecs: Option<String>,
    pub language: Option<String>,
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct CodedVideo {
    pub width: u32,
    pub height: u32,
    pub sample_aspect_ratio: Option<Rational>,
    pub color: Option<ColorInfo>,
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct CodedAudio {
    pub sample_rate: u32,
    pub channels: u32,
    pub channel_layout: Option<String>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum CodedFormat {
    Video(CodedVideo),
    Audio(CodedAudio),
    Data,
}

/// One encoded stream: what a packets port carries.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct CodedStream {
    pub codec: String,
    pub time_base: Rational,
    pub format: CodedFormat,
    pub extradata: Vec<u8>,
    pub profile: Option<i32>,
    pub level: Option<i32>,
}

/// One encoded packet, exactly as the encoder emitted it.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Packet {
    pub pts: i64,
    pub dts: Option<i64>,
    pub duration: Option<i64>,
    pub keyframe: bool,
    pub data: Vec<u8>,
}

/// A stream's format, resolved.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Format {
    Video(VideoFormat),
    Audio(AudioFormat),
    /// The codec of a data stream, "json" today.
    Data(String),
    Packets(CodedStream),
}

/// What the compiler knows of one stream a call binds, before the run.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct StreamHint {
    /// Video: the frame rate. Audio: the sample rate over 1. None where
    /// nothing settles it before the run: a self-clocked source's output, a
    /// feed by port.
    pub rate: Option<Rational>,
}

/// One stream bound to an input port at `init`.
#[derive(Clone, Debug, PartialEq)]
pub struct BoundStream {
    pub port: String,
    /// What every tick call names this stream by.
    pub id: u32,
    pub info: StreamInfo,
    pub format: Option<Format>,
    pub rendition: RenditionMeta,
    pub row: Option<u32>,
    pub decode_delay: u32,
    pub latency: Option<f64>,
    /// The hint `shape` was asked with for this stream.
    pub hint: StreamHint,
}

impl BoundStream {
    /// A stream on `port` with nothing known about it but its time base.
    pub fn new(port: &str, id: u32, time_base: Rational) -> BoundStream {
        BoundStream {
            port: port.to_owned(),
            id,
            info: StreamInfo::new(time_base),
            format: None,
            rendition: RenditionMeta::default(),
            row: None,
            decode_delay: 0,
            latency: None,
            hint: StreamHint::default(),
        }
    }

    /// The stream at `rate`, as the compiler told `shape`: a video stream's
    /// frame rate.
    pub fn rate(mut self, rate: Rational) -> BoundStream {
        self.hint.rate = Some(rate);
        self
    }

    /// A video stream of `width` x `height` in `pix_fmt`.
    pub fn video(
        port: &str,
        id: u32,
        width: u32,
        height: u32,
        pix_fmt: &str,
        time_base: Rational,
    ) -> BoundStream {
        let mut stream = BoundStream::new(port, id, time_base);
        stream.info.kind = "video".to_owned();
        stream.format = Some(Format::Video(VideoFormat {
            width,
            height,
            pix_fmt: pix_fmt.to_owned(),
            color: None,
        }));
        stream
    }

    /// An audio stream at `sample_rate` with `channels` interleaved, in
    /// `sample_fmt`, counted in samples, its hint at that rate.
    pub fn audio(
        port: &str,
        id: u32,
        sample_rate: u32,
        channels: u32,
        sample_fmt: &str,
    ) -> BoundStream {
        let mut stream = BoundStream::new(port, id, Rational::new(1, sample_rate as i32))
            .rate(Rational::new(sample_rate as i32, 1));
        stream.info.kind = "audio".to_owned();
        stream.format = Some(Format::Audio(AudioFormat {
            sample_rate,
            channels,
            sample_fmt: sample_fmt.to_owned(),
            channel_layout: None,
        }));
        stream
    }

    /// A data stream of JSON rows.
    pub fn rows(port: &str, id: u32, time_base: Rational) -> BoundStream {
        let mut stream = BoundStream::new(port, id, time_base);
        stream.info.kind = "data".to_owned();
        stream.info.codec = "json".to_owned();
        stream.format = Some(Format::Data("json".to_owned()));
        stream
    }

    /// The stream's frames, when it is a video stream.
    pub fn video_format(&self) -> Option<&VideoFormat> {
        match &self.format {
            Some(Format::Video(video)) => Some(video),
            _ => None,
        }
    }

    /// The stream's samples, when it is an audio stream.
    pub fn audio_format(&self) -> Option<&AudioFormat> {
        match &self.format {
            Some(Format::Audio(audio)) => Some(audio),
            _ => None,
        }
    }
}

/// One frame as a tick sees it, without its bytes: a picture, or the tick's
/// samples as one run.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Frame {
    /// In the stream's time base; for audio, the first sample's.
    pub pts: i64,
    /// Its place in the stream's frames this tick: what `fetch` and `same`
    /// name it by.
    pub index: u32,
    pub duration: Option<i64>,
    /// The rows that rode in with it, each a JSON object.
    pub rows: Vec<String>,
}

impl Frame {
    /// The rows riding this frame, each read as a `T`.
    pub fn rows<T: serde::de::DeserializeOwned>(&self) -> Result<Vec<T>, String> {
        self.rows
            .iter()
            .map(|row| crate::rows::parse(row))
            .collect()
    }
}

/// One message on a data input: for codec "json", one row.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Message {
    pub pts: i64,
    pub data: Vec<u8>,
}

impl Message {
    /// The message read as a `T`.
    pub fn row<T: serde::de::DeserializeOwned>(&self) -> Result<T, String> {
        let text = std::str::from_utf8(&self.data)
            .map_err(|_| format!("the message at {} is not utf-8", self.pts))?;
        crate::rows::parse(text)
    }
}

/// The rows a state input received on one tick an instance did not process.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct TimedRows {
    pub pts: i64,
    pub rows: Vec<String>,
}

/// Where a hold input's current source starts.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct FeedStart {
    pub tags: Vec<(String, String)>,
    /// Its first frame's pts, in its own time base.
    pub first_pts: i64,
    /// The clock time that first frame stands at, in the clock's time base.
    pub at: i64,
    /// The clock time of the tick the start was fixed on: seconds before
    /// `at` for a timed source, `at` itself when fixed on the tick it
    /// showed. What a countdown counts from.
    pub known: i64,
}

/// A hold input's current source, from the tick its first frame shows on to
/// the last its last frame shows on.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Feed {
    pub start: FeedStart,
    /// The clock time of the last tick it shows on, once the host can tell.
    pub ends: Option<i64>,
}
