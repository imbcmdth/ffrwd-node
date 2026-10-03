package node

import (
	"bytes"
	"os"
	"testing"
)

// The module carries its own wit/av.wit, since a Go module holds no file
// outside its directory; in the repo it has to be the root's byte for byte.
func TestTheWitIsTheRepos(t *testing.T) {
	root, err := os.ReadFile("../wit/av.wit")
	if err != nil {
		t.Skip("not in the repo")
	}
	ours, err := os.ReadFile("wit/av.wit")
	if err != nil {
		t.Fatal(err)
	}
	normal := func(text []byte) []byte { return bytes.ReplaceAll(text, []byte("\r\n"), []byte("\n")) }
	if !bytes.Equal(normal(root), normal(ours)) {
		t.Fatal("go/wit/av.wit differs from wit/av.wit: run go generate")
	}
}
