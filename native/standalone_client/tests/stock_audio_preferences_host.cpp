// The production helper and SDK DataResource methods are inserted below.
// XML parsing and OS file I/O are explicit fault-injectable boundaries.
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace BW { using string = std::string; }
static int errors = 0, createdFiles = 0, fullSaves = 0, resolves = 0;
static bool failLoad = false, failSave = false, nullRoot = false;
static std::vector<std::string> trace;
#define BW_GUARD
#define TRACE_MSG(...) ((void)0)
#define WARNING_MSG(...) ((void)0)
#define ERROR_MSG(...) (++errors)
#define bw_safe_delete(p) do { delete (p); (p) = nullptr; } while (0)

struct RefCounted {
    mutable int references = 0;
    virtual ~RefCounted() = default;
};
template<class T> class Ptr {
    T* value_;
    void retain() { if (value_) ++value_->references; }
    void release() { if (value_ && --value_->references == 0) delete value_; }
public:
    Ptr(T* value = nullptr) : value_(value) { retain(); }
    Ptr(const Ptr& other) : value_(other.value_) { retain(); }
    ~Ptr() { release(); }
    Ptr& operator=(T* value) { Ptr copy(value); std::swap(value_, copy.value_); return *this; }
    Ptr& operator=(const Ptr& other) { return operator=(other.value_); }
    T* operator->() const { return value_; }
    T* getObject() const { return value_; }
    bool exists() const { return value_ != nullptr; }
    explicit operator bool() const { return exists(); }
};
struct DataSection;
using DataSectionPtr = Ptr<DataSection>;
static DataSectionPtr s_scriptsPreferences;

struct DataSection : RefCounted {
    std::string value;
    std::vector<std::pair<std::string, DataSectionPtr>> children;
    DataSectionPtr newSection(const std::string& name) {
        DataSectionPtr child = new DataSection;
        children.emplace_back(name, child);
        return child;
    }
    DataSectionPtr openSection(const std::string& path, bool create = false) {
        const auto slash = path.find('/');
        const std::string name = path.substr(0, slash);
        DataSectionPtr child;
        for (const auto& entry : children) if (entry.first == name) { child = entry.second; break; }
        if (!child && create) child = newSection(name);
        if (!child || slash == std::string::npos) return child;
        return child->openSection(path.substr(slash + 1), create);
    }
    void writeString(const std::string& path, const std::string& text) {
        openSection(path, true)->value = text;
    }
    void writeInt(const std::string& path, int number) { writeString(path, std::to_string(number)); }
    void writeUInt(const std::string& path, unsigned number) {
        const bool live = this == s_scriptsPreferences.getObject();
        trace.push_back(std::string(live ? "live-write:" : "disk-write:") +
            (live ? "scriptsPreferences/" : "") + path + ":" + std::to_string(number));
        writeString(path, std::to_string(number));
    }
    void delChildren() { children.clear(); }
};
static DataSectionPtr copySection(const DataSectionPtr& input) {
    if (!input) return {};
    DataSectionPtr result = new DataSection;
    result->value = input->value;
    for (const auto& entry : input->children)
        result->children.emplace_back(entry.first, copySection(entry.second));
    return result;
}
static std::string snapshot(const DataSectionPtr& root) {
    if (!root) return "<absent>";
    std::string out = std::to_string(root->value.size()) + ":" + root->value;
    for (const auto& entry : root->children)
        out += "[" + entry.first + "]" + snapshot(entry.second);
    return out + ";";
}
static std::map<std::string, DataSectionPtr> files;
static std::string currentPath;
struct PathedFilename {
    BW::string resolveName() const { ++resolves; trace.push_back("resolve:" + currentPath); return currentPath; }
};
struct DeviceApp {
    PathedFilename preferencesFilename_;
    static DeviceApp instance;
    BW::string preferencesFilePath() const;
};
DeviceApp DeviceApp::instance;
@PATH_OWNER@

enum DataResourceType { RESOURCE_TYPE_XML };
struct DataHandle : RefCounted {
    enum Error { DHE_NoError, DHE_LoadFailed, DHE_SaveFailed };
    virtual DataSectionPtr getRootSection() = 0;
    virtual Error save(const BW::string&) = 0;
};
struct XMLHandle : DataHandle {
    std::string path;
    DataSectionPtr root;
    bool good;
    explicit XMLHandle(const BW::string& filename, bool create = false) : path(filename), good(false) {
        trace.push_back("load:" + path);
        const auto found = files.find(path);
        good = !failLoad && found != files.end();
        if (good && !nullRoot) root = copySection(found->second);
        if (create) { ++createdFiles; good = true; root = new DataSection; }
    }
    bool isGood() const { return good; }
    DataSectionPtr getRootSection() override { return root; }
    Error save(const BW::string& filename) override {
        if (!filename.empty()) throw std::runtime_error("Unexpected save-as path");
        trace.push_back("save:" + path);
        if (failSave) return DHE_SaveFailed;
        files[path] = copySection(root);
        return DHE_NoError;
    }
};
struct DataResource {
    Ptr<DataHandle> pResource_;
    DataResource();
    DataSectionPtr getRootSection();
    DataHandle::Error load(const BW::string&, DataResourceType = RESOURCE_TYPE_XML, bool = false);
    DataHandle::Error save(const BW::string& = "");
};
@DATA_RESOURCE@

// The negative control reaches the real patched SDK graphics writer through
// the old full-save call. At early audio startup only nine owners exist.
namespace Moo {
struct UniqueID { std::string toString() const { return "fixture-device"; } };
struct DeviceInfo { struct { UniqueID DeviceIdentifier; } identifier_; };
struct RC { const DeviceInfo& deviceInfo(int) { static DeviceInfo info; return info; } };
static RC& rc() { static RC context; return context; }
struct GraphicsSetting : RefCounted {
    using GraphicsSettingPtr = Ptr<GraphicsSetting>;
    using GraphicsSettingVector = std::vector<GraphicsSettingPtr>;
    std::string label_;
    int selected;
    GraphicsSetting(std::string label, int value) : label_(std::move(label)), selected(value) {}
    const std::string& label() const { return label_; }
    int activeOption() const { return selected; }
    bool stockImportant() const { return true; }
    static GraphicsSettingVector registered;
    static const GraphicsSettingVector& settings() { return registered; }
    static void write(DataSectionPtr);
};
GraphicsSetting::GraphicsSettingVector GraphicsSetting::registered;
static void writeStockGraphicsPreferencesMetadata(DataSectionPtr section) {
    section->writeInt("graphicsSettingsVersion", 4);
}
@GRAPHICS_WRITER@
}
struct App {
    static App& instance() { static App app; return app; }
    bool savePreferences() {
        ++fullSaves;
        auto root = copySection(files.at(currentPath));
        Moo::GraphicsSetting::write(root->openSection("graphicsPreferences", true));
        files[currentPath] = root;
        return true;
    }
};
@LEGACY_HELPER@
@PRODUCTION_HELPER@

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static DataSectionPtr seed(bool live = true, unsigned registered = 9) {
    trace.clear(); errors = createdFiles = fullSaves = resolves = 0;
    failLoad = failSave = nullRoot = false;
    currentPath = "C:/player settings/偏好.xml";
    files.clear();
    DataSectionPtr root = new DataSection;
    auto graphics = root->newSection("graphicsPreferences");
    graphics->writeInt("graphicsSettingsVersion", 4);
    graphics->writeInt("graphicsSettingsVersionMinor", 2);
    graphics->writeInt("graphicsSettingsVersionMaintainance", 2);
    static const char* labels[] = {"DECALS_QUALITY", "DECOR_LEVEL", "DEVICE_EX", "DRR_AUTOSCALER_ENABLED",
        "EFFECTS_QUALITY", "FAR_PLANE", "FLORA_QUALITY", "FOOT_PRINTS", "FXAA_PROCESSING",
        "LIGHTING_QUALITY", "MOTION_BLUR_QUALITY", "OBJECT_LOD", "POST_PROCESSING_QUALITY",
        "PS_USE_PERFORMANCER", "RENDER_PIPELINE", "SEMITRANSPARENT_LEAVES_ENABLED", "SHADER_DEBUG",
        "SHADER_VERSION_CAP", "SHADOWS_QUALITY", "SMOKE_ENABLED", "SNIPER_MODE_EFFECTS_QUALITY",
        "SNIPER_MODE_GRASS_ENABLED", "SNIPER_MODE_SWINGING_ENABLED", "SPEEDTREE_QUALITY",
        "TERRAIN_QUALITY", "TEXTURE_QUALITY", "VEHICLE_DUST_ENABLED", "VEHICLE_TRACES_ENABLED", "WATER_QUALITY"};
    Moo::GraphicsSetting::registered.clear();
    for (unsigned i = 0; i != 29; ++i) {
        auto entry = graphics->newSection("entry");
        entry->writeString("label", labels[i]); entry->writeInt("activeOption", int(i % 4));
        if (i < registered) Moo::GraphicsSetting::registered.emplace_back(new Moo::GraphicsSetting(labels[i], int(i % 4)));
    }
    root->writeString("devicePreferences/windowed", "true");
    root->writeString("devicePreferences/waitVSync", "false");
    root->writeInt("devicePreferences/windowedWidth", 1014);
    root->writeInt("devicePreferences/windowedHeight", 736);
    root->writeString("scriptsPreferences/custom/untouched", "preserve me");
    root->writeInt("scriptsPreferences/soundPrefs/speakerConfig", 2);
    root->writeInt("scriptsPreferences/soundPrefs/detectedSpeakerConfig", 5);
    root->newSection("extension")->value = "first";
    root->newSection("extension")->value = "second";
    files[currentPath] = root;
    s_scriptsPreferences = live ? copySection(root->openSection("scriptsPreferences")) : DataSectionPtr();
    return root;
}
static int run(bool legacy) {
    const char* keys[] = {"soundPrefs/detectedSpeakerConfig", "soundPrefs/speakerConfig"};
    for (const char* key : keys) for (unsigned registered : {9u, 29u}) for (unsigned value : {0u, 2u, 5u, 7u, UINT32_MAX}) {
        auto original = seed(true, registered), expected = copySection(original);
        expected->writeString(std::string("scriptsPreferences/") + key, std::to_string(value));
        if (legacy) legacySaveSpeakerPreference(key, value); else saveSpeakerPreference(key, value);
        require(snapshot(files.at(currentPath)->openSection("graphicsPreferences")) ==
            snapshot(original->openSection("graphicsPreferences")), "unregistered graphics settings were lost");
        require(snapshot(files.at(currentPath)) == snapshot(expected), "unrelated XML data changed");
        require(s_scriptsPreferences->openSection(key)->value == std::to_string(value), "live value was not updated");
        require(fullSaves == 0 && createdFiles == 0 && errors == 0 && resolves == 1, "wrong persistence owner");
        const std::vector<std::string> expectedTrace = {
            std::string("live-write:scriptsPreferences/") + key + ":" + std::to_string(value),
            "resolve:" + currentPath, "load:" + currentPath,
            std::string("disk-write:scriptsPreferences/") + key + ":" + std::to_string(value), "save:" + currentPath};
        require(trace == expectedTrace, "speaker write ordering changed");
    }
    for (const char* key : keys) {
        // Missing, unreadable and invalid XML all fail the real load method.
        for (unsigned failure = 0; failure != 4; ++failure) {
            auto original = seed();
            if (failure == 0) files.clear();
            else if (failure <= 2) failLoad = true;
            else nullRoot = true;
            saveSpeakerPreference(key, 7);
            require(s_scriptsPreferences->openSection(key)->value == "7", "load failure blocked live update");
            require(createdFiles == 0 && fullSaves == 0 && errors == 0, "load failure created or rewrote preferences");
            require(trace.size() == 3, "failed load reached the save boundary");
            if (failure == 0) require(files.empty(), "missing preferences were created");
            else require(snapshot(files.at(currentPath)) == snapshot(original), "failed load changed existing preferences");
        }
        auto original = seed(); failSave = true;
        saveSpeakerPreference(key, 7);
        require(snapshot(files.at(currentPath)) == snapshot(original), "failed write replaced existing preferences");
        require(s_scriptsPreferences->openSection(key)->value == "7" && errors == 1, "failed write lost live state or error");
        require(trace.size() == 5 && createdFiles == 0, "failed write retried through another owner");
        original = seed(false);
        saveSpeakerPreference(key, 7);
        require(files.at(currentPath)->openSection(std::string("scriptsPreferences/") + key)->value == "7",
            "absent live preferences blocked the original file update");
        require(trace.size() == 4 && trace.front() == "resolve:" + currentPath, "absent live update was fabricated");
    }
    seed(); const std::string first = currentPath;
    saveSpeakerPreference(keys[0], 7);
    auto firstSnapshot = snapshot(files.at(first));
    currentPath = "D:/another preferences.xml";
    files[currentPath] = copySection(files.at(first));
    saveSpeakerPreference(keys[1], 5);
    require(resolves == 2 && snapshot(files.at(first)) == firstSnapshot, "dynamic filename was cached or another profile changed");
    s_scriptsPreferences = nullptr; files.clear(); Moo::GraphicsSetting::registered.clear();
    return 0;
}
int main(int argc, char**) {
    try { return run(argc > 1); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
