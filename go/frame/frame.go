// Package frame is the crop and Pillow's bilinear resize that the Rust crate
// ffrwd-frame gives a Rust module, with its names: a frame, a rect of it,
// and the planes or the tensor a vision model reads. The resize is
// ffrwd-frame's to the byte: the same fixed point, across first and then
// down, each pass rounding back into eight bits, a pass skipped when its
// axis keeps its size.
package frame

import (
	"encoding/binary"
	"fmt"
	"math"
)

// Rgba is a frame as the wire carries it: 4 bytes a pixel, row-major, no
// padding. The alpha byte is read by nothing here.
type Rgba struct {
	Data          []byte
	Width, Height int
}

// NewRgba is a frame over data, or an error when it is not exactly
// width * height * 4 bytes.
func NewRgba(data []byte, width, height int) (Rgba, error) {
	if want := width * height * 4; len(data) != want {
		return Rgba{}, fmt.Errorf("an rgba frame of %dx%d is %d bytes, not %d", width, height, want, len(data))
	}
	return Rgba{data, width, height}, nil
}

// Rect is a half-open rectangle in frame pixels: exclusive on the right and
// bottom.
type Rect struct {
	X0, Y0, X1, Y1 int
}

// Whole is the whole frame.
func Whole(width, height int) Rect {
	return Rect{0, 0, width, height}
}

// Padded is the crop a detector's box names: x, y, w and h in frame pixels,
// widened by pad of the box's own width and height on every side, each edge
// floored to a whole pixel and clamped to the frame. False when nothing of
// it lands on the frame.
func Padded(x, y, w, h, pad float64, width, height int) (Rect, bool) {
	pw, ph := w*pad, h*pad
	floorClamp := func(value float64, limit int) int {
		return int(min(max(math.Floor(value), 0), float64(limit)))
	}
	r := Rect{
		X0: floorClamp(x-pw, width),
		Y0: floorClamp(y-ph, height),
		X1: floorClamp(x+w+pw, width),
		Y1: floorClamp(y+h+ph, height),
	}
	return r, r.X1 > r.X0 && r.Y1 > r.Y0
}

// Width is how many pixels across.
func (r Rect) Width() int { return max(r.X1-r.X0, 0) }

// Height is how many pixels down.
func (r Rect) Height() int { return max(r.Y1-r.Y0, 0) }

// Filter is one of Pillow's resampling filters.
type Filter int

// Bilinear is Pillow's BILINEAR, antialiased when downscaling.
const Bilinear Filter = iota

// Norm is the per-channel mean and standard deviation applied after scaling
// to 0..1.
type Norm struct {
	Mean, Std [3]float32
}

// ImageNet is what the usual export normalizes with: ImageNet's mean and
// standard deviation, over red, green and blue in 0..1.
var ImageNet = Norm{
	Mean: [3]float32{0.485, 0.456, 0.406},
	Std:  [3]float32{0.229, 0.224, 0.225},
}

// Planes is rect of frame resized to width x height (stretched, no aspect
// padding), then scaled to 0..1 and normalized: planar RGB, [3, height,
// width], the red plane first.
func Planes(frame Rgba, rect Rect, width, height int, filter Filter, norm Norm) []float32 {
	return toPlanes(resizedRGB(frame, rect, width, height, filter), width, height, norm)
}

// Tensor is the same as the little-endian fp32 bytes of a [1, 3, height,
// width] tensor.
func Tensor(frame Rgba, rect Rect, width, height int, filter Filter, norm Norm) []byte {
	return Tensors(frame, []Rect{rect}, width, height, filter, norm)
}

// Tensors is several crops of one frame as one [n, 3, height, width]
// tensor's bytes, in the order the rects were given.
func Tensors(frame Rgba, rects []Rect, width, height int, filter Filter, norm Norm) []byte {
	out := make([]byte, 0, len(rects)*width*height*3*4)
	for _, rect := range rects {
		for _, sample := range Planes(frame, rect, width, height, filter, norm) {
			out = binary.LittleEndian.AppendUint32(out, math.Float32bits(sample))
		}
	}
	return out
}

// resizedRGB is rect of the frame, brought inside it, resized to width x
// height: eight-bit interleaved red, green and blue. Black when the crop or
// the target has no pixels.
func resizedRGB(frame Rgba, rect Rect, width, height int, filter Filter) []byte {
	if filter != Bilinear {
		panic(fmt.Sprintf("frame: no filter %d", filter))
	}
	rect.X1, rect.Y1 = min(rect.X1, frame.Width), min(rect.Y1, frame.Height)
	rect.X0, rect.Y0 = min(rect.X0, rect.X1), min(rect.Y0, rect.Y1)
	cw, ch := rect.Width(), rect.Height()
	if cw == 0 || ch == 0 || width == 0 || height == 0 {
		return make([]byte, width*height*3)
	}
	rgb := make([]byte, 0, cw*ch*3)
	for y := rect.Y0; y < rect.Y1; y++ {
		for x := rect.X0; x < rect.X1; x++ {
			at := (y*frame.Width + x) * 4
			rgb = append(rgb, frame.Data[at:at+3]...)
		}
	}
	if width != cw {
		rgb = horizontal(rgb, cw, ch, width)
	}
	if height != ch {
		rgb = vertical(rgb, width, ch, height)
	}
	return rgb
}

