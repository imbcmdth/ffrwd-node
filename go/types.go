package node

import (
	"encoding/json"
	"fmt"
	"unicode/utf8"
)

// The records the host hands a node, as plain Go: field for field the WIT's,
// an option as a pointer.

// ColorInfo is a stream's colorimetry, in ffmpeg's names.
type ColorInfo struct {
	Range, Primaries, Trc, Space string
}

// VideoFormat is the frames of a video stream: square pixels, tightly
// packed.
type VideoFormat struct {
	Width, Height uint32
	PixFmt        string
	Color         *ColorInfo
}

// AudioFormat is the samples of an audio stream, interleaved.
type AudioFormat struct {
	SampleRate, Channels uint32
	// "f32" or "s16".
	SampleFmt     string
	ChannelLayout *string
}

// StreamInfo is the stream a bound input reads, as the host knows it.
type StreamInfo struct {
	Index    uint32
	Kind     string
	Codec    string
	Duration *float64
	Tags     [][2]string
	TimeBase Rational
}

// Tag is the value of tag name, and whether the stream carries it.
func (s StreamInfo) Tag(name string) (string, bool) {
	for _, tag := range s.Tags {
		if tag[0] == name {
			return tag[1], true
		}
	}
	return "", false
}

// RenditionMeta is what the source read of one relation row.
type RenditionMeta struct {
	Name      *string
	Bandwidth *uint64
	Codecs    *string
	Language  *string
}

type CodedVideo struct {
	Width, Height     uint32
	SampleAspectRatio *Rational
	Color             *ColorInfo
}

type CodedAudio struct {
	SampleRate, Channels uint32
	ChannelLayout        *string
}

// CodedFormat is a coded stream's video or audio, or neither for data.
type CodedFormat struct {
	Video *CodedVideo
	Audio *CodedAudio
}

// CodedStream is one encoded stream: what a packets port carries.
type CodedStream struct {
	Codec     string
	TimeBase  Rational
	Format    CodedFormat
	Extradata []byte
	Profile   *int32
	Level     *int32
}

// Packet is one encoded packet, exactly as the encoder emitted it.
type Packet struct {
	Pts      int64
	Dts      *int64
	Duration *int64
	Keyframe bool
	Data     []byte
}

// Format is a stream's format, resolved: exactly one of its fields is set.
type Format struct {
	Video *VideoFormat
	Audio *AudioFormat
	// The codec of a data stream, "json" today.
	Data    *string
	Packets *CodedStream
}

func (f *Format) kind() Kind {
	switch {
	case f.Video != nil:
		return Video
	case f.Audio != nil:
		return Audio
	case f.Data != nil:
		return Data
	default:
		return Packets
	}
}

// StreamHint is what the compiler knows of one stream a call binds, before
// the run: a video stream's frame rate, an audio stream's sample rate over
// 1, nil where nothing settles it.
type StreamHint struct {
	Rate *Rational
}

// BoundStream is one stream bound to an input port at init.
type BoundStream struct {
	Port string
	// What every tick call names this stream by.
	ID          uint32
	Info        StreamInfo
	Format      *Format
	Rendition   RenditionMeta
	Row         *uint32
	DecodeDelay uint32
	Latency     *float64
	// The hint shape was asked with for this stream.
	Hint StreamHint
}

// NewBoundStream is a stream on port with nothing known about it but its
// time base.
func NewBoundStream(port string, id uint32, timeBase Rational) BoundStream {
	return BoundStream{Port: port, ID: id, Info: StreamInfo{TimeBase: timeBase}}
}

// VideoStream is a video stream of width x height in pixFmt.
func VideoStream(port string, id, width, height uint32, pixFmt string, timeBase Rational) BoundStream {
	stream := NewBoundStream(port, id, timeBase)
	stream.Info.Kind = "video"
	stream.Format = &Format{Video: &VideoFormat{Width: width, Height: height, PixFmt: pixFmt}}
	return stream
}

// AudioStream is an audio stream at sampleRate with channels interleaved,
// counted in samples, its hint at that rate.
func AudioStream(port string, id, sampleRate, channels uint32, sampleFmt string) BoundStream {
	stream := NewBoundStream(port, id, Rational{1, int32(sampleRate)}).WithRate(Rational{int32(sampleRate), 1})
	stream.Info.Kind = "audio"
	stream.Format = &Format{Audio: &AudioFormat{SampleRate: sampleRate, Channels: channels, SampleFmt: sampleFmt}}
	return stream
}

// RowsStream is a data stream of JSON rows.
func RowsStream(port string, id uint32, timeBase Rational) BoundStream {
	stream := NewBoundStream(port, id, timeBase)
	stream.Info.Kind = "data"
	stream.Info.Codec = "json"
	codec := "json"
	stream.Format = &Format{Data: &codec}
	return stream
}

// WithRate is the stream at rate, as the compiler told shape.
func (s BoundStream) WithRate(rate Rational) BoundStream {
	s.Hint.Rate = &rate
	return s
}

// VideoFormat is the stream's pictures, nil unless it is a video stream.
func (s *BoundStream) VideoFormat() *VideoFormat {
	if s.Format == nil {
		return nil
	}
	return s.Format.Video
}

// AudioFormat is the stream's samples, nil unless it is an audio stream.
func (s *BoundStream) AudioFormat() *AudioFormat {
	if s.Format == nil {
		return nil
	}
	return s.Format.Audio
}

// Frame is one frame as a tick sees it, without its bytes: a picture, or the
// tick's samples as one run.
type Frame struct {
	// In the stream's time base; for audio, the first sample's.
	Pts int64
	// Its place in the stream's frames this tick: what Fetch and Same name
	// it by.
	Index    uint32
	Duration *int64
	// The rows that rode in with it, each a JSON object.
	Rows []string
}

// Message is one message on a data input: for codec "json", one row.
type Message struct {
	Pts  int64
	Data []byte
}

// Decode reads the message as a row into v.
func (m Message) Decode(v any) error {
	if !utf8.Valid(m.Data) {
		return fmt.Errorf("the message at %d is not utf-8", m.Pts)
	}
	return Parse(string(m.Data), v)
}

// TimedRows is the rows a state input received on one tick an instance did
// not process.
type TimedRows struct {
	Pts  int64
	Rows []string
}

// FeedStart is where a hold input's current source starts.
type FeedStart struct {
	Tags [][2]string
	// Its first frame's pts, in its own time base.
	FirstPts int64
	// The clock time that first frame stands at, in the clock's time base.
	At int64
	// The clock time of the tick the start was fixed on.
	Known int64
}

// Feed is a hold input's current source.
type Feed struct {
	Start FeedStart
	// The clock time of the last tick it shows on, once the host can tell.
	Ends *int64
}

// Parse reads one JSON row into v.
func Parse(row string, v any) error {
	if err := json.Unmarshal([]byte(row), v); err != nil {
		return fmt.Errorf("the row %s does not read: %v", row, err)
	}
	return nil
}
