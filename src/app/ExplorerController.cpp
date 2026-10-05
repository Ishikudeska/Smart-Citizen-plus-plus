#include "ExplorerController.h"

#include "AppController.h"
#include "core/EnginePaths.h"
#include "core/i18n/Translator.h"
#include "core/pipeline/Extraction.h"
#include "engine/cryxml/CryXml.h"
#include "engine/forge/Exporter.h"
#include "engine/forge/RecordBuilder.h"
#include "engine/p4k/Extractor.h"
#include "engine/xml/DotNetXmlWriter.h"

#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLocale>
#include <QLoggingCategory>
#include <QPointer>
#include <QStandardPaths>
#include <QThreadPool>

#include <algorithm>
#include <unordered_set>

Q_DECLARE_LOGGING_CATEGORY(lcApp)

using engine::p4k::TreeIndex;

namespace {

constexpr int kMaxResults = 5000;
constexpr std::uint64_t kMaxPreviewBytes = 64ull << 20; // read whole entries up to this size
constexpr std::size_t kHexBytes = 64 * 1024;
constexpr qsizetype kMaxPreviewChars = 1 << 20;

QString text(const char *key, const QVariantHash &args = {})
{
    return core::i18n::tr(key, args);
}

QString str(std::string_view s)
{
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}

QString size(std::uint64_t bytes)
{
    return P4kTreeModel::formatSize(bytes);
}

QString number(std::uint64_t n)
{
    return QLocale::system().toString(static_cast<qulonglong>(n));
}

QString localPath(const QUrl &url)
{
    return url.isEmpty() ? QString() : url.isLocalFile() ? url.toLocalFile() : url.toString();
}

// Builds the tree over the archive's entries and, when given, the
// database's records.
std::shared_ptr<ExplorerData> buildData(std::shared_ptr<const engine::p4k::Archive> archive,
                                        std::shared_ptr<const engine::forge::DataForge> forge,
                                        const std::string &dcbName, const std::atomic<bool> *cancel)
{
    auto data = std::make_shared<ExplorerData>();
    data->archive = std::move(archive);
    data->forge = std::move(forge);
    data->dcbName = dcbName;
    const engine::p4k::Archive &a = *data->archive;
    data->archiveItems = static_cast<std::uint32_t>(a.entryCount());
    if (data->forge) {
        const std::string prefix = dcbName + " (records)/";
        for (const std::uint32_t r : data->forge->fileRecords()) {
            std::string path = prefix;
            engine::xml::appendLatin1AsUtf8(path, data->forge->recordFileName(r));
            data->recordPaths.push_back(std::move(path));
            data->records.push_back(r);
        }
    }
    std::vector<TreeIndex::Item> items;
    items.reserve(a.entryCount() + data->recordPaths.size());
    for (std::size_t i = 0; i < a.entryCount(); ++i)
        items.push_back({a.name(i), a.entry(i).uncompressedSize, a.entry(i).compressedSize});
    for (const std::string &p : data->recordPaths)
        items.push_back({p, 0, 0});
    auto index = TreeIndex::build(items, cancel);
    if (!index)
        return nullptr;
    data->index = std::move(*index);
    return data;
}

std::string_view itemPath(const ExplorerData &d, std::uint32_t item)
{
    return d.isRecord(item) ? std::string_view(d.recordPaths[item - d.archiveItems]) : d.archive->name(item);
}

bool containsCaseless(std::string_view hay, std::string_view needle)
{
    if (needle.empty())
        return true;
    const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
    return std::search(hay.begin(), hay.end(), needle.begin(), needle.end(),
                       [&](char x, char y) { return lower(x) == lower(y); }) != hay.end();
}

QString dosDate(std::uint32_t dos)
{
    const int time = static_cast<int>(dos & 0xFFFF), date = static_cast<int>(dos >> 16);
    const QDateTime dt(QDate(1980 + (date >> 9), (date >> 5) & 0xF, date & 0x1F),
                       QTime(time >> 11, (time >> 5) & 0x3F, (time & 0x1F) * 2));
    return dt.isValid() ? QLocale::system().toString(dt, QLocale::ShortFormat) : QString();
}

QString hexDump(std::span<const std::uint8_t> bytes)
{
    QString out;
    out.reserve(static_cast<qsizetype>(bytes.size() / 16 + 1) * 78);
    for (std::size_t off = 0; off < bytes.size(); off += 16) {
        out += QStringLiteral("%1  ").arg(off, 8, 16, QLatin1Char('0'));
        QString ascii;
        for (std::size_t i = 0; i < 16; ++i) {
            if (off + i < bytes.size()) {
                const std::uint8_t b = bytes[off + i];
                out += QStringLiteral("%1 ").arg(b, 2, 16, QLatin1Char('0'));
                ascii += b >= 0x20 && b < 0x7F ? QChar(b) : QChar(u'.');
            } else {
                out += QStringLiteral("   ");
            }
            if (i == 7)
                out += u' ';
        }
        out += u' ';
        out += ascii;
        out += u'\n';
    }
    return out;
}

// Text a person would read: valid UTF-8 with no NULs in the first 8 KB.
bool looksLikeText(std::span<const std::uint8_t> bytes)
{
    const std::size_t n = std::min<std::size_t>(bytes.size(), 8192);
    if (n == 0)
        return true;
    int control = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint8_t b = bytes[i];
        if (b == 0)
            return false;
        if (b < 0x20 && b != '\n' && b != '\r' && b != '\t')
            ++control;
    }
    if (control * 50 > static_cast<int>(n))
        return false;
    QStringDecoder decoder(QStringDecoder::Utf8);
    (void)decoder.decode(
        QByteArrayView(reinterpret_cast<const char *>(bytes.data()), static_cast<qsizetype>(n)));
    // A cut multi-byte sequence at the end of the sample is not an error.
    return !decoder.hasError() || n < bytes.size();
}

