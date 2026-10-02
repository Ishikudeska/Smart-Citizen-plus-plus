#include "core/enhancements/Xml.h"

#include "core/EnginePaths.h"
#include "engine/io/FileSystem.h"

namespace core::enh {

namespace {

struct Step
{
    bool descendant = false; // reached through "//"
    std::string_view tag;    // "*" matches any element
    std::string_view attr;   // predicate [@attr] / [@attr='value']
    std::optional<std::string_view> value;
};

// ".//a/b[@x='y']" -> steps. Only the forms the generator uses.
std::vector<Step> compile(std::string_view path)
{
    std::vector<Step> steps;
    bool descendant = false;
    if (path.starts_with(".//")) {
        descendant = true;
        path.remove_prefix(3);
    } else if (path.starts_with("./")) {
        path.remove_prefix(2);
    }
    while (!path.empty()) {
        std::size_t end = 0;
        int depth = 0;
        while (end < path.size() && (path[end] != '/' || depth > 0)) {
            depth += path[end] == '[' ? 1 : path[end] == ']' ? -1 : 0;
            ++end;
        }
        std::string_view part = path.substr(0, end);
        Step step;
        step.descendant = descendant;
        if (const std::size_t open = part.find('['); open != std::string_view::npos) {
            std::string_view pred = part.substr(open + 1, part.size() - open - 2); // @attr='v'
            part = part.substr(0, open);
            pred.remove_prefix(1);
            if (const std::size_t eq = pred.find('='); eq != std::string_view::npos) {
                step.attr = pred.substr(0, eq);
                step.value = pred.substr(eq + 2, pred.size() - eq - 3);
            } else {
                step.attr = pred;
            }
        }
        step.tag = part;
        steps.push_back(step);
        path.remove_prefix(end);
        descendant = false;
        if (path.starts_with("//")) {
            descendant = true;
            path.remove_prefix(2);
        } else if (path.starts_with("/")) {
            path.remove_prefix(1);
        }
    }
    return steps;
}

bool matches(Node n, const Step &step)
{
    if (n.type() != pugi::node_element)
        return false;
    if (step.tag != "*" && tag(n) != step.tag)
        return false;
    if (step.attr.empty())
        return true;
    const pugi::xml_attribute a = n.attribute(std::string(step.attr).c_str());
    if (!a)
        return false;
    return !step.value || std::string_view(a.value()) == *step.value;
}

// Runs the steps from `context`; `visit` returns false to stop early.
template <class Visit>
bool select(Node context, const std::vector<Step> &steps, std::size_t i, Visit &visit)
{
    if (i == steps.size())
        return visit(context);
    const Step &step = steps[i];
    if (step.descendant) {
        bool keepGoing = true;
        forEachElement(context, [&](Node n) {
            if (n != context && matches(n, step))
                keepGoing = select(n, steps, i + 1, visit);
            return keepGoing;
        });
        return keepGoing;
    }
    for (Node c = context.first_child(); c; c = c.next_sibling())
        if (matches(c, step) && !select(c, steps, i + 1, visit))
            return false;
    return true;
}

} // namespace

XmlDoc XmlDoc::load(const QString &path)
{
    auto doc = std::make_shared<pugi::xml_document>();
#ifdef _WIN32
    const std::wstring native = engine::io::longPath(fsPath(path));
    const pugi::xml_parse_result ok = doc->load_file(native.c_str());
#else
    const pugi::xml_parse_result ok = doc->load_file(path.toUtf8().constData());
#endif
    XmlDoc out;
    if (ok && doc->document_element())
        out.doc_ = std::move(doc);
    return out;
}

XmlDoc XmlDoc::parse(std::string_view text)
{
    auto doc = std::make_shared<pugi::xml_document>();
    XmlDoc out;
    if (doc->load_buffer(text.data(), text.size()) && doc->document_element())
        out.doc_ = std::move(doc);
    return out;
}

std::optional<std::string_view> get(Node n, const char *name)
{
    const pugi::xml_attribute a = n.attribute(name);
    if (!a)
        return std::nullopt;
    return std::string_view(a.value());
}

std::string_view getOr(Node n, const char *name, std::string_view fallback)
{
    const pugi::xml_attribute a = n.attribute(name);
    return a ? std::string_view(a.value()) : fallback;
}

QString qs(std::string_view utf8)
{
    return QString::fromUtf8(utf8.data(), qsizetype(utf8.size()));
}

Node find(Node n, std::string_view path)
{
    const std::vector<Step> steps = compile(path);
    Node found;
    auto visit = [&](Node m) {
        found = m;
        return false;
    };
    select(n, steps, 0, visit);
    return found;
}

std::vector<Node> findAll(Node n, std::string_view path)
{
    const std::vector<Step> steps = compile(path);
    std::vector<Node> out;
    auto visit = [&](Node m) {
        out.push_back(m);
        return true;
    };
    select(n, steps, 0, visit);
    return out;
}

std::vector<Node> iter(Node n, std::string_view tagName)
{
    std::vector<Node> out;
    forEachElement(n, [&](Node m) {
        if (tagName.empty() || tag(m) == tagName)
            out.push_back(m);
        return true;
    });
    return out;
}

std::vector<Node> children(Node n)
{
    std::vector<Node> out;
    for (Node c = n.first_child(); c; c = c.next_sibling())
        if (c.type() == pugi::node_element)
            out.push_back(c);
    return out;
}

Node findDescendant(Node root, std::string_view tagName)
{
    Node found;
    forEachElement(root, [&](Node m) {
        if (m != root && tag(m) == tagName) {
            found = m;
            return false;
        }
        return true;
    });
    return found;
}

Node findByType(Node root, std::string_view typeName)
{
    Node found;
    forEachElement(root, [&](Node m) {
        if (getOr(m, "__type") == typeName || tag(m) == typeName) {
            found = m;
            return false;
        }
        return true;
    });
    return found;
}

std::optional<std::string_view> attrOf(Node root, std::string_view tagName, const char *attr)
{
    const Node el = findDescendant(root, tagName);
    return el ? get(el, attr) : std::nullopt;
}

} // namespace core::enh
