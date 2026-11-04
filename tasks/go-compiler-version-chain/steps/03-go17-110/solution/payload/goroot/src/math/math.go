// Package math provides basic constants and mathematical functions.
package math

func Sqrt(x float64) float64
func Floor(x float64) float64
func Ceil(x float64) float64
func Trunc(x float64) float64
func Log(x float64) float64
func Log2(x float64) float64
func Log10(x float64) float64
func Exp(x float64) float64
func Sin(x float64) float64
func Cos(x float64) float64
func Tan(x float64) float64
func Asin(x float64) float64
func Acos(x float64) float64
func Atan(x float64) float64
func Cbrt(x float64) float64
func Round(x float64) float64
func Abs(x float64) float64
func Pow(x, y float64) float64
func Atan2(y, x float64) float64
func Mod(x, y float64) float64
func Hypot(p, q float64) float64
func IsNaN(f float64) bool
func isInf(f float64, sign int) bool
func inf(sign int) float64
func NaN() float64
func Ldexp(frac float64, exp int) float64
func Frexp(f float64) (frac float64, exp int)
func Float64bits(f float64) uint64
func Float64frombits(b uint64) float64
func Float32bits(f float32) uint32
func Float32frombits(b uint32) float32

func IsInf(f float64, sign int) bool { return isInf(f, sign) }
func Inf(sign int) float64           { return inf(sign) }

const (
	MaxInt    = 1<<63 - 1
	MinInt    = -1 << 63
	MaxInt8   = 1<<7 - 1
	MinInt8   = -1 << 7
	MaxInt16  = 1<<15 - 1
	MinInt16  = -1 << 15
	MaxInt32  = 1<<31 - 1
	MinInt32  = -1 << 31
	MaxInt64  = 1<<63 - 1
	MinInt64  = -1 << 63
	MaxUint8  = 1<<8 - 1
	MaxUint16 = 1<<16 - 1
	MaxUint32 = 1<<32 - 1
	MaxUint64 = 1<<64 - 1
	MaxUint   = 1<<64 - 1
)

const (
	MaxFloat32             = 0x1p127 * (1 + (1 - 0x1p-23))
	SmallestNonzeroFloat32 = 0x1p-126 * 0x1p-23
	MaxFloat64             = 0x1p1023 * (1 + (1 - 0x1p-52))
	SmallestNonzeroFloat64 = 0x1p-1022 * 0x1p-52
)

const (
	E   = 2.71828182845904523536028747135266249775724709369995957496696763
	Pi  = 3.14159265358979323846264338327950288419716939937510582097494459
	Phi = 1.61803398874989484820458683436563811772030917980576286213544862

	Sqrt2   = 1.41421356237309504880168872420969807856967187537694806841
	SqrtE   = 1.64872127070012814684865078781416357165377610071014801157
	SqrtPi  = 1.77245385090551602729816748334114518279754945612238712821
	SqrtPhi = 1.27201964951406896425242246173749149171560804184009624861

	Ln2    = 0.693147180559945309417232121458176568075500134360255254120680
	Log2E  = 1 / Ln2
	Ln10   = 2.30258509299404568401799145468436420760110148862877297603333
	Log10E = 1 / Ln10
)

func Max(x, y float64) float64 {
	if IsNaN(x) || IsNaN(y) {
		return NaN()
	}
	if x > y {
		return x
	}
	return y
}

func Min(x, y float64) float64 {
	if IsNaN(x) || IsNaN(y) {
		return NaN()
	}
	if x < y {
		return x
	}
	return y
}

func Signbit(x float64) bool { return Float64bits(x)&(1<<63) != 0 }

func Copysign(f, sign float64) float64 {
	const signBit = 1 << 63
	return Float64frombits(Float64bits(f)&^signBit | Float64bits(sign)&signBit)
}
