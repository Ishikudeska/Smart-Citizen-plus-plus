# Format library: P4K archives, DataForge, CryXML, game-data export.
# Deliberately Qt-free so it can be tested and reused without an event loop.

add_library(scengine STATIC
    Error.h
    Parallel.h
    Try.h
    Version.h
    Version.cpp

    io/RandomAccessFile.h
    io/RandomAccessFile.cpp
    io/FileSystem.h
    io/FileSystem.cpp

    crypto/Aes128.h
    crypto/Aes128.cpp
    crypto/Sha256.h
    crypto/Sha256.cpp

    p4k/Archive.h
    p4k/Archive.cpp
    p4k/Extractor.h
    p4k/Extractor.cpp
    p4k/TreeIndex.h
    p4k/TreeIndex.cpp

    cryxml/CryXml.h
    cryxml/CryXml.cpp

    zip/ZipWriter.h
    zip/ZipWriter.cpp

    xml/XmlTree.h
    xml/XmlTree.cpp
    xml/DotNetXmlWriter.h
    xml/DotNetXmlWriter.cpp

    forge/DotNetFormat.h
    forge/DotNetFormat.cpp
    forge/DataForge.h
    forge/DataForge.cpp
    forge/RecordBuilder.h
    forge/RecordBuilder.cpp
    forge/Exporter.h
    forge/Exporter.cpp

    gamedata/DotNet.h
    gamedata/DotNet.cpp
    gamedata/Json.h
    gamedata/Json.cpp
    gamedata/GameData.h
    gamedata/GameData.cpp
)

target_include_directories(scengine PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(scengine
    PUBLIC  pugixml::static
    PRIVATE libzstd_static zlibstatic
)
set_target_properties(scengine PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
sc_set_warnings(scengine)
