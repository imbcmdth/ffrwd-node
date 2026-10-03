package frame

import (
	"bytes"
	"math"
	"testing"
)

var eightBits = Norm{Std: [3]float32{1.0 / 255, 1.0 / 255, 1.0 / 255}}

// edge is ffrwd-frame's own test picture: three sawtooth ramps with a flat
// block over the lower right, alpha varying.
func edge(width, height int) []byte {
	pixels := make([]byte, 0, width*height*4)
	for y := range height {
		for x := range width {
			r, g, b := byte((x*7+y*11)%256), byte((x*5+y*3)%256), byte((x*2+y*13)%256)
			if 3*x >= width && 3*y >= height {
				r, g, b = 250, 8, 130
			}
			pixels = append(pixels, r, g, b, byte(255-(x+y)%256))
		}
	}
	return pixels
}

// noise is its other: splitmix64 from 0x5EED, one draw a pixel, its low four
// bytes.
func noise(width, height int) []byte {
	pixels := make([]byte, 0, width*height*4)
	state := uint64(0x5EED)
	for range width * height {
		state += 0x9E3779B97F4A7C15
		z := state
		z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9
		z = (z ^ (z >> 27)) * 0x94D049BB133111EB
		z ^= z >> 31
		pixels = append(pixels, byte(z), byte(z>>8), byte(z>>16), byte(z>>24))
	}
	return pixels
}

func digest(data []byte) uint64 {
	value := uint64(0xCBF29CE484222325)
	for _, b := range data {
		value = (value ^ uint64(b)) * 0x100000001B3
	}
	return value
}

// eight is planes of plain eight-bit values back to interleaved bytes.
func eight(planes []float32, width, height int) []byte {
	plane := width * height
	out := make([]byte, 0, plane*3)
	for at := range plane {
		for c := range 3 {
			out = append(out, byte(min(max(math.Round(float64(planes[c*plane+at])), 0), 255)))
		}
	}
	return out
}

func mustRgba(t *testing.T, data []byte, width, height int) Rgba {
	t.Helper()
	frame, err := NewRgba(data, width, height)
	if err != nil {
		t.Fatal(err)
	}
	return frame
}

func TestAFrameIsFourBytesAPixelAndNothingElse(t *testing.T) {
	frame := mustRgba(t, make([]byte, 8*5*4), 8, 5)
	if frame.Width != 8 || frame.Height != 5 {
		t.Fatalf("a frame of %dx%d", frame.Width, frame.Height)
	}
	if _, err := NewRgba(make([]byte, 8*5*4-1), 8, 5); err == nil {
		t.Fatal("a byte short is a frame")
	}
	_, err := NewRgba(make([]byte, 8*5*4+4), 8, 5)
	if err == nil || err.Error() != "an rgba frame of 8x5 is 160 bytes, not 164" {
		t.Fatalf("a pixel over: %v", err)
	}
	if _, err := NewRgba(make([]byte, 8*5*3), 8, 5); err == nil {
		t.Fatal("three bytes a pixel is a frame")
	}
}

func TestABoxGrowsATenthOfItselfFlooredAndClampedToTheFrame(t *testing.T) {
	cases := []struct {
		x, y, w, h    float64
		width, height int
		want          Rect
	}{
		{300, 100, 100, 200, 1280, 720, Rect{290, 80, 410, 320}},
		{14, 14, 33, 33, 640, 480, Rect{10, 10, 50, 50}},
		{5, 4, 100, 200, 1280, 720, Rect{0, 0, 115, 224}},
		{1200, 600, 100, 200, 1280, 720, Rect{1190, 580, 1280, 720}},
		{-50, -50, 400, 400, 320, 240, Whole(320, 240)},
	}
	for _, c := range cases {
		got, ok := Padded(c.x, c.y, c.w, c.h, 0.1, c.width, c.height)
		if !ok || got != c.want {
			t.Errorf("%v: %v, %v", c, got, ok)
		}
	}
	if r, _ := Padded(300, 100, 100, 200, 0.1, 1280, 720); r.Width() != 120 || r.Height() != 240 {
		t.Errorf("%dx%d", r.Width(), r.Height())
	}
}