struct Preview
{
    QString info, text, kind;
};

Preview truncated(QString body, const QString &kind)
{
    Preview p;
    p.kind = kind;
    if (body.size() > kMaxPreviewChars) {
        body.truncate(kMaxPreviewChars);
        p.info =
            text("scx.explorer_preview_truncated", {{QStringLiteral("count"), number(kMaxPreviewChars)}});
    }
    p.text = std::move(body);
    return p;
}

Preview previewEntry(const ExplorerData &d, std::uint32_t item)
{
    const engine::p4k::Archive &a = *d.archive;
    const engine::p4k::Entry &e = a.entry(item);
    std::vector<std::uint8_t> bytes;
    bool partial = false;
    if (e.uncompressedSize > kMaxPreviewBytes) {
        partial = true;
        auto r = a.read(item, [&bytes](std::span<const std::uint8_t> chunk) -> engine::Result<void> {
            bytes.insert(bytes.end(), chunk.begin(), chunk.end());
            if (bytes.size() >= kHexBytes)
                return engine::fail(engine::Errc::Cancelled, "enough");
            return {};
        });
        if (!r && r.error().code != engine::Errc::Cancelled)
            return {QString(), QString::fromStdString(r.error().message), QStringLiteral("message")};
    } else {
        auto r = a.read(item);
        if (!r)
            return {QString(), QString::fromStdString(r.error().message), QStringLiteral("message")};
        bytes = std::move(*r);
    }
    if (!partial && engine::cryxml::isCryXml(bytes)) {
        auto xml = engine::cryxml::toXml(bytes);
        if (xml) {
            Preview p = truncated(QString::fromStdString(*xml), QStringLiteral("xml"));
            p.info = text("scx.explorer_preview_cryxml") +
                     (p.info.isEmpty() ? QString() : QStringLiteral(" · ") + p.info);
            return p;
        }
        qCWarning(lcApp).noquote() << "CryXML preview failed:" << QString::fromStdString(xml.error().message);
    }
    if (!partial && (engine::cryxml::isPlainXml(bytes) || looksLikeText(bytes))) {
        std::size_t skip =
            bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF ? 3 : 0;
        return truncated(QString::fromUtf8(reinterpret_cast<const char *>(bytes.data() + skip),
                                           static_cast<qsizetype>(bytes.size() - skip)),
                         engine::cryxml::isPlainXml(bytes) ? QStringLiteral("xml") : QStringLiteral("text"));
    }
    const std::size_t shown = std::min(bytes.size(), kHexBytes);
    Preview p{QString(), hexDump(std::span(bytes).first(shown)), QStringLiteral("hex")};
    if (shown < e.uncompressedSize)
        p.info = text("scx.explorer_preview_hex_partial", {{QStringLiteral("shown"), size(shown)}});
    return p;
}

