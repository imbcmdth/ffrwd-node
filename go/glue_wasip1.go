//go:build wasip1

package node

import (
	"fmt"
	"runtime"
	"runtime/debug"
	"runtime/metrics"

	exports "github.com/imbcmdth/ffrwd-node/go/internal/wit/export/export_ffrwd_av_node"
	wn "github.com/imbcmdth/ffrwd-node/go/internal/wit/ffrwd_av_node"
	wtick "github.com/imbcmdth/ffrwd-node/go/internal/wit/ffrwd_av_node_tick"
	nt "github.com/imbcmdth/ffrwd-node/go/internal/wit/ffrwd_av_node_types"
	wt "github.com/imbcmdth/ffrwd-node/go/internal/wit/ffrwd_av_types"
	_ "github.com/imbcmdth/ffrwd-node/go/internal/wit/wit_exports"
	witRuntime "go.bytecodealliance.org/pkg/wit/runtime"
	witTypes "go.bytecodealliance.org/pkg/wit/types"
)

// The collector never runs on its own. The host lowers a call's arguments
// through cabi_realloc, an allocation there can start a GC assist, and the
// mark phase reads the clock, a wasi import: an import called from inside
// cabi_realloc is a reentrancy the host traps ("cannot leave component
// instance"). The heap is collected instead at the top of process, where an
// import is allowed, once collectEvery bytes have been allocated since the
// last collection.
//
// cabi_realloc also pins everything it allocates in the bindings' one
// pinner, which only shape, init and set-params release. The bytes of every
// frame a process call fetches land there, so a node that never takes new
// params would hold every frame it ever read. Process releases it first:
// what the previous call fetched is held by Go references, or by nothing.
const collectEvery = 256 << 20

var collectedAt uint64

func init() {
	debug.SetGCPercent(-1)
	exports.Node = glue{}
}

func collect() {
	sample := []metrics.Sample{{Name: "/gc/heap/allocs:bytes"}}
	metrics.Read(sample)
	if allocated := sample[0].Value.Uint64(); allocated-collectedAt >= collectEvery {
		runtime.GC()
		collectedAt = allocated
	}
}

var instance opened

type glue struct{}

func guarded[T any](call func() (T, error)) (result witTypes.Result[T, string]) {
	defer func() {
		if caught := recover(); caught != nil {
			result = witTypes.Err[T, string](fmt.Sprint(caught))
		}
	}()
	value, err := call()
	if err != nil {
		return witTypes.Err[T, string](err.Error())
	}
	return witTypes.Ok[T, string](value)
}

func unit(call func() error) witTypes.Result[witTypes.Unit, string] {
	return guarded(func() (witTypes.Unit, error) { return witTypes.Unit{}, call() })
}

func defined() (exportable, error) {
	if exported == nil {
		return nil, fmt.Errorf("the module exports no node: call node.Export from its init")
	}
	return exported, nil
}

func (glue) Describe() wt.Meta {
	meta := Meta{Name: "?", Version: "0.0.0", ParamsSchema: NoParams}
	if exported != nil {
		meta = exported.Describe()
	}
	return wt.Meta{
		Name:          meta.Name,
		Version:       meta.Version,
		ParamsSchema:  meta.ParamsSchema,
		RowsSchema:    meta.RowsSchema,
		PixelFormats:  []string{},
		SampleFormats: []string{},
		SampleRates:   []uint32{},
		ChannelCounts: []uint32{},
		RowsLanguage:  append([]string{}, meta.RowsLanguage...),
	}
}

func (glue) Shape(params string, bound []nt.Binding) witTypes.Result[nt.NodeShape, string] {
	return guarded(func() (nt.NodeShape, error) {
		node, err := defined()
		if err != nil {
			return nt.NodeShape{}, err
		}
		bindings := make([]Binding, len(bound))
		for i, binding := range bound {
			bindings[i] = Binding{Input: binding.Input}
			for _, hint := range binding.Streams {
				bindings[i].Streams = append(bindings[i].Streams, StreamHint{Rate: option(hint.Rate, rational)})
			}
		}
		shape, err := node.ResolveShape(params, BoundFrom(bindings))
		if err != nil {
			return nt.NodeShape{}, err
		}
		return witShape(shape), nil
	})
}

func (glue) Init(bound []nt.BoundStream, latched []string, params string) witTypes.Result[witTypes.Unit, string] {
	return unit(func() error {
		node, err := defined()
		if err != nil {
			return err
		}
		instance = nil
		streams := make([]BoundStream, len(bound))
		for i, stream := range bound {
			streams[i] = boundStream(stream)
		}
		instance, err = node.open(streams, latched, params)
		return err
	})
}

func (glue) SetParams(params string) witTypes.Result[witTypes.Unit, string] {
	return unit(func() error {
		if instance == nil {
			return fmt.Errorf("%s was called before init", exported.name())
		}
		return instance.SetParams(params)
	})
}

func (glue) Process(tick *wtick.Tick) witTypes.Result[wn.Emitted, string] {
	// The bindings leave the borrowed tick to the module, and a call that
	// returns holding one is refused.
	defer tick.Drop()
	witRuntime.Unpin()
	collect()
	return guarded(func() (wn.Emitted, error) {
		if instance == nil {
			return wn.Emitted{}, fmt.Errorf("%s was called before init", exported.name())
		}
		emitted, err := instance.Process(hostTick{tick})
		if err != nil {
			return wn.Emitted{}, err
		}
		return witEmitted(emitted), nil
	})
}

type hostTick struct {
	tick *wtick.Tick
}

func (h hostTick) Pts() int64                    { return h.tick.Pts() }
func (h hostTick) Ordinal() uint64               { return h.tick.Ordinal() }
func (h hostTick) TimeBase() Rational            { return rational(h.tick.TimeBase()) }
func (h hostTick) Last() bool                    { return h.tick.Last() }
func (h hostTick) Streams(port string) []uint32  { return h.tick.Streams(port) }
func (h hostTick) Info(id uint32) StreamInfo     { return streamInfo(h.tick.Info(id)) }
func (h hostTick) Fetch(id, index uint32) []byte { return h.tick.Fetch(id, index) }

func (h hostTick) Feed(id uint32) *Feed {
	return option(h.tick.Feed(id), feed)
}

func (h hostTick) EndedFeeds(id uint32) []Feed {
	return each(h.tick.EndedFeeds(id), feed)
}

func (h hostTick) Frames(id uint32) []Frame {
	return each(h.tick.Frames(id), func(frame nt.Frame) Frame {
		return Frame{Pts: frame.Pts, Index: frame.Index, Duration: option(frame.Duration, same), Rows: frame.Rows}
	})
}

func (h hostTick) Messages(id uint32) []Message {
	return each(h.tick.Messages(id), func(message nt.Message) Message {
		return Message{Pts: message.Pts, Data: message.Data}
	})
}

func (h hostTick) Packets(id uint32) []Packet {
	return each(h.tick.Packets(id), func(packet wt.Packet) Packet {
		return Packet{
			Pts:      packet.Pts,
			Dts:      option(packet.Dts, same),
			Duration: option(packet.Duration, same),
			Keyframe: packet.Keyframe,
			Data:     packet.Data,
		}
	})
}

func (h hostTick) EarlierRows(id uint32) []TimedRows {
	return each(h.tick.EarlierRows(id), func(timed nt.TimedRows) TimedRows {
		return TimedRows{Pts: timed.Pts, Rows: timed.Rows}
	})
}

func same[T any](value T) T { return value }

func option[T, U any](value witTypes.Option[T], convert func(T) U) *U {
	if value.IsNone() {
		return nil
	}
	converted := convert(value.Some())
	return &converted
}

func witOption[T, U any](value *T, convert func(T) U) witTypes.Option[U] {
	if value == nil {
		return witTypes.None[U]()
	}
	return witTypes.Some(convert(*value))
}

func each[T, U any](values []T, convert func(T) U) []U {
	converted := make([]U, len(values))
	for i, value := range values {
		converted[i] = convert(value)
	}
	return converted
}

func rational(value wt.Rational) Rational {
	return Rational{value.Num, value.Den}
}

func witRational(value Rational) wt.Rational {
	return wt.Rational{Num: value.Num, Den: value.Den}
}

func tags(values []witTypes.Tuple2[string, string]) [][2]string {
	return each(values, func(tag witTypes.Tuple2[string, string]) [2]string { return [2]string{tag.F0, tag.F1} })
}

func feed(value nt.Feed) Feed {
	return Feed{
		Start: FeedStart{
			Tags:     tags(value.Start.Tags),
			FirstPts: value.Start.FirstPts,
			At:       value.Start.At,
			Known:    value.Start.Known,
		},
		Ends: option(value.Ends, same),
	}
}

func color(value wt.ColorInfo) ColorInfo {
	return ColorInfo{value.Range, value.Primaries, value.Trc, value.Space}
}

func witColor(value ColorInfo) wt.ColorInfo {
	return wt.ColorInfo{Range: value.Range, Primaries: value.Primaries, Trc: value.Trc, Space: value.Space}
}

func streamInfo(value wt.StreamInfo) StreamInfo {
	return StreamInfo{
		Index:    value.Index,
		Kind:     value.Kind,
		Codec:    value.Codec,
		Duration: option(value.Duration, same),
		Tags:     tags(value.Tags),
		TimeBase: rational(value.TimeBase),
	}
}

func codedStream(value wt.CodedStream) CodedStream {
	coded := CodedStream{
		Codec:     value.Codec,
		TimeBase:  rational(value.TimeBase),
		Extradata: value.Extradata,
		Profile:   option(value.Profile, same),
		Level:     option(value.Level, same),
	}
	switch value.Format.Tag() {
	case wt.CodedFormatVideo:
		video := value.Format.Video()
		coded.Format.Video = &CodedVideo{
			Width:             video.Width,
			Height:            video.Height,
			SampleAspectRatio: option(video.SampleAspectRatio, rational),
			Color:             option(video.Color, color),
		}
	case wt.CodedFormatAudio:
		audio := value.Format.Audio()
		coded.Format.Audio = &CodedAudio{
			SampleRate:    audio.SampleRate,
			Channels:      audio.Channels,
			ChannelLayout: option(audio.ChannelLayout, same),
		}
	}
	return coded
}

func witCodedStream(value CodedStream) wt.CodedStream {
	format := wt.MakeCodedFormatData()
	switch {
	case value.Format.Video != nil:
		video := value.Format.Video
		format = wt.MakeCodedFormatVideo(wt.CodedVideo{
			Width:             video.Width,
			Height:            video.Height,
			SampleAspectRatio: witOption(video.SampleAspectRatio, witRational),
			Color:             witOption(video.Color, witColor),
		})
	case value.Format.Audio != nil:
		audio := value.Format.Audio
		format = wt.MakeCodedFormatAudio(wt.CodedAudio{
			SampleRate:    audio.SampleRate,
			Channels:      audio.Channels,
			ChannelLayout: witOption(audio.ChannelLayout, same),
		})
	}
	extradata := value.Extradata
	if extradata == nil {
		extradata = []byte{}
	}
	return wt.CodedStream{
		Codec:     value.Codec,
		TimeBase:  witRational(value.TimeBase),
		Format:    format,
		Extradata: extradata,
		Profile:   witOption(value.Profile, same),
		Level:     witOption(value.Level, same),
	}
}

func videoFormat(value wt.VideoFormat) VideoFormat {
	return VideoFormat{Width: value.Width, Height: value.Height, PixFmt: value.PixFmt, Color: option(value.Color, color)}
}

