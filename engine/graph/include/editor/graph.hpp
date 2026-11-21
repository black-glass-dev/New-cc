#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace editor {
// Experimental structural contract only. No pixel/audio evaluation, JNI or persistence yet.
enum class PortKind { image, mask, audio, scalar, geometry };
struct GraphNode {
    std::uint64_t id;
    std::string type;
    std::vector<PortKind> inputs; // all required in this initial contract
    std::vector<PortKind> outputs;
};
struct GraphEdge {
    std::uint64_t source;
    std::size_t output;
    std::uint64_t destination;
    std::size_t input;
};
struct ProcessingGraph {
    std::vector<GraphNode> nodes;
    std::vector<GraphEdge> edges;
    std::uint64_t output_node;
};
// Validates a single connected output DAG and returns a deterministic dependency order.
// Throws invalid_argument for malformed/unsupported structure; never mutates its input.
// A backend registry must separately validate node types, parameters and device capability.
std::vector<std::uint64_t> validate_graph(const ProcessingGraph& graph);
} // namespace editor
