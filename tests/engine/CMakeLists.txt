sc_add_test(tst_engine_version
    SOURCES tst_version.cpp
    LIBS scengine
    LABELS unit engine
)

sc_add_test(tst_engine_crypto
    SOURCES tst_crypto.cpp
    LIBS scengine
    LABELS unit engine
)

sc_add_test(tst_engine_dotnetformat
    SOURCES tst_dotnetformat.cpp
    LIBS scengine
    LABELS unit engine
)

sc_add_test(tst_engine_xml
    SOURCES tst_xml.cpp
    LIBS scengine
    LABELS unit engine
)

# These build their own fixtures, so they compress with zstd/zlib directly.
sc_add_test(tst_engine_p4k
    SOURCES tst_p4k.cpp P4kBuilder.h
    LIBS scengine libzstd_static zlibstatic
    LABELS unit engine
)

sc_add_test(tst_engine_cryxml
    SOURCES tst_cryxml.cpp
    LIBS scengine
    LABELS unit engine
)

sc_add_test(tst_engine_dataforge
    SOURCES tst_dataforge.cpp DcbBuilder.h
    LIBS scengine
    LABELS unit engine
)

sc_add_test(tst_engine_gamedata
    SOURCES tst_gamedata.cpp
    LIBS scengine Qt6::Core
    LABELS unit engine
)

sc_add_test(tst_engine_treeindex
    SOURCES tst_treeindex.cpp
    LIBS scengine
    LABELS unit engine
)
