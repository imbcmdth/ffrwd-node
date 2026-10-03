package main

import (
	"fmt"
	"math"
)

// What the Rust example takes from ffrwd-frame, written out by hand: the
// crop and Pillow's bilinear resize, as ffrwd-frame asks fast_image_resize
// for it. The resize antialiases when it shrinks, goes across first and then
// down, and rounds back into eight bits after each pass, so its bytes are
// the Rust example's.

// Rect is a half-open rectangle of a picture, in pixels: exclusive on the
// right and bottom.
type Rect struct {
	X0, Y0, X1, Y1 int
}

// Whole is the whole picture.
func Whole(width, height int) Rect {
	return Rect{0, 0, width, height}
}

func (r Rect) Width() int  { return max(r.X1-r.X0, 0) }
func (r Rect) Height() int { return max(r.Y1-r.Y0, 0) }

// resize is rect of an rgba picture resized to width x height: interleaved
// red, green and blue, alpha dropped.
func resize(pixels []byte, pictureWidth, pictureHeight int, rect Rect, width, height int) ([]byte, error) {
	if want := pictureWidth * pictureHeight * 4; len(pixels) != want {
		return nil, fmt.Errorf("an rgba frame of %dx%d is %d bytes, not %d", pictureWidth, pictureHeight, want, len(pixels))
	}
	rect.X1, rect.Y1 = min(rect.X1, pictureWidth), min(rect.Y1, pictureHeight)
	rect.X0, rect.Y0 = min(rect.X0, rect.X1), min(rect.Y0, rect.Y1)
	cw, ch := rect.Width(), rect.Height()
	if cw == 0 || ch == 0 || width == 0 || height == 0 {
		return make([]byte, width*height*3), nil
	}
	rgb := make([]byte, 0, cw*ch*3)
	for y := rect.Y0; y < rect.Y1; y++ {
		for x := rect.X0; x < rect.X1; x++ {
			at := (y*pictureWidth + x) * 4
			rgb = append(rgb, pixels[at:at+3]...)
		}
	}
	if width != cw {
		rgb = horizontal(rgb, cw, ch, width)
	}
	if height != ch {
		rgb = vertical(rgb, width, ch, height)
	}
	return rgb, nil
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
