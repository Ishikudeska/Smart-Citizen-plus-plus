#pragma once

#include "engine/Error.h"
#include "engine/forge/DataForge.h"
#include "engine/xml/XmlTree.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

// game_data.json: weapons, ship armor, vehicles (hardpoints, armor and
// physics) and missile racks, in the shape battlestations reads. Ports
// sc.gamedata from Osiris-DevWorks/odw-fast-unp4k (Program.cs and the
// extractors): same passes over the same in-memory record DOMs, same
// numbers, same key order and .NET formatting, so the file matches the C#
// tool's output.
namespace engine::gamedata {

struct Options
{
    std::optional<std::string> channel; // --channel; omitted from the JSON when unset
    std::string baseIniPath;            // --base-ini; empty: names fall back to prettified ids
    std::string overlayPath;            // --overlay: backfill vehicle physics from an earlier file
    std::string generatedAt;            // empty: now, as DateTime.UtcNow.ToString("o")
};

// The console lines sc.gamedata prints (counts per pass), one per call.
using LogSink = std::function<void(const std::string &line)>;

// The JSON text (CRLF line ends, trailing newline, no BOM).
Result<std::string> buildGameDataJson(const forge::DataForge &forge, const Options &options,
                                      const LogSink &log = {});

// The records as DataForge.PathToRecordMap lists them, for tests: `paths`
// in map order (UTF-8), `build` renders one into `tree` and returns its
// root (XmlTree::kNone to skip), as ReadRecordByPathAsXml would.
struct RecordSource
{
    std::vector<std::string> paths;
    std::function<xml::XmlTree::NodeId(std::size_t, xml::XmlTree &)> build;
    int dcbVersion = 0;
};
Result<std::string> buildGameDataJson(const RecordSource &records, const Options &options,
                                      const LogSink &log = {});

} // namespace engine::gamedata
