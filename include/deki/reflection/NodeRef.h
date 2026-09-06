#pragma once

#include <cstdint>

// A reference from one node-graph node to another node in the SAME graph, by the
// target's graph-unique id (NodeGraphData::NodeInstance::id). It is the "jump"
// primitive for node graphs: the editor's Node Graph window draws a NodeRef field
// as a dropdown of the graph's nodes (no bezier wire), and the consuming
// interpreter reads `targetId` to continue evaluation at the referenced node.
// 0 = unset (a graph error at runtime for any node that requires a target).
//
// Standard-layout (a single uint32_t) so a node struct holding one stays
// standard-layout and offsetof-reflectable. Serialized as a plain integer: the
// editor writes the id as a JSON number; the generated msgpack deserialize reads
// it straight back into targetId.

namespace Deki
{

struct NodeRef
{
    uint32_t targetId = 0;
};

}  // namespace Deki
