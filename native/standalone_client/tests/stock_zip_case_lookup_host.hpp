// Production ZIP algorithms and owning StringRefMap, with host-only I/O boundaries.
#include <algorithm>
#include <cassert>
#include <cctype>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <vector>

static size_t lowerComparisons = 0;
static size_t keyComparisons = 0;
static size_t warningCount = 0;
#define MF_ASSERT(value) assert(value)
#define MF_MIN(a,b) std::min(a,b)
#define WARNING_MSG(...) (++warningCount)
#define ENABLE_MSG_LOGGING 0
#define ENABLE_FILE_CASE_CHECKING 1
#define bw_snprintf std::snprintf
#define bw_malloc std::malloc
#define bw_free std::free
#define bw_strdup strdup
using uint32 = unsigned int;
using BinaryPtr = const char*;

// The Windows build calls the real CRT. This host boundary reproduces its C
// locale byte/NUL contract; it does not claim Windows locale acceptance.
static char* _strlwr(char* value)
{
    for (char* p = value; *p; ++p)
        *p = static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
    return value;
}

namespace BW {
using string = std::string;
template<class K, class V, class C = std::less<K>> using map = std::map<K, V, C>;
// Non-owning, length-aware references preserve the production map's key
// ownership, substring and embedded-NUL boundaries.
class StringRef {
    const char* data_;
    size_t size_;
public:
    static const size_t npos = std::string::npos;
    StringRef(const char* data): data_(data), size_(std::strlen(data)) {}
    StringRef(const char* data, size_t size): data_(data), size_(size) {}
    StringRef(const std::string& data): data_(data.data()), size_(data.size()) {}
    const char* data() const { return data_; }
    size_t size() const { return size_; }
    size_t length() const { return size_; }
    char operator[](size_t offset) const { assert(offset < size_); return data_[offset]; }
    std::string to_string() const { return std::string(data_, size_); }
    operator std::string() const { return to_string(); }
    size_t find_first_of(char value) const { return to_string().find_first_of(value); }
    size_t rfind(char value) const { return to_string().rfind(value); }
    StringRef substr(size_t start, size_t count = npos) const {
        assert(start <= size_);
        return StringRef(data_ + start, std::min(count, size_ - start));
    }
    bool equals_lower(const StringRef& other) const {
        ++lowerComparisons;
        if (size_ != other.size_) return false;
        for (size_t i = 0; i < size_; ++i)
            if (std::tolower(static_cast<unsigned char>(data_[i])) !=
                std::tolower(static_cast<unsigned char>(other.data_[i]))) return false;
        return true;
    }
    bool operator<(const StringRef& other) const {
        ++keyComparisons;
        const int result = std::memcmp(data_, other.data_, std::min(size_, other.size_));
        return result < 0 || (result == 0 && size_ < other.size_);
    }
};
inline std::string operator+(const std::string& left, const StringRef& right) {
    return left + right.to_string();
}
}

@MAP@
@LOOKUPS@

struct ZipFileSystem {
    struct LocalFile {
        BW::string filename_;
        BinaryPtr pData_;
        bool folder_ = false;
        LocalFile(): pData_(nullptr) {}
        LocalFile(const BW::string& name, BinaryPtr data): filename_(name), pData_(data) {}
        void update(const BW::string& name) { filename_ = name; }
        void clear() { filename_.clear(); pData_ = nullptr; }
        bool isFolder() const { return folder_; }
        void isFolder(bool value) { folder_ = value; }
    };
    using IndexPair = std::pair<uint32, uint32>;
    using Directory = std::vector<BW::string>;
    using DirPair = std::pair<Directory, Directory>;
    using FileMap = StringRefMap<IndexPair>;
    using FileDuplicatesMap = StringRefMap<uint32>;
    using DirMap = StringRefMap<DirPair>;
    FileMap fileMap_;
    DirMap dirMap_;
    FileDuplicatesMap duplicates_;
    std::vector<LocalFile> centralDirectory_;
    FILE* pFile_ = nullptr;
    BW::string path_, tag_;
    ZipFileSystem* parentZip_ = nullptr;
    bool internalChildNode() const { return parentZip_ != nullptr; }
    const BW::string& tag() const { return tag_; }
    bool checkDuplicate(const BW::StringRef&) const;
    BW::StringRef decodeDuplicate(const BW::StringRef&) const;
    BW::string encodeDuplicate(const BW::StringRef&, int) const;
    void resolvePath(BW::string&, bool bExtraChecks = false) const;
    bool resolveDuplicate(BW::string&) const;
    void updateFile(const BW::StringRef&, BinaryPtr);
    bool writeToStructure(const BW::StringRef&, BinaryPtr);
    bool eraseFileOrDirectory(const BW::StringRef&);
    bool moveFileOrDirectory(const BW::StringRef&, const BW::StringRef&);
    bool copyFileOrDirectory(const BW::StringRef&, const BW::StringRef&);
    bool dirTest(const BW::StringRef&);
    void clear(const BW::StringRef&);
    void closeZip();
    // Only central-directory byte I/O is replaced. The entry-to-map loop below
    // is extracted from openZip, including its real recursive directory writes.
    void ingest(const std::vector<BW::string>& names) {
        closeZip();
        DirPair blankDir;
        dirMap_[""] = blankDir;
        centralDirectory_.resize(names.size());
        BW::string dirComponent, fileComponent;
        int pos;
        for (uint32 i = 0; i < names.size(); ++i) {
            LocalFile& localFile = centralDirectory_[i];
            localFile.filename_ = names[i];
            @INGEST@
        }
    }
    void assertCanonicalKeys() const {
        for (const auto& entry: fileMap_) { BW::string key = entry.first.to_string(); cleanSlashes(key); assert(key == entry.first.to_string()); }
        for (const auto& entry: dirMap_) { BW::string key = entry.first.to_string(); cleanSlashes(key); assert(key == entry.first.to_string()); }
        for (const auto& entry: duplicates_) { BW::string key = entry.first.to_string(); cleanSlashes(key); assert(key == entry.first.to_string()); }
    }
};

@METHODS@

static void normalisationAndLookup()
{
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"", ""}, {"////", ""}, {"A", "a"}, {"//Vehicles\\GERMAN///GUN.model/", "vehicles/german/gun.model"},
        {"A/./B/../C", "a/./b/../c"}, {"A...B", "a...b"},
        {"\xc4/\xc3\x89/ABC", "\xc4/\xc3\x89/abc"},
        {std::string("A\0B\\C", 5), std::string("a\0B/C", 5)},
        {std::string("\0A", 2), std::string("\0A", 2)}
    };
    StringRefMap<int> map;
    for (const auto& item: cases) {
        BW::string key = item.first;
        cleanSlashes(key);
        assert(key == item.second);
        map[key] = 7;
        assert(fileMapLookup(map, item.first)->second == 7);
        assert(fileMapLookupConst(map, item.first)->second == 7);
    }
    assert(fileMapLookup(map, "absent") == map.end());
    assert(fileMapLookupConst(map, "absent") == map.end());
    assert(fileMapLookup(map, "a/b/c") == map.end()); // no dot-segment collapse
    assert(fileMapLookup(map, "\xc3\xa4/\xc3\xa9/abc") == map.end()); // no Unicode folding
    BW::string nulKey("A\0b\\C", 5);
    assert(fileMapLookup(map, nulKey) == map.end()); // case after NUL stays distinct
    StringRefMap<int> copy(map);
    map.clear();
    assert(fileMapLookup(copy, "A")->second == 7);
    StringRefMap<int> assigned;
    assigned = copy;
    copy.erase(fileMapLookup(copy, "A"));
    assert(fileMapLookup(copy, "A") == copy.end());
    assert(fileMapLookupConst(assigned, "a")->second == 7);
    copy["a"] = 9;
    assert(fileMapLookup(copy, "A")->second == 9);
    assert(lowerComparisons == 0);
}

static void centralCollisionsAndDirectories()
{
    ZipFileSystem zip;
    zip.ingest({"Models/Tank.MODEL", "models/tank.model", "MODELS\\TANK.model", "MODELS/Other.visual"});
    const auto hit = fileMapLookup(zip.fileMap_, "Models/Tank.MODEL");
    assert(hit != zip.fileMap_.end() && hit->second.first == 2);
    assert(fileMapLookupConst(zip.fileMap_, "models/tank.model")->second.first == 2);
    assert(zip.dirTest("//MODELS\\"));
    assert(zip.dirTest(""));
    assert(!zip.dirTest("missing"));
    assert(!zip.dirTest("models/tank.model"));
    assert(zip.centralDirectory_[2].filename_ == "models/tank.model");
    zip.assertCanonicalKeys();
    ZipFileSystem nested;
    nested.parentZip_ = &zip;
    nested.tag_ = "MODELS";
    assert(nested.dirTest(""));
    assert(!nested.dirTest("missing"));
    // Physical duplicate ZIP names select the final entry, independently of
    // the engine's space-suffixed logical duplicate representation.
    zip.ingest({"DUP/file", "dup/FILE", "dup/file"});
    assert(fileMapLookup(zip.fileMap_, "DUP/FILE")->second.first == 2);
    zip.ingest({"DUP/Foo 1", "dup/FOO 2"});
    assert(fileMapLookup(zip.duplicates_, "DUP/foo")->second == 2);
    BW::string resolved = "DUP/FOO";
    zip.resolvePath(resolved);
    assert(zip.resolveDuplicate(resolved) && resolved == "dup/foo 1");
    assert(zip.eraseFileOrDirectory("DUP/FOO"));
    assert(fileMapLookup(zip.fileMap_, "dup/foo 2") == zip.fileMap_.end());
    assert(fileMapLookup(zip.fileMap_, "DUP/FOO 1") != zip.fileMap_.end());
    zip.assertCanonicalKeys();
    zip.closeZip();
    zip.closeZip();
    assert(zip.fileMap_.empty() && zip.dirMap_.empty() && zip.duplicates_.empty());
    zip.ingest({"Reopened/A"});
    assert(fileMapLookup(zip.fileMap_, "REOPENED/a") != zip.fileMap_.end());
    assert(fileMapLookup(zip.fileMap_, "dup/file") == zip.fileMap_.end());
}

static void writesDuplicatesEraseAndClear()
{
    ZipFileSystem zip;
    zip.ingest({});
    assert(zip.writeToStructure("//DIR\\A.XML/", "first"));
    assert(zip.writeToStructure("dir/a.xml", "second"));
    assert(zip.writeToStructure("DIR/A.XML", "third"));
    assert(fileMapLookup(zip.fileMap_, "dir/a.xml") == zip.fileMap_.end());
    assert(fileMapLookup(zip.duplicates_, "DIR/A.XML")->second == 3);
    for (int i = 1; i <= 3; ++i) {
        const std::string key = "DIR/A.XML " + std::to_string(i);
        assert(fileMapLookup(zip.fileMap_, key) != zip.fileMap_.end());
    }
    BW::string resolved = "DIR/A.XML";
    zip.resolvePath(resolved);
    assert(zip.resolveDuplicate(resolved));
    assert(resolved == "dir/a.xml 1");
    assert(zip.eraseFileOrDirectory("DIR\\A.XML"));
    assert(fileMapLookup(zip.fileMap_, "dir/a.xml 3") == zip.fileMap_.end());
    assert(fileMapLookup(zip.duplicates_, "dir/a.xml")->second == 2);
    assert(zip.writeToStructure("Dir/A.Xml", "replacement-third"));
    assert(fileMapLookup(zip.duplicates_, "dir/a.xml")->second == 3);
    zip.assertCanonicalKeys();
    assert(zip.eraseFileOrDirectory("dir/a.xml"));
    assert(zip.eraseFileOrDirectory("dir/a.xml"));
    assert(zip.eraseFileOrDirectory("dir/a.xml"));
    assert(!zip.eraseFileOrDirectory("dir/a.xml"));
    assert(zip.duplicates_.empty());
    assert(zip.writeToStructure("DIR\\A.XML", "reinserted"));
    assert(zip.writeToStructure("DIR\\B.XML", "another"));
    zip.clear("//DIR\\");
    assert(zip.dirMap_.find("dir")->second.first.empty());
    assert(fileMapLookup(zip.fileMap_, "dir/a.xml") == zip.fileMap_.end());
    assert(fileMapLookup(zip.fileMap_, "dir/b.xml") == zip.fileMap_.end());
    zip.assertCanonicalKeys();
    // These APIs remain unsupported; the patch must not invent mutation rules.
    assert(!zip.moveFileOrDirectory("dir", "newdir"));
    assert(!zip.copyFileOrDirectory("dir", "newdir"));
}

static void lookupReport()
{
    ZipFileSystem zip;
    zip.ingest({"Case/Name.model", "case/name.model"});
    const auto first = fileMapLookup(zip.fileMap_, "Case/Name.model");
    const auto second = fileMapLookup(zip.fileMap_, "case/name.model");
    std::cout << first->second.first << " " << second->second.first << "\n";
    StringRefMap<int> many;
    for (int i = 0; i < 16384; ++i) {
        char buffer[80];
        std::snprintf(buffer, sizeof(buffer), "objects/item%05d.model", i);
        BW::string key = buffer;
        cleanSlashes(key);
        many[key] = i;
    }
    lowerComparisons = keyComparisons = 0;
    for (int i = 0; i < 16; ++i) {
        assert(fileMapLookup(many, "objects/item99999.model") == many.end());
        assert(fileMapLookupConst(many, "OBJECTS/ITEM99999.MODEL") == many.end());
    }
    std::cout << lowerComparisons << " " << keyComparisons << "\n";
}

int main(int argc, char** argv)
{
    if (argc > 1 && std::string(argv[1]) == "report") { lookupReport(); return 0; }
    normalisationAndLookup();
    centralCollisionsAndDirectories();
    writesDuplicatesEraseAndClear();
    assert(warningCount == 0);
    std::cout << "ZIP canonical keys, owning maps, central collisions, directories, duplicates and mutations passed\n";
}
