#include "editor/graph.hpp"

#include <map>
#include <set>
#include <stdexcept>

namespace editor {
std::vector<std::uint64_t> validate_graph(const ProcessingGraph& graph) {
    const auto require = [](bool valid, const char* message) {
        if (!valid) throw std::invalid_argument(message);
    };
    require(!graph.nodes.empty() && graph.nodes.size() <= 4096, "graph node budget exceeded or empty");
    require(graph.edges.size() <= 32768, "graph edge budget exceeded");
    std::map<std::uint64_t, const GraphNode*> nodes;
    std::map<std::uint64_t, std::size_t> indegree;
    std::map<std::uint64_t, std::vector<std::uint64_t>> forward, reverse;
    std::set<std::pair<std::uint64_t, std::size_t>> connected_inputs;
    const auto valid_port = [](PortKind p) { return p >= PortKind::image && p <= PortKind::geometry; };
    for (const auto& node : graph.nodes) {
        require(node.id != 0 && !node.type.empty() && node.type.size() <= 128, "invalid node identity");
        require(node.inputs.size() <= 32 && node.outputs.size() <= 32, "node port budget exceeded");
        for (auto p : node.inputs) require(valid_port(p), "unknown input type");
        for (auto p : node.outputs) require(valid_port(p), "unknown output type");
        require(nodes.emplace(node.id, &node).second, "duplicate node ID");
        indegree[node.id] = 0;
    }
    require(nodes.contains(graph.output_node), "missing graph output");
    require(nodes.at(graph.output_node)->outputs.empty(), "output node must be a sink");
    for (const auto& edge : graph.edges) {
        require(nodes.contains(edge.source) && nodes.contains(edge.destination), "dangling edge");
        const auto& source = *nodes.at(edge.source);
        const auto& target = *nodes.at(edge.destination);
        require(edge.output < source.outputs.size() && edge.input < target.inputs.size(), "invalid port index");
        require(source.outputs[edge.output] == target.inputs[edge.input], "port type mismatch");
        require(connected_inputs.emplace(edge.destination, edge.input).second, "multiple writers for one input");
        ++indegree[edge.destination];
        forward[edge.source].push_back(edge.destination);
        reverse[edge.destination].push_back(edge.source);
    }
    std::set<std::uint64_t> ready;
    for (const auto& [id, count] : indegree) {
        require(count == nodes.at(id)->inputs.size(), "unconnected required input");
        if (count == 0) ready.insert(id);
    }
    std::vector<std::uint64_t> order;
    while (!ready.empty()) {
        const auto id = *ready.begin(); ready.erase(ready.begin()); order.push_back(id);
        for (const auto target : forward[id]) if (--indegree[target] == 0) ready.insert(target);
    }
    require(order.size() == nodes.size(), "processing graph cycle");
    // Every node must contribute to this output: iterative walk avoids stack exhaustion.
    std::set<std::uint64_t> live;
    std::vector<std::uint64_t> pending{graph.output_node};
    while (!pending.empty()) {
        const auto id = pending.back(); pending.pop_back();
        if (!live.insert(id).second) continue;
        for (const auto source : reverse[id]) pending.push_back(source);
    }
    require(live.size() == nodes.size(), "disconnected processing node");
    return order;
}
} // namespace editor
