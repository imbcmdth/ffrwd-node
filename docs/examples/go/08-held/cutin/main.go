package main

import (
	"errors"

	node "github.com/imbcmdth/ffrwd-node/go"
)

// timed is the tag a feeder puts on its stream to say its pts are programme
// time.
const timed = "smart_timed"

type Params struct {
	Lead    float64 `json:"lead"`
	Linger  float64 `json:"linger"`
	Timeout float64 `json:"timeout"`
}

// Presence is one change in what the host says of the feed, or a note the
// feeder wrote beside its picture.
type Presence struct {
	Event string  `json:"event"`
	T     float64 `json:"t"`
	At    float64 `json:"at"`
	Text  *string `json:"text"`
}

// Note is what a feeder writes beside its picture.
type Note struct {
	Text string `json:"text"`
}

type Cutin struct {
	v     uint32
	width int
	feed  *uint32
	notes *uint32
	// One frame of the programme, in its time base.
	step int64
}

var Definition = node.Definition[Params]{
	Name:         "cutin",
	Version:      "0.1.0",
	ParamsSchema: `{"type":"object","properties":{"port":{"type":"integer","minimum":1,"maximum":65535,"default":9000},"lead":{"type":"number","minimum":0,"maximum":60,"default":0.5},"linger":{"type":"number","minimum":0,"maximum":60,"default":0},"timeout":{"type":"number","minimum":0,"maximum":60,"default":1}},"additionalProperties":false}`,
	Shape: func(params Params, _ *node.Bound) (node.Shape, error) {
		feed := node.VideoInput("feed").
			Optional().
			Hold().
			Anchor(node.Tagged(timed)).
			Lead(params.Lead).
			Group("cam").
			PortParam("port").
			Like("v").
			PixelFormats("rgba")
		if params.Linger > 0 {
			feed = feed.Linger(params.Linger)
		}
		if params.Timeout > 0 {
			feed = feed.Timeout(params.Timeout)
		}
		return node.NewShape().
			Input(node.VideoInput("v").Clock().PixelFormats("rgba")).
			Input(feed).
			Input(node.RowsInput("notes").
				Optional().
				Interval().
				Group("cam").
				Schema(node.SchemaOf[Note]())).
			Output(node.LikeOutput("v")).
			Output(node.RowsOutput("presence").Schema(node.SchemaOf[Presence]())).
			Pure().
			OneToOne(), nil
	},
	Init: func(_ Params, init *node.Init) (node.Instance, error) {
		v, err := init.Stream("v")
		if err != nil {
			return nil, err
		}
		video := v.VideoFormat()
		if video == nil {
			return nil, errors.New("`v` is a video input")
		}
		step := int64(1)
		if rate := v.Hint.Rate; rate != nil {
			step = max(v.Info.TimeBase.Pts(rate.Duration(1)), 1)
		}
		cutin := &Cutin{v: v.ID, width: int(video.Width), step: step}
		if feed := init.Optional("feed"); feed != nil {
			cutin.feed = &feed.ID
		}
		if notes := init.Optional("notes"); notes != nil {
			cutin.notes = &notes.ID
		}
		return cutin, nil
	},
}

func (c *Cutin) Process(tick *node.Tick, out *node.Out) error {
	frame, ok := tick.Frame(c.v)
	if !ok {
		return nil
	}
	clock := tick.TimeBase()
	pts, step := frame.Pts, c.step
	if frame.Duration != nil {
		step = *frame.Duration
	}
	step = max(step, 1)
	say := func(event string, at int64) error {
		row := Presence{
			Event: event,
			T:     clock.Seconds(pts),
			At:    clock.Seconds(at),
		}
		return out.Row("presence", pts, row)
	}
	var countdown *float64
	if c.feed != nil {
		if current := tick.Feed(*c.feed); current != nil {
			start := current.Start
			if start.Known == pts && start.Known < start.At {
				if err := say("coming", start.At); err != nil {
					return err
				}
			}
			if pts <= start.At && start.At < pts+step {
				if err := say("on", start.At); err != nil {
					return err
				}
			}
			if pts < start.At {
				left := float64(start.At-pts) / float64(start.At-start.Known)
				countdown = &left
			}
		}
		for _, ended := range tick.EndedFeeds(*c.feed) {
			if ends := ended.Ends; ends != nil && *ends < pts && pts-step <= *ends {
				if err := say("off", *ends+step); err != nil {
					return err
				}
			}
		}
	}
	if c.notes != nil {
		base := tick.Info(*c.notes).TimeBase
		for _, message := range tick.Messages(*c.notes) {
			at := max(base.Rescale(message.Pts, clock), pts)
			var note Note
			if err := message.Decode(&note); err != nil {
				return err
			}
			row := Presence{
				Event: "note",
				T:     clock.Seconds(pts),
				At:    clock.Seconds(at),
				Text:  &note.Text,
			}
			if err := out.Row("presence", at, row); err != nil {
				return err
			}
		}
	}
	if c.feed != nil {
		if shown, ok := tick.Frame(*c.feed); ok {
			return out.Same("v", pts, frame.Duration, *c.feed, shown.Index)
		}
	}
	if countdown == nil {
		return out.Pass("v", c.v, frame)
	}
	pixels := tick.Fetch(c.v, frame.Index)
	bar := int(float64(c.width) * *countdown)
	rows := len(pixels) / (c.width * 4)
	for n := 0; n < 8 && n < rows; n++ {
		row := pixels[(rows-1-n)*c.width*4:]
		for at := 0; at < bar*4; at += 4 {
			copy(row[at:at+4], []byte{220, 40, 40, 255})
		}
	}
	return out.Frame("v", pts, frame.Duration, pixels)
}

func init() { node.Export(Definition) }

func main() {}
