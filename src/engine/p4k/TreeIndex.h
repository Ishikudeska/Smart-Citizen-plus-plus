#pragma once

#include <atomic>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace engine::p4k {

// A directory tree over a flat list of '/'-separated paths (an archive's
// 1.37M entry names, DataForge record paths), for browsing. Folders merge
// case-insensitively and list before files; siblings sort by name ignoring
// case. Each folder's children sit in one contiguous range, so a tree view
// can ask for "child n of folder x" in constant time.
//
// Names are views into the caller's path strings, which must outlive the
// index.
class TreeIndex
{
public:
    using NodeId = std::uint32_t;
    static constexpr NodeId kNone = 0xFFFFFFFFu;

    struct Item
    {
        std::string_view path;
        std::uint64_t size = 0;
        std::uint64_t packed = 0; // compressed size
    };

    struct Node
    {
        std::string_view name;
        NodeId parent = kNone;
        std::uint32_t row = 0;        // position among the parent's children
        std::uint32_t firstChild = 0; // into the child table
        std::uint32_t childCount = 0;
        std::uint32_t item = kNone; // the Item a file came from; kNone for folders
        std::uint32_t files = 0;    // folders: files beneath
        std::uint64_t size = 0;     // folders: totals beneath
        std::uint64_t packed = 0;
    };

    // Builds the tree. Returns nothing if `cancel` was raised. A path that
    // names both a file and a folder ("a/b" and "a/b/c") keeps both nodes.
    static std::optional<TreeIndex> build(std::span<const Item> items,
                                          const std::atomic<bool> *cancel = nullptr);

    NodeId root() const { return 0; }
    std::size_t nodeCount() const { return nodes_.size(); }
    const Node &node(NodeId id) const { return nodes_[id]; }
    bool isFolder(NodeId id) const { return nodes_[id].item == kNone; }
    NodeId child(NodeId folder, std::uint32_t row) const
    {
        return children_[nodes_[folder].firstChild + row];
    }

    // "Data/Libs/x.xml" (no leading slash; the root is "").
    std::string path(NodeId id) const;
    // Case-insensitive; '\' or '/'. The root for "".
    std::optional<NodeId> find(std::string_view path) const;
    // The items of every file at or beneath `id`, in tree order.
    void collectItems(NodeId id, std::vector<std::uint32_t> &out) const;
    // The node built from item `index`.
    NodeId nodeOfItem(std::uint32_t index) const { return itemNodes_[index]; }

private:
    std::vector<Node> nodes_;
    std::vector<NodeId> children_;
    std::vector<NodeId> itemNodes_;
};

} // namespace engine::p4k