func witVideoFormat(value VideoFormat) wt.VideoFormat {
	return wt.VideoFormat{
		Width:  value.Width,
		Height: value.Height,
		PixFmt: value.PixFmt,
		Color:  witOption(value.Color, witColor),
	}
}

func audioFormat(value wt.AudioFormat) AudioFormat {
	return AudioFormat{
		SampleRate:    value.SampleRate,
		Channels:      value.Channels,
		SampleFmt:     value.SampleFmt,
		ChannelLayout: option(value.ChannelLayout, same),
	}
}

func witAudioFormat(value AudioFormat) wt.AudioFormat {
	return wt.AudioFormat{
		SampleRate:    value.SampleRate,
		Channels:      value.Channels,
		SampleFmt:     value.SampleFmt,
		ChannelLayout: witOption(value.ChannelLayout, same),
	}
}

func boundFormat(value nt.OutputFormat) *Format {
	switch value.Tag() {
	case nt.OutputFormatVideo:
		video := videoFormat(value.Video())
		return &Format{Video: &video}
	case nt.OutputFormatAudio:
		audio := audioFormat(value.Audio())
		return &Format{Audio: &audio}
	case nt.OutputFormatData:
		codec := value.Data()
		return &Format{Data: &codec}
	case nt.OutputFormatPackets:
		coded := codedStream(value.Packets())
		return &Format{Packets: &coded}
	}
	return nil
}

func boundStream(value nt.BoundStream) BoundStream {
	var format *Format
	if value.Format.IsSome() {
		format = boundFormat(value.Format.Some())
	}
	return BoundStream{
		Port:   value.Port,
		ID:     value.Id,
		Info:   streamInfo(value.Info),
		Format: format,
		Rendition: RenditionMeta{
			Name:      option(value.Rendition.Name, same),
			Bandwidth: option(value.Rendition.Bandwidth, same),
			Codecs:    option(value.Rendition.Codecs, same),
			Language:  option(value.Rendition.Language, same),
		},
		Row:         option(value.Row, same),
		DecodeDelay: value.DecodeDelay,
		Latency:     option(value.Latency, same),
		Hint:        StreamHint{Rate: option(value.Hint.Rate, rational)},
	}
}

func witAnchor(value Anchor) nt.Anchor {
	switch value.Kind {
	case AnchorFirstFrame:
		return nt.MakeAnchorFirstFrame()
	case AnchorTagged:
		return nt.MakeAnchorTagged(value.Tag)
	}
	return nt.MakeAnchorSharedClock()
}

func witPairing(value Pairing) nt.Pairing {
	switch value.Kind {
	case PairingHold:
		hold := value.Hold
		return nt.MakePairingHold(nt.Hold{
			Anchor:    witAnchor(hold.Anchor),
			Lead:      hold.Lead,
			Linger:    witOption(hold.Linger, same),
			Timeout:   witOption(hold.Timeout, same),
			Group:     witOption(hold.Group, same),
			PortParam: witOption(hold.PortParam, same),
		})
	case PairingInterval:
		interval := value.Interval
		return nt.MakePairingInterval(nt.Interval{
			Latency: witOption(interval.Latency, same),
			Ahead:   interval.Ahead,
			Anchor:  witAnchor(interval.Anchor),
			Group:   witOption(interval.Group, same),
		})
	case PairingArrival:
		return nt.MakePairingArrival()
	}
	return nt.MakePairingLockstep()
}

func list[T any](values []T) []T {
	if values == nil {
		return []T{}
	}
	return values
}

func witInput(value InputPort) nt.InputPort {
	return nt.InputPort{
		Name:     value.Name,
		Kind:     nt.PortKind(value.Kind),
		Required: value.Required,
		Many:     value.Many,
		Pairing:  witPairing(value.Pairing),
		Rows:     nt.RowsUse(value.Rows),
		Window:   value.Window,
		Stride:   value.Stride,
		Accepts: nt.Accepts{
			PixelFormats:  list(value.Accepts.PixelFormats),
			SampleFormats: list(value.Accepts.SampleFormats),
			SampleRates:   list(value.Accepts.SampleRates),
			ChannelCounts: list(value.Accepts.ChannelCounts),
			Codecs:        list(value.Accepts.Codecs),
			Wants:         wt.Wants(value.Accepts.Wants),
			Like:          witOption(value.Accepts.Like, same),
		},
		Schema: witOption(value.Schema, same),
	}
}

