//go:build wasip1

// Package export_ffrwd_av_node is where the generated wit_exports calls the
// node export. componentize-go leaves this package to the library; it hands
// every call to Node, which the node package's glue sets.
package export_ffrwd_av_node

import (
	"github.com/imbcmdth/ffrwd-node/go/internal/wit/ffrwd_av_node"
	"github.com/imbcmdth/ffrwd-node/go/internal/wit/ffrwd_av_node_tick"
	"github.com/imbcmdth/ffrwd-node/go/internal/wit/ffrwd_av_node_types"
	"github.com/imbcmdth/ffrwd-node/go/internal/wit/ffrwd_av_types"
	witTypes "go.bytecodealliance.org/pkg/wit/types"
)

type Exports interface {
	Describe() ffrwd_av_types.Meta
	Shape(params string, bound []ffrwd_av_node_types.Binding) witTypes.Result[ffrwd_av_node_types.NodeShape, string]
	Init(bound []ffrwd_av_node_types.BoundStream, latched []string, params string) witTypes.Result[witTypes.Unit, string]
	SetParams(params string) witTypes.Result[witTypes.Unit, string]
	Process(tick *ffrwd_av_node_tick.Tick) witTypes.Result[ffrwd_av_node.Emitted, string]
}

var Node Exports

func Describe() ffrwd_av_types.Meta {
	return Node.Describe()
}

func Shape(params string, bound []ffrwd_av_node_types.Binding) witTypes.Result[ffrwd_av_node_types.NodeShape, string] {
	return Node.Shape(params, bound)
}

func Init(bound []ffrwd_av_node_types.BoundStream, latched []string, params string) witTypes.Result[witTypes.Unit, string] {
	return Node.Init(bound, latched, params)
}

func SetParams(params string) witTypes.Result[witTypes.Unit, string] {
	return Node.SetParams(params)
}

func Process(tick *ffrwd_av_node_tick.Tick) witTypes.Result[ffrwd_av_node.Emitted, string] {
	return Node.Process(tick)
}
