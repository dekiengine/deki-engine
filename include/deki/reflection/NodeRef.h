#pragma once

#include <cstdint>

// A reference from one node-graph node to another node in the same graph, by
// the target's graph-unique id (NodeGraphData::NodeInstance::id). It is the
// "jump" for node graphs: the Node Graph window draws a NodeRef field as a
// dropdown of the graph's nodes instead of a wire, and the interpreter reads
// `targetId` to continue at that node. 0 means unset, which is a runtime graph
// error for a node that requires a target.
//
// A single uint32_t, so a node struct holding one stays standard-layout and
// offsetof-reflectable. Serialized as a plain integer: a JSON number in the
// editor, read straight back into targetId by the generated msgpack code.

namespace Deki
{

struct NodeRef
{
    uint32_t targetId = 0;
};

}  // namespace Deki