func witOutputFormat(value OutputPort) witTypes.Option[nt.OutputFormat] {
	if like := value.Like; like != nil {
		port := ""
		if like.Port != nil {
			port = *like.Port
		}
		return witTypes.Some(nt.MakeOutputFormatLike(nt.LikeInput{
			Port:         port,
			PixelFormat:  witOption(like.PixelFormat, same),
			SampleFormat: witOption(like.SampleFormat, same),
		}))
	}
	format := value.Format
	switch {
	case format == nil:
		return witTypes.None[nt.OutputFormat]()
	case format.Video != nil:
		return witTypes.Some(nt.MakeOutputFormatVideo(witVideoFormat(*format.Video)))
	case format.Audio != nil:
		return witTypes.Some(nt.MakeOutputFormatAudio(witAudioFormat(*format.Audio)))
	case format.Data != nil:
		return witTypes.Some(nt.MakeOutputFormatData(*format.Data))
	default:
		return witTypes.Some(nt.MakeOutputFormatPackets(witCodedStream(*format.Packets)))
	}
}

func witOutput(value OutputPort) nt.OutputPort {
	return nt.OutputPort{
		Name:     value.Name,
		Kind:     nt.PortKind(value.Kind),
		Format:   witOutputFormat(value),
		TimeBase: witOption(value.TimeBase, witRational),
		Latency:  value.Latency,
		Schema:   witOption(value.Schema, same),
		Row:      witOption(value.Row, same),
	}
}

func witShape(shape *NodeShape) nt.NodeShape {
	var clock nt.Clock
	switch shape.Clock.Kind {
	case ClockInput:
		clock = nt.MakeClockInput(shape.Clock.Port)
	case ClockRate:
		clock = nt.MakeClockRate(witRational(shape.Clock.Rate))
	case ClockRateOf:
		clock = nt.MakeClockRateOf(shape.Clock.Port)
	default:
		clock = nt.MakeClockSelfClocked()
	}
	return nt.NodeShape{
		Inputs:   each(shape.Inputs, witInput),
		Outputs:  each(shape.Outputs, witOutput),
		Clock:    clock,
		Pure:     shape.Pure,
		OneToOne: shape.OneToOne,
		Bounded:  shape.Bounded,
		Relation: list(shape.Relation),
	}
}

func witPayload(value Payload) wn.Payload {
	switch value.Kind {
	case PayloadFrame:
		return wn.MakePayloadFrame(wt.RawFrame{
			Pts:      value.Pts,
			Duration: witOption(value.Duration, same),
			Data:     list(value.Data),
		})
	case PayloadSame:
		return wn.MakePayloadSame(wn.SameFrame{
			Pts:      value.Pts,
			Duration: witOption(value.Duration, same),
			Id:       value.ID,
			Index:    value.Index,
		})
	case PayloadMessage:
		return wn.MakePayloadMessage(nt.Message{Pts: value.Pts, Data: list(value.Data)})
	}
	packet := value.Packet
	return wn.MakePayloadPacket(wt.Packet{
		Pts:      packet.Pts,
		Dts:      witOption(packet.Dts, same),
		Duration: witOption(packet.Duration, same),
		Keyframe: packet.Keyframe,
		Data:     list(packet.Data),
	})
}

func witEmitted(value Emitted) wn.Emitted {
	return wn.Emitted{
		Items: each(value.Items, func(item Emission) wn.Emission {
			return wn.Emission{Port: item.Port, Payload: witPayload(item.Payload)}
		}),
		Rows:     list(value.Reports),
		Finished: value.Finished,
	}
}
