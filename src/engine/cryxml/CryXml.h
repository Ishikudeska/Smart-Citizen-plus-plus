#pragma once

#include "engine/Error.h"
#include "engine/xml/XmlTree.h"

#include <cstdint>
#include <span>
#include <string>

namespace engine::cryxml {

// True if `data` starts with a CryXmlB, CryXml or CRY3SDK header.
bool isCryXml(std::span<const std::uint8_t> data);

// True if `data` looks like text XML (optionally after a UTF-8 BOM and
// leading whitespace).
bool isPlainXml(std::span<const std::uint8_t> data);

// Parses CryEngine binary XML into `tree` (cleared first) and returns the
// root element. Ports unforge's CryXmlSerializer: the byte order is detected
// from the file-length field, attributes are read sequentially, and
// non-blank content becomes a CDATA section.
Result<xml::XmlTree::NodeId> parse(std::span<const std::uint8_t> data, xml::XmlTree &tree);

// CryXmlB converted to XML text in unforge's writer format. Plain XML input
// is returned unchanged.
Result<std::string> toXml(std::span<const std::uint8_t> data);

} // namespace engine::cryxml
