#pragma once

#include <algorithm>
#include <memory>
#include <vector>
#include <map>

namespace DependencyGraph {

using NodeID = uint32_t;

class Edge;

class Node {
public:
    Node() = default;
    virtual ~Node() = default;

    NodeID ID() const { return id_; }

    template <typename T>
    std::vector<T*> InEdges() {
        std::vector<T*> result;
        for (auto& edge : in_edges_) {
            if (auto casted = dynamic_cast<T*>(edge)) result.push_back(casted);
        }
        return result;
    }

    template <typename T>
    std::vector<T*> OutEdges() {
        std::vector<T*> result;
        for (auto& edge : out_edges_) {
            if (auto casted = dynamic_cast<T*>(edge)) result.push_back(casted);
        }
        return result;
    }

protected:
    NodeID id_ = 0;
    std::vector<Edge*> in_edges_;
    std::vector<Edge*> out_edges_;

    friend class DependencyGraph;
};

class Edge {
public:
    Edge() = default;
    virtual ~Edge() = default;

    template <typename T>
    T* From() { return dynamic_cast<T*>(from_); }

    template <typename T>
    T* To() { return dynamic_cast<T*>(to_); }

protected:
    Node* from_ = nullptr;
    Node* to_ = nullptr;

    friend class DependencyGraph;
};

class DependencyGraph {
public:
    template <typename T, typename... Args>
    T* CreateNode(Args&&... args) {
        auto node = std::make_unique<T>(std::forward<Args>(args)...);
        node->id_ = next_id_++;
        auto raw = node.get();
        nodes_.emplace_back(std::move(node));
        node_map_[raw->ID()] = raw;
        return raw;
    }

    template <typename T, typename... Args>
    T* CreateEdge(Args&&... args) {
        auto edge = std::make_unique<T>(std::forward<Args>(args)...);
        auto raw = edge.get();
        edges_.emplace_back(std::move(edge));
        return raw;
    }

    void Link(Node* from, Node* to, Edge* edge) {
        edge->from_ = from;
        edge->to_ = to;
        from->out_edges_.push_back(edge);
        to->in_edges_.push_back(edge);
    }

    Node* GetNode(NodeID id) {
        auto it = node_map_.find(id);
        if (it != node_map_.end()) return it->second;
        return nullptr;
    }

private:
    NodeID next_id_ = 0;
    // Arena-like storage: vector of unique_ptrs ensures pointer stability (heap allocation) 
    // and automatic cleanup when Graph is destroyed.
    std::vector<std::unique_ptr<Node>> nodes_;
    std::vector<std::unique_ptr<Edge>> edges_;
    
    std::map<NodeID, Node*> node_map_;
};

} // namespace DependencyGraph