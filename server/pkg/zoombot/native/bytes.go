package native

import (
	"unsafe"
)

// ToSlice returns a Go slice of T * length backed by C memory at ptr.
func ToSlice[T any](ptr unsafe.Pointer, size int) []T {
	if ptr == nil || size <= 0 {
		return nil
	}
	return unsafe.Slice((*T)(ptr), size)
}

// ToSliceFromBytes returns a Go slice of T by interpreting bytes size at ptr.
func ToSliceFromBytes[T any](ptr unsafe.Pointer, bytes uintptr) []T {
	size := unsafe.Sizeof(*new(T))
	if size == 0 {
		return nil
	}
	count := int(bytes / size)
	return ToSlice[T](ptr, count)
}