Preview previewRecord(const ExplorerData &d, std::uint32_t item, engine::forge::BuildOptions options)
{
    engine::forge::RecordBuilder builder(*d.forge, options);
    engine::xml::XmlTree tree;
    std::string out;
    if (!engine::forge::writeRecordXml(builder, tree, d.recordOf(item), out))
        return {QString(), text("scx.explorer_record_empty"), QStringLiteral("message")};
    return truncated(QString::fromStdString(out), QStringLiteral("xml"));
}

QString entryInfo(const ExplorerData &d, std::uint32_t item)
{
    if (d.isRecord(item)) {
        const auto &forge = *d.forge;
        const std::uint32_t r = d.recordOf(item);
        std::string s;
        engine::xml::appendLatin1AsUtf8(s, forge.recordName(r));
        QStringList bits{text("scx.explorer_record_info",
                              {{QStringLiteral("name"), QString::fromStdString(s)},
                               {QStringLiteral("type"),
                                QString::fromStdString(forge.structName(forge.records()[r].structIndex))}})};
        return bits.join(u' ');
    }
    const engine::p4k::Entry &e = d.archive->entry(item);
    QStringList bits;
    bits << text("scx.explorer_info_size", {{QStringLiteral("size"), size(e.uncompressedSize)},
                                            {QStringLiteral("bytes"), number(e.uncompressedSize)}});
    bits << text("scx.explorer_info_packed", {{QStringLiteral("size"), size(e.compressedSize)}});
    bits << QString::fromLatin1(engine::p4k::methodName(e.method));
    if (e.encrypted)
        bits << text("scx.explorer_encrypted");
    if (const QString date = dosDate(e.dosDateTime); !date.isEmpty())
        bits << date;
    return bits.join(QStringLiteral(" · "));
}

QString folderInfo(const TreeIndex::Node &n)
{
    return text("scx.explorer_folder_info", {{QStringLiteral("files"), number(n.files)},
                                             {QStringLiteral("size"), size(n.size)},
                                             {QStringLiteral("packed"), size(n.packed)}});
}

} // namespace

ExplorerController::ExplorerController(QObject *parent) : QObject(parent), tree_(new P4kTreeModel(this))
{}

ExplorerController::~ExplorerController() = default;

AppController &ExplorerController::app() const
{
    return *AppController::instance();
}

QString ExplorerController::summary() const
{
    if (!data_)
        return {};
    QString s = text("scx.explorer_summary", {{QStringLiteral("entries"), number(data_->archiveItems)},
                                              {QStringLiteral("size"), size(data_->archive->fileSize())}});
    if (data_->forge)
        s += QStringLiteral(" · ") +
             text("scx.explorer_summary_records", {{QStringLiteral("count"), number(data_->records.size())}});
    return s;
}

int ExplorerController::recordsNode() const
{
    if (!data_ || !data_->forge)
        return -1;
    const auto node = data_->index.find(data_->dcbName + " (records)");
    return node ? static_cast<int>(*node) : -1;
}

void ExplorerController::setMaxPointerDepth(int v)
{
    v = std::clamp(v, 0, 1000);
    if (v == maxPointerDepth_)
        return;
    maxPointerDepth_ = v;
    emit depthChanged();
    if (currentNode_ > 0)
        select(currentNode_);
}

void ExplorerController::setMaxReferenceDepth(int v)
{
    v = std::clamp(v, 0, 100);
    if (v == maxReferenceDepth_)
        return;
    maxReferenceDepth_ = v;
    emit depthChanged();
    if (currentNode_ > 0)
        select(currentNode_);
}

void ExplorerController::setLoaded(std::shared_ptr<const ExplorerData> data, const QString &path)
{
    data_ = std::move(data);
    archivePath_ = path;
    tree_->setExplorerData(data_);
    results_.clear();
    resultsNote_.clear();
    ++*searchSerial_;
    searching_ = false;
    emit resultsChanged();
    setPreview(-1, {}, {}, {}, QStringLiteral("none"), false);
    emit loadedChanged();
}

void ExplorerController::setPreview(int node, const QString &title, const QString &info, const QString &body,
                                    const QString &kind, bool busy)
{
    currentNode_ = node;
    previewTitle_ = title;
    previewInfo_ = info;
    previewText_ = body;
    previewKind_ = kind;
    previewBusy_ = busy;
    emit previewChanged();
}

