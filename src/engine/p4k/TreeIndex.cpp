#include "engine/p4k/TreeIndex.h"

#include <algorithm>
#include <unordered_map>

namespace engine::p4k {

namespace {

char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

// -1/0/1 comparing ASCII case-insensitively.
int compareCaseless(std::string_view a, std::string_view b)
{
    const std::size_t n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i) {
        const char x = lower(a[i]), y = lower(b[i]);
        if (x != y)
            return static_cast<unsigned char>(x) < static_cast<unsigned char>(y) ? -1 : 1;
    }
    return a.size() == b.size() ? 0 : (a.size() < b.size() ? -1 : 1);
}

bool equalCaseless(std::string_view a, std::string_view b)
{
    return a.size() == b.size() && compareCaseless(a, b) == 0;
}

} // namespace

std::optional<TreeIndex> TreeIndex::build(std::span<const Item> items, const std::atomic<bool> *cancel)
{
    TreeIndex index;
    auto &nodes = index.nodes_;
    nodes.reserve(items.size() + items.size() / 8 + 1);
    nodes.push_back({});
    index.itemNodes_.assign(items.size(), kNone);

    std::unordered_map<std::string, NodeId> folders;
    folders.emplace(std::string(), 0);
    std::string key;

    // The folder for `dir` (a prefix of some item path), created with its
    // ancestors when new. Parents are always created before their children,
    // so a parent's id is lower than its children's.
    auto folderOf = [&](std::string_view dir, auto &self) -> NodeId {
        key.assign(dir);
        std::transform(key.begin(), key.end(), key.begin(), lower);
        if (const auto it = folders.find(key); it != folders.end())
            return it->second;
        const std::size_t slash = dir.rfind('/');
        const NodeId parent = slash == std::string_view::npos ? 0 : self(dir.substr(0, slash), self);
        Node n;
        n.name = slash == std::string_view::npos ? dir : dir.substr(slash + 1);
        n.parent = parent;
        const auto id = static_cast<NodeId>(nodes.size());
        nodes.push_back(n);
        key.assign(dir);
        std::transform(key.begin(), key.end(), key.begin(), lower);
        folders.emplace(key, id);
        return id;
    };

    for (std::size_t i = 0; i < items.size(); ++i) {
        if (cancel && (i & 0xFFFF) == 0 && cancel->load(std::memory_order_relaxed))
            return std::nullopt;
        std::string_view p = items[i].path;
        while (!p.empty() && p.front() == '/')
            p.remove_prefix(1);
        const std::size_t slash = p.rfind('/');
        const std::string_view dir = slash == std::string_view::npos ? std::string_view() : p.substr(0, slash);
        const std::string_view name = slash == std::string_view::npos ? p : p.substr(slash + 1);
        const NodeId parent = dir.empty() ? 0 : folderOf(dir, folderOf);
        if (name.empty()) { // a zip folder entry ("a/b/")
            index.itemNodes_[i] = parent;
            continue;
        }
        Node n;
        n.name = name;
        n.parent = parent;
        n.item = static_cast<std::uint32_t>(i);
        n.files = 1;
        n.size = items[i].size;
        n.packed = items[i].packed;
        index.itemNodes_[i] = static_cast<NodeId>(nodes.size());
        nodes.push_back(n);
    }
    folders.clear();

    if (cancel && cancel->load(std::memory_order_relaxed))
        return std::nullopt;

    // Group siblings: by parent, folders first, then name ignoring case.
    std::vector<NodeId> order(nodes.size() - 1);
    for (std::size_t i = 0; i < order.size(); ++i)
        order[i] = static_cast<NodeId>(i + 1);
    std::sort(order.begin(), order.end(), [&nodes](NodeId a, NodeId b) {
        const Node &x = nodes[a], &y = nodes[b];
        if (x.parent != y.parent)
            return x.parent < y.parent;
        const bool fx = x.item != kNone, fy = y.item != kNone;
        if (fx != fy)
            return fy;
        if (const int c = compareCaseless(x.name, y.name); c != 0)
            return c < 0;
        if (x.name != y.name)
            return x.name < y.name;
        return a < b;
    });
    for (std::size_t i = 0; i < order.size(); ++i) {
        Node &n = nodes[order[i]];
        Node &parent = nodes[n.parent];
        if (parent.childCount == 0)
            parent.firstChild = static_cast<std::uint32_t>(i);
        n.row = static_cast<std::uint32_t>(i) - parent.firstChild;
        ++parent.childCount;
    }
    index.children_ = std::move(order);

    // Totals: children have higher ids than their parents.
    for (std::size_t id = nodes.size() - 1; id > 0; --id) {
        const Node &n = nodes[id];
        Node &parent = nodes[n.parent];
        parent.files += n.files;
        parent.size += n.size;
        parent.packed += n.packed;
    }
    return index;
}

std::string TreeIndex::path(NodeId id) const
{
    std::vector<std::string_view> parts;
    for (NodeId n = id; n != kNone && n != 0; n = nodes_[n].parent)
        parts.push_back(nodes_[n].name);
    std::string out;
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
        if (!out.empty())
            out += '/';
        out += *it;
    }
    return out;
}

std::optional<TreeIndex::NodeId> TreeIndex::find(std::string_view path) const
{
    NodeId at = 0;
    while (!path.empty()) {
        const std::size_t sep = path.find_first_of("/\\");
        const std::string_view part = path.substr(0, sep);
        path = sep == std::string_view::npos ? std::string_view() : path.substr(sep + 1);
        if (part.empty())
            continue;
        const Node &folder = nodes_[at];
        // More path to come needs a folder; at the end a file wins over a
        // folder of the same name.
        const bool wantFolder = !path.empty();
        NodeId found = kNone;
        for (std::uint32_t r = 0; r < folder.childCount; ++r) {
            const NodeId c = children_[folder.firstChild + r];
            if (!equalCaseless(nodes_[c].name, part))
                continue;
            if (isFolder(c) == wantFolder) {
                found = c;
                break;
            }
            if (!wantFolder && found == kNone)
                found = c;
        }
        if (found == kNone)
            return std::nullopt;
        at = found;
    }
    return at;
}

void TreeIndex::collectItems(NodeId id, std::vector<std::uint32_t> &out) const
{
    std::vector<NodeId> stack{id};
    while (!stack.empty()) {
        const NodeId n = stack.back();
        stack.pop_back();
        const Node &node = nodes_[n];
        if (node.item != kNone) {
            out.push_back(node.item);
            continue;
        }
        for (std::uint32_t r = node.childCount; r-- > 0;)
            stack.push_back(children_[node.firstChild + r]);
    }
}

} // namespace engine::p4k
