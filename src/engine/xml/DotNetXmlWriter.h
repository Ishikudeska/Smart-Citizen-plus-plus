#pragma once

#include "engine/xml/XmlTree.h"

#include <optional>
#include <string>
#include <string_view>

namespace engine::xml {

// Serializes `root` the way .NET's XmlWriter does with unforge's settings:
// UTF-8 without BOM or declaration, two-space indent, CRLF line breaks,
// " />" for empty elements, and no newline at the end. In attribute values
// & < > " are escaped, \t \n \r become &#x9; &#xA; &#xD;, and other control
// characters become &#xN;. After a CDATA section the rest of that element is
// written without indentation (.NET's "mixed content" rule).
void writeDotNet(const XmlTree &tree, XmlTree::NodeId root, std::string &out);

// The exception message System.Xml.XmlDocument throws when asked to create
// an element or attribute with this name, or nothing if the name is
// accepted. `latin1` holds one character per byte, as DataForge and CryXml
// store names.
std::optional<std::string> dotNetNameError(std::string_view latin1);

// Appends Latin-1 text (one character per byte) as UTF-8.
void appendLatin1AsUtf8(std::string &out, std::string_view latin1);

} // namespace engine::xml