// ── open ──────────────────────────────────────────────────────────────────

void ExplorerController::openUrl(const QUrl &url)
{
    open(localPath(url));
}

void ExplorerController::open(const QString &requested)
{
    const QString path = requested.isEmpty() ? app().p4kPath() : requested;
    if (path.isEmpty() || !QFileInfo(path).isFile()) {
        app().prompts()->warning(
            text("scx.explorer_title"),
            text("scx.explorer_no_archive", {{QStringLiteral("path"), QDir::toNativeSeparators(path)}}));
        return;
    }
    struct Opened
    {
        std::shared_ptr<const ExplorerData> data;
        QString error;
    };
    QPointer<ExplorerController> self(this);
    app().tasks()->run<Opened>(
        text("scx.explorer_opening"), true,
        [path](TaskRunner::Job &job) -> Opened {
            engine::p4k::OpenOptions options;
            const QString reading = text("scx.explorer_reading_directory");
            options.progress = [&job, &reading](std::uint64_t done, std::uint64_t total) {
                job.report(reading, static_cast<int>(done >> 20), static_cast<int>(total >> 20));
            };
            options.cancel = job.cancelFlag();
            auto archive = engine::p4k::Archive::open(core::fsPath(path), options);
            if (!archive)
                return {nullptr, archive.error().code == engine::Errc::Cancelled
                                     ? QString()
                                     : QString::fromStdString(archive.error().message)};
            job.report(text("scx.explorer_building_tree"));
            auto data = buildData(*archive, nullptr, {}, job.cancelFlag());
            return {std::move(data), {}};
        },
        [self, path](Opened result) {
            if (!self)
                return;
            if (!result.error.isEmpty()) {
                self->app().prompts()->error(text("scx.explorer_open_failed"), result.error);
                return;
            }
            if (!result.data)
                return; // cancelled
            qCInfo(lcApp).noquote() << "Explorer: opened" << path << "-" << result.data->archiveItems
                                    << "entries," << result.data->index.nodeCount() << "nodes";
            self->setLoaded(std::move(result.data), path);
        });
}

void ExplorerController::close()
{
    setLoaded(nullptr, {});
}

void ExplorerController::loadDataForge()
{
    if (!data_)
        return;
    std::string dcb;
    for (const char *candidate : {"Data/Game2.dcb", "Data/Game.dcb"})
        if (const auto i = data_->archive->find(candidate)) {
            dcb = std::string(data_->archive->name(*i));
            break;
        }
    if (dcb.empty()) {
        app().prompts()->warning(text("scx.explorer_title"), text("scx.explorer_no_dcb"));
        return;
    }
    struct Loaded
    {
        std::shared_ptr<const ExplorerData> data;
        QString error;
    };
    QPointer<ExplorerController> self(this);
    auto archive = data_->archive;
    app().tasks()->run<Loaded>(
        text("scx.explorer_loading_forge"), true,
        [archive, dcb](TaskRunner::Job &job) -> Loaded {
            job.report(text("scx.explorer_reading_dcb", {{QStringLiteral("name"), str(dcb)}}));
            const auto index = archive->find(dcb);
            auto bytes = archive->read(*index);
            if (!bytes)
                return {nullptr, QString::fromStdString(bytes.error().message)};
            if (job.cancelled())
                return {};
            auto forge = engine::forge::DataForge::load(std::move(*bytes));
            if (!forge)
                return {nullptr, QString::fromStdString(forge.error().message)};
            job.report(text("scx.explorer_building_tree"));
            auto shared = std::make_shared<const engine::forge::DataForge>(std::move(*forge));
            return {buildData(archive, std::move(shared), dcb, job.cancelFlag()), {}};
        },
        [self](Loaded result) {
            if (!self)
                return;
            if (!result.error.isEmpty()) {
                self->app().prompts()->error(text("scx.explorer_forge_failed"), result.error);
                return;
            }
            if (result.data)
                self->setLoaded(std::move(result.data), self->archivePath_);
        });
}

// ── search ────────────────────────────────────────────────────────────────