func TestABoxWithNothingOnTheFrameIsNoRectAtAll(t *testing.T) {
	for _, box := range [][4]float64{{2000, 100, 50, 50}, {-500, 100, 50, 50}, {100, 2000, 50, 50}, {100, 100, 0, 50}, {100, 100, 50, 0}} {
		if r, ok := Padded(box[0], box[1], box[2], box[3], 0.1, 1280, 720); ok {
			t.Errorf("%v: %v", box, r)
		}
	}
}

func TestTheResizeIsFfrwdFramesToTheByte(t *testing.T) {
	// What ffrwd-frame 0.1.1's planes answers for each, as eight-bit bytes.
	pictures := map[string]func(int, int) []byte{"edge": edge, "noise": noise}
	cases := []struct {
		kind          string
		sw, sh        int
		rect          *Rect
		width, height int
		want          uint64
	}{
		{"edge", 9, 7, nil, 32, 24, 0x2b81603c6e9b7af2},
		{"noise", 9, 7, nil, 32, 24, 0x89c99096ab2298ac},
		{"edge", 500, 380, nil, 224, 224, 0x3f0ef4e1e3120a7d},
		{"noise", 500, 380, nil, 224, 224, 0xb6ef2710b0b018d8},
		{"edge", 9, 7, nil, 9, 7, 0x67bff18552e376de},
		{"noise", 320, 240, &Rect{80, 60, 187, 140}, 320, 240, 0x817c929422ae445b},
		{"noise", 320, 240, nil, 160, 240, 0xe1ec2af94805ba44},
		{"edge", 320, 240, nil, 320, 80, 0xee245d22a607658f},
		{"edge", 64, 48, nil, 200, 20, 0x10d6a057a703d7db},
		{"noise", 40, 30, &Rect{30, 20, 99, 99}, 16, 16, 0xc4e1a45b3de0dbc7},
	}
	for _, c := range cases {
		frame := mustRgba(t, pictures[c.kind](c.sw, c.sh), c.sw, c.sh)
		rect := Whole(c.sw, c.sh)
		if c.rect != nil {
			rect = *c.rect
		}
		got := digest(eight(Planes(frame, rect, c.width, c.height, Bilinear, eightBits), c.width, c.height))
		if got != c.want {
			t.Errorf("%s %dx%d into %dx%d: %#x, want %#x", c.kind, c.sw, c.sh, c.width, c.height, got, c.want)
		}
	}
}

func TestTheTensorIsFfrwdFramesToTheBit(t *testing.T) {
	frame := mustRgba(t, noise(97, 61), 97, 61)
	padded, _ := Padded(20, 10, 30, 25, 0.1, 97, 61)
	one := Tensor(frame, padded, 24, 24, Bilinear, ImageNet)
	if len(one) != 3*24*24*4 || digest(one) != 0x5411c2aba83d4522 {
		t.Errorf("one: %d bytes, %#x", len(one), digest(one))
	}
	two := Tensors(frame, []Rect{Whole(97, 61), padded}, 16, 12, Bilinear, ImageNet)
	if digest(two) != 0xf5a3beedbb307eed {
		t.Errorf("two: %#x", digest(two))
	}
	if none := Tensors(frame, nil, 16, 12, Bilinear, ImageNet); len(none) != 0 {
		t.Errorf("no rects: %d bytes", len(none))
	}
}

func TestACropWithNoPixelsIsBlack(t *testing.T) {
	frame := mustRgba(t, edge(9, 7), 9, 7)
	for at, v := range Planes(frame, Rect{5, 5, 5, 7}, 4, 4, Bilinear, eightBits) {
		if v != 0 {
			t.Fatalf("sample %d is %v", at, v)
		}
	}
}

func TestTheAlphaByteReachesNothing(t *testing.T) {
	opaque := edge(24, 24)
	varying := bytes.Clone(opaque)
	for at := 3; at < len(varying); at += 4 {
		varying[at] = byte(at)
	}
	rect := Whole(24, 24)
	a := Tensor(mustRgba(t, opaque, 24, 24), rect, 10, 10, Bilinear, ImageNet)
	b := Tensor(mustRgba(t, varying, 24, 24), rect, 10, 10, Bilinear, ImageNet)
	if !bytes.Equal(a, b) {
		t.Fatal("alpha moved the tensor")
	}
}