// toPlanes is interleaved eight-bit RGB as planar floats, each channel
// scaled to 0..1 and then normalized.
func toPlanes(rgb []byte, width, height int, norm Norm) []float32 {
	plane := width * height
	out := make([]float32, plane*3)
	for c := range 3 {
		mean, std := norm.Mean[c], norm.Std[c]
		for at := range plane {
			out[c*plane+at] = (float32(rgb[at*3+c])/255 - mean) / std
		}
	}
	return out
}

// kernel is, for each of size pixels resampled from in, the first source
// pixel it reads and the weight of each it reads, in fixed point of
// precision bits.
type kernel struct {
	starts    []int
	weights   [][]int16
	precision uint
}

func bilinear(x float64) float64 {
	x = math.Abs(x)
	if x < 1 {
		return 1 - x
	}
	return 0
}

func newKernel(in, size int) kernel {
	scale := float64(in) / float64(size)
	filterScale := max(scale, 1)
	radius := filterScale
	window := int(math.Ceil(radius))*2 + 1
	recip := 1 / filterScale
	values := make([]float64, 0, window*size)
	type bound struct{ start, size int }
	bounds := make([]bound, 0, size)
	for out := range size {
		inCenter := (float64(out) + 0.5) * scale
		lo := int(math.Max(math.Floor(inCenter-radius), 0))
		hi := int(math.Min(math.Ceil(inCenter+radius), float64(in)))
		at := len(values)
		total := 0.0
		center := inCenter - 0.5
		start, end := lo, hi
		for x := lo; x < hi; x++ {
			w := bilinear((float64(x) - center) * recip)
			if x == start && w == 0 {
				start++
			} else {
				values = append(values, w)
				total += w
			}
		}
		for i := len(values) - 1; i >= 0; i-- {
			if end <= start || values[i] != 0 {
				break
			}
			end--
		}
		if total != 0 {
			for i := at; i < len(values); i++ {
				values[i] /= total
			}
		}
		for len(values) < at+window {
			values = append(values, 0)
		}
		values = values[:at+window]
		bounds = append(bounds, bound{start, end - start})
	}

	heaviest := 0.0
	for n, w := range values {
		if n == 0 || w > heaviest {
			heaviest = w
		}
	}
	var precision uint
	for bits := uint(0); bits < 32-8-2; bits++ {
		precision = bits
		if int32(math.Round(heaviest*float64(int32(1)<<(bits+1)))) >= 1<<15 {
			break
		}
	}
	k := kernel{precision: precision}
	scaleBy := float64(int32(1) << precision)
	for n, b := range bounds {
		weights := make([]int16, b.size)
		for i := range weights {
			weights[i] = int16(math.Round(values[n*window+i] * scaleBy))
		}
		k.starts = append(k.starts, b.start)
		k.weights = append(k.weights, weights)
	}
	return k
}

func (k kernel) clip(sum int32) byte {
	return byte(min(max(sum>>k.precision, 0), 255))
}

// horizontal resamples each row of an rgb picture to width pixels.
func horizontal(rgb []byte, inWidth, height, width int) []byte {
	k := newKernel(inWidth, width)
	initial := int32(1) << (k.precision - 1)
	out := make([]byte, width*height*3)
	for y := range height {
		row := rgb[y*inWidth*3:]
		for x := range width {
			sums := [3]int32{initial, initial, initial}
			for i, w := range k.weights[x] {
				at := (k.starts[x] + i) * 3
				for c := range 3 {
					sums[c] += int32(row[at+c]) * int32(w)
				}
			}
			for c := range 3 {
				out[(y*width+x)*3+c] = k.clip(sums[c])
			}
		}
	}
	return out
}

// vertical resamples each column of an rgb picture to height pixels.
func vertical(rgb []byte, width, inHeight, height int) []byte {
	k := newKernel(inHeight, height)
	initial := int32(1) << (k.precision - 1)
	stride := width * 3
	out := make([]byte, stride*height)
	for y := range height {
		for x := range stride {
			sum := initial
			for i, w := range k.weights[y] {
				sum += int32(rgb[(k.starts[y]+i)*stride+x]) * int32(w)
			}
			out[y*stride+x] = k.clip(sum)
		}
	}
	return out
}