void ExplorerController::search(const QString &pattern)
{
    const quint64 serial = ++*searchSerial_;
    const std::string needle = pattern.trimmed().toStdString();
    if (!data_ || needle.empty()) {
        results_.clear();
        resultsNote_.clear();
        searching_ = false;
        emit resultsChanged();
        return;
    }
    searching_ = true;
    emit resultsChanged();
    QPointer<ExplorerController> self(this);
    auto data = data_;
    QThreadPool::globalInstance()->start([self, data, needle, serial, current = searchSerial_] {
        const bool glob = needle.find_first_of("*?") != std::string::npos;
        const std::string globPattern = glob && needle.front() != '*' ? "*" + needle : needle;
        std::vector<std::uint32_t> hits;
        std::size_t total = 0;
        const std::size_t items = data->archiveItems + data->recordPaths.size();
        for (std::size_t i = 0; i < items; ++i) {
            if ((i & 0x3FFF) == 0 && *current != serial)
                return; // superseded
            const std::string_view path = itemPath(*data, static_cast<std::uint32_t>(i));
            if (glob ? engine::p4k::globMatches(globPattern, path) : containsCaseless(path, needle)) {
                if (hits.size() < kMaxResults)
                    hits.push_back(static_cast<std::uint32_t>(i));
                ++total;
            }
        }
        QVariantList rows;
        rows.reserve(static_cast<qsizetype>(hits.size()));
        for (const std::uint32_t item : hits) {
            const TreeIndex::NodeId node = data->index.nodeOfItem(item);
            const TreeIndex::Node &n = data->index.node(node);
            rows.push_back(
                QVariantMap{{QStringLiteral("node"), static_cast<int>(node)},
                            {QStringLiteral("path"), str(itemPath(*data, item))},
                            {QStringLiteral("size"), data->isRecord(item) ? QString() : size(n.size)}});
        }
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, serial, rows = std::move(rows), total, shown = hits.size()] {
                if (!self || *self->searchSerial_ != serial)
                    return;
                self->results_ = rows;
                self->resultsNote_ =
                    total == shown
                        ? text("scx.explorer_results", {{QStringLiteral("count"), number(total)}})
                        : text("scx.explorer_results_capped", {{QStringLiteral("count"), number(total)},
                                                               {QStringLiteral("shown"), number(shown)}});
                self->searching_ = false;
                emit self->resultsChanged();
            },
            Qt::QueuedConnection);
    });
}

// ── preview ───────────────────────────────────────────────────────────────

QString ExplorerController::nodePath(int node) const
{
    if (!data_ || node < 0 || static_cast<std::size_t>(node) >= data_->index.nodeCount())
        return {};
    return str(data_->index.path(static_cast<TreeIndex::NodeId>(node)));
}

bool ExplorerController::isFolder(int node) const
{
    return data_ && node >= 0 && static_cast<std::size_t>(node) < data_->index.nodeCount() &&
           data_->index.isFolder(static_cast<TreeIndex::NodeId>(node));
}

int ExplorerController::findNode(const QString &path) const
{
    if (!data_)
        return -1;
    const auto node = data_->index.find(path.toStdString());
    return node ? static_cast<int>(*node) : -1;
}

void ExplorerController::copyPath(int node) const
{
    QString path = nodePath(node);
    if (data_ && node > 0) {
        const TreeIndex::Node &n = data_->index.node(static_cast<TreeIndex::NodeId>(node));
        if (n.item != TreeIndex::kNone && data_->isRecord(n.item))
            path = str(data_->recordPath(n.item));
    }
    QGuiApplication::clipboard()->setText(path);
}

void ExplorerController::select(int node)
{
    const quint64 serial = ++previewSerial_;
    if (!data_ || node <= 0 || static_cast<std::size_t>(node) >= data_->index.nodeCount()) {
        setPreview(-1, {}, {}, {}, QStringLiteral("none"), false);
        return;
    }
    const auto id = static_cast<TreeIndex::NodeId>(node);
    const TreeIndex::Node &n = data_->index.node(id);
    const QString title = str(data_->index.path(id));
    if (n.item == TreeIndex::kNone) {
        setPreview(node, title, folderInfo(n), {}, QStringLiteral("none"), false);
        return;
    }
    const QString info = entryInfo(*data_, n.item);
    setPreview(node, title, info, {}, QStringLiteral("none"), true);
    QPointer<ExplorerController> self(this);
    auto data = data_;
    const std::uint32_t item = n.item;
    engine::forge::BuildOptions options;
    options.maxPointerDepth = maxPointerDepth_;
    options.maxReferenceDepth = maxReferenceDepth_;
    QThreadPool::globalInstance()->start([self, data, item, options, serial, node, title, info] {
        Preview p = data->isRecord(item) ? previewRecord(*data, item, options) : previewEntry(*data, item);
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, serial, node, title, info, p = std::move(p)] {
                if (!self || self->previewSerial_ != serial)
                    return;
                self->setPreview(node, title, p.info.isEmpty() ? info : info + QStringLiteral(" · ") + p.info,
                                 p.text, p.kind, false);
            },
            Qt::QueuedConnection);
    });
}

// ── extract ───────────────────────────────────────────────────────────────

QString ExplorerController::defaultExtractDir() const
{
    return QDir(app().userDataRoot()).filePath(QStringLiteral("extracted"));
}

void ExplorerController::extract(const QVariantList &nodes, const QUrl &outputDir, bool convertCryXml,
                                 bool skipExisting)
{
    if (!data_)
        return;
    std::vector<std::uint32_t> items;
    for (const QVariant &v : nodes) {
        const int node = v.toInt();
        if (node >= 0 && static_cast<std::size_t>(node) < data_->index.nodeCount())
            data_->index.collectItems(static_cast<TreeIndex::NodeId>(node), items);
    }
    std::sort(items.begin(), items.end());
    items.erase(std::unique(items.begin(), items.end()), items.end());
    if (items.empty()) {
        app().prompts()->info(text("scx.explorer_extract_title"), text("scx.explorer_extract_nothing"));
        return;
    }
    const QString out = localPath(outputDir);
    std::vector<std::size_t> entries;
    std::vector<std::uint32_t> recordItems;
    for (const std::uint32_t item : items) {
        if (data_->isRecord(item))
            recordItems.push_back(item);
        else
            entries.push_back(item);
    }

    struct Extracted
    {
        std::size_t files = 0, skipped = 0;
        std::uint64_t bytes = 0;
        QStringList failures;
        QString error;
        bool cancelled = false;
    };
    QPointer<ExplorerController> self(this);
    auto data = data_;
    engine::forge::BuildOptions build;
    build.maxPointerDepth = maxPointerDepth_;
    build.maxReferenceDepth = maxReferenceDepth_;
    app().tasks()->run<Extracted>(
        text("scx.explorer_extract_title"), true,
        [data, entries, recordItems, out, convertCryXml, skipExisting,
         build](TaskRunner::Job &job) -> Extracted {
            Extracted result;
            const auto outRoot = core::fsPath(out);
            if (!entries.empty()) {
                engine::p4k::ExtractOptions options;
                options.outputDir = outRoot;
                options.skipExisting = skipExisting;
                options.cancel = job.cancelFlag();
                const QString message = text("scx.explorer_extracting_files");
                options.progress = [&job, &message](std::size_t done, std::size_t total) {
                    job.report(message, static_cast<int>(done), static_cast<int>(total));
                };
                if (convertCryXml)
                    options.transform =
                        [](std::string_view,
                           std::vector<std::uint8_t> bytes) -> engine::Result<std::vector<std::uint8_t>> {
                        if (!engine::cryxml::isCryXml(bytes))
                            return bytes;
                        auto xml = engine::cryxml::toXml(bytes);
                        if (!xml)
                            return std::unexpected(xml.error());
                        return std::vector<std::uint8_t>(xml->begin(), xml->end());
                    };
                auto stats = engine::p4k::extract(*data->archive, entries, options);
                if (!stats) {
                    result.cancelled = stats.error().code == engine::Errc::Cancelled;
                    result.error = QString::fromStdString(stats.error().message);
                    return result;
                }
                result.files += stats->extracted;
                result.skipped += stats->skipped;
                result.bytes += stats->bytes;
                for (const std::string &f : stats->failures)
                    result.failures << QString::fromStdString(f);
            }
            if (!recordItems.empty() && !job.cancelled()) {
                std::unordered_set<std::string_view> wanted;
                for (const std::uint32_t item : recordItems)
                    wanted.insert(data->recordPath(item));
                engine::forge::ExportOptions options;
                options.build = build;
                options.include = [&wanted](std::string_view path) { return wanted.contains(path); };
                options.cancel = job.cancelFlag();
                const QString message = text("scx.explorer_extracting_records");
                options.progress = [&job, &message](std::size_t done, std::size_t total) {
                    job.report(message, static_cast<int>(done), static_cast<int>(total));
                };
                auto stats = engine::forge::exportRecords(*data->forge, outRoot, options);
                if (!stats) {
                    result.cancelled = stats.error().code == engine::Errc::Cancelled;
                    result.error = QString::fromStdString(stats.error().message);
                    return result;
                }
                result.files += stats->written;
                result.skipped += stats->skipped;
                result.bytes += stats->bytes;
                for (const std::string &f : stats->failures)
                    result.failures << QString::fromStdString(f);
            }
            result.cancelled = result.cancelled || job.cancelled();
            return result;
        },
        [self, out](const Extracted &r) {
            if (!self)
                return;
            auto &prompts = *self->app().prompts();
            if (r.cancelled) {
                self->app().setStatus(text("scx.explorer_extract_cancelled"));
                return;
            }
            if (!r.error.isEmpty()) {
                prompts.error(text("scx.explorer_extract_title"), r.error);
                return;
            }
            qCInfo(lcApp).noquote() << "Explorer: extracted" << r.files << "files," << r.bytes << "bytes to"
                                    << out << "(" << r.failures.size() << "failures)";
            QString body =
                text("scx.explorer_extract_done", {{QStringLiteral("count"), number(r.files)},
                                                   {QStringLiteral("size"), size(r.bytes)},
                                                   {QStringLiteral("path"), QDir::toNativeSeparators(out)}});
            if (r.skipped)
                body += QStringLiteral("\n") +
                        text("scx.explorer_extract_skipped", {{QStringLiteral("count"), number(r.skipped)}});
            if (!r.failures.isEmpty()) {
                body += QStringLiteral("\n") + text("scx.explorer_extract_failures",
                                                    {{QStringLiteral("count"), number(r.failures.size())}});
                prompts.warning(text("scx.explorer_extract_title"), body, r.failures.join(u'\n'));
                return;
            }
            prompts.info(text("scx.explorer_extract_title"), body);
        });
}

