package audio

import (
	"bytes"
	"encoding/binary"
	"testing"
)

func TestAudioType_String(t *testing.T) {
	tests := []struct {
		typ      AudioType
		expected string
	}{
		{AudioTypeMixed, "mixed"},
		{AudioTypeOneWay, "one_way"},
		{AudioTypeShare, "share"},
		{AudioType(999), "unknown"},
	}

	for _, test := range tests {
		if got := test.typ.String(); got != test.expected {
			t.Errorf("AudioType(%d).String() = %q, want %q", test.typ, got, test.expected)
		}
	}
}

func TestAudioFrame_Encode(t *testing.T) {
	tests := []struct {
		frame    AudioFrame
		wantSize int
	}{
		{
			frame: AudioFrame{
				Type:   AudioTypeMixed,
				UserID: 12345,
				Data:   []byte{1, 2, 3, 4, 5},
			},
			wantSize: BinaryFrameHeaderSize + 5,
		},
		{
			frame: AudioFrame{
				Type:   AudioTypeOneWay,
				UserID: 0,
				Data:   []byte{},
			},
			wantSize: BinaryFrameHeaderSize,
		},
	}

	for i, test := range tests {
		got := test.frame.Encode()

		if len(got) != test.wantSize {
			t.Errorf("Test %d: len(encoded) = %d, want %d", i, len(got), test.wantSize)
		}

		// Verify Type field
		gotType := AudioType(binary.LittleEndian.Uint64(got[0:8]))
		if gotType != test.frame.Type {
			t.Errorf("Test %d: decoded Type = %v, want %v", i, gotType, test.frame.Type)
		}

		// Verify UserID field
		gotUserID := binary.LittleEndian.Uint64(got[8:16])
		if gotUserID != test.frame.UserID {
			t.Errorf("Test %d: decoded UserID = %d, want %d", i, gotUserID, test.frame.UserID)
		}

		// Verify Data field
		if !bytes.Equal(got[BinaryFrameHeaderSize:], test.frame.Data) {
			t.Errorf("Test %d: decoded Data = %v, want %v", i, got[BinaryFrameHeaderSize:], test.frame.Data)
		}
	}
}

func TestBinaryFrameHeaderSize(t *testing.T) {
	// The header should be the size of Type (8 bytes) + UserID (8 bytes)
	expectedSize := 8 + 8 // Type and UserID are both uint64
	if BinaryFrameHeaderSize != expectedSize {
		t.Errorf("BinaryFrameHeaderSize = %d, want %d", BinaryFrameHeaderSize, expectedSize)
	}
}