// ── game data ─────────────────────────────────────────────────────────────

QString ExplorerController::defaultGameDataPath() const
{
    return QDir(app().userDataRoot()).filePath(QStringLiteral("game_data.json"));
}

void ExplorerController::exportGameData(const QUrl &output, const QString &channel, const QUrl &baseIni,
                                        const QUrl &overlay)
{
    const QString out = localPath(output);
    if (out.isEmpty())
        return;
    const QString base = localPath(baseIni);
    const QString over = localPath(overlay);
    std::shared_ptr<const engine::p4k::Archive> archive = data_ ? data_->archive : nullptr;
    const QString p4k = app().p4kPath();
    if (!archive && (p4k.isEmpty() || !QFileInfo(p4k).isFile())) {
        app().prompts()->warning(
            text("scx.explorer_gamedata_title"),
            text("scx.explorer_no_archive", {{QStringLiteral("path"), QDir::toNativeSeparators(p4k)}}));
        return;
    }
    struct Exported
    {
        QStringList summary;
        QString error;
    };
    QPointer<ExplorerController> self(this);
    app().tasks()->run<Exported>(
        text("scx.explorer_gamedata_title"), false,
        [archive, p4k, out, base, over, channel](TaskRunner::Job &job) -> Exported {
            std::shared_ptr<const engine::p4k::Archive> a = archive;
            if (!a) {
                job.report(text("scx.explorer_opening"));
                auto opened = engine::p4k::Archive::open(core::fsPath(p4k));
                if (!opened)
                    return {{}, QString::fromStdString(opened.error().message)};
                a = *opened;
            }
            auto result = core::exportGameData(
                *a, out, base, over, channel, [&job](const QString &step, qint64 done, qint64 total) {
                    job.report(step, static_cast<int>(done), static_cast<int>(total));
                });
            if (!result)
                return {{}, QString::fromStdString(result.error().message)};
            return {*result, {}};
        },
        [self, out](const Exported &r) {
            if (!self)
                return;
            if (!r.error.isEmpty()) {
                self->app().prompts()->error(text("scx.explorer_gamedata_title"), r.error);
                return;
            }
            self->app().prompts()->info(
                text("scx.explorer_gamedata_title"),
                text("scx.explorer_gamedata_done", {{QStringLiteral("path"), QDir::toNativeSeparators(out)}}),
                r.summary.join(u'\n'));
        });
}
