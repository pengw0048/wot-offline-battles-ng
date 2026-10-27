// Runs the patched SDK 4.2.22 flash.net class alias bodies over an AS3
// fixture: interned strings, file-owned classes and the VM exception slot.
#include <cassert>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

#define SF_UNUSED1(x) (void)(x)
// The 37b1e207 client log names the missing variable, so GFX_AS3_VERBOSE is on.
#define SF_DEBUG_ARG(x) , x
enum { StatMV_VM_VMAbcFile_Mem = 1, StatMV_VM_VM_Mem = 2 };

struct ASStringNode { std::string text; };
// Strings are interned per manager: equal text is one node.
struct StringManager
{
    ASStringNode NullStringNode = {"null"};
    std::map<std::string, ASStringNode *> nodes;
    ASStringNode * Intern(const std::string & text)
    {
        ASStringNode *& node = nodes[text];
        if (!node) node = new ASStringNode{text};
        return node;
    }
};
class ASString;
StringManager strings;
class ASString
{
public:
    explicit ASString(ASStringNode * node) : pNode(node) {}
    bool operator<(const ASString & other) const { return pNode < other.pNode; }
    bool operator==(const ASString & other) const { return pNode == other.pNode; }
    const char * ToCStr() const { return pNode->text.c_str(); }
    ASStringNode * pNode;
};
ASString S(const char * text) { return ASString(strings.Intern(text)); }
struct ASStringHashFunctor {};
struct FakeStringManager
{
    ASString CreateEmptyString() { return S(""); }
};
template <class K> struct FixedSizeHash {};
template <class K, class V, class HashF, int SID> class HashLH
{
public:
    V * Get(const K & key)
    {
        typename std::map<K, V>::iterator it = values.find(key);
        return it == values.end() ? NULL : &it->second;
    }
    void Set(const K & key, const V & value)
    {
        values.erase(key);
        values.insert(std::make_pair(key, value));
    }
    void Remove(const K & key) { values.erase(key); }
    std::map<K, V> values;
};

template <class U> class ASStringHash
{
public:
    U * Get(const ASString & key)
    {
        typename std::map<ASString, U>::iterator it = values.find(key);
        return it == values.end() ? NULL : &it->second;
    }
    const U * Get(const ASString & key) const
    {
        typename std::map<ASString, U>::const_iterator it = values.find(key);
        return it == values.end() ? NULL : &it->second;
    }
    void Set(const ASString & key, const U & value) { values[key] = value; }
    void Remove(const ASString & key) { values.erase(key); }
    std::map<ASString, U> values;
};
template <class C, class HashF, class AltHashF, int SID> class HashSetUncachedLH
{
public:
    class ConstIterator
    {
    public:
        ConstIterator(typename std::set<C>::const_iterator it, typename std::set<C>::const_iterator end)
            : it_(it), end_(end) {}
        bool IsEnd() const { return it_ == end_; }
        const C & operator*() const { return *it_; }
        ConstIterator & operator++() { ++it_; return *this; }
    private:
        typename std::set<C>::const_iterator it_, end_;
    };
    ConstIterator Begin() const { return ConstIterator(values.begin(), values.end()); }
    void Set(const C & value) { values.insert(value); }
    void Remove(const C & value) { values.erase(value); }
    std::set<C> values;
};

class VM;
class VMAbcFile;
struct Traits
{
    VMAbcFile * file;
    VMAbcFile * GetFilePtr() const { return file; }
};
struct Class
{
    std::string name;
    Traits traits;
    Traits & GetTraits() { return traits; }
};
// Argument kinds the registerClassAlias thunk can pass.
struct Value
{
    Value() : text("undefined"), cls(NULL), null(true) {}
    explicit Value(const ASString & value) : text(value.ToCStr()), cls(NULL), null(false) {}
    explicit Value(Class * value) : text("[class]"), cls(value), null(value == NULL) {}
    static Value Null() { Value v; v.text = "null"; return v; }
    bool IsNullOrUndefined() const { return null; }
    bool IsClass() const { return cls != NULL; }
    Class & AsClass() const { return *cls; }
    std::string text;
    Class * cls;
    bool null;
};
template <class T> class SPtr
{
public:
    SPtr() : p_(NULL) {}
    SPtr & operator=(T * p) { p_ = p; return *this; }
    T * GetPtr() const { return p_; }
private:
    T * p_;
};

class VMFile
{
public:
    VMFile(VM & vm, const char * name) : VMRef(vm), name(name) {}
    ~VMFile();
@FILE_API@
    VM & VMRef;
    std::string name;
    HashSetUncachedLH<ASString, ASStringHashFunctor, ASStringHashFunctor, StatMV_VM_VMAbcFile_Mem> ClassAliases;
};
class VMAbcFile : public VMFile
{
public:
    VMAbcFile(VM & vm, const char * name) : VMFile(vm, name) {}
};

class VM
{
public:
    enum ErrorID { eClassNotFoundError = 1014, eCheckTypeFailedError = 1034 };
    struct Error
    {
        Error(ErrorID id, VM &, const Value & argument) : id(id), argument(argument.text) {}
        Error(ErrorID id, VM &, const Value & argument, const char * type)
            : id(id), argument(argument.text + " to " + type) {}
        ErrorID id;
        std::string argument;
    };
    void Throw(const char * type, const Error & error)
    {
        assert(exception.empty());
        char text[256];
        std::snprintf(text, sizeof(text), "%s #%d %s", type, int(error.id), error.argument.c_str());
        exception = text;
    }
    void ThrowReferenceError(const Error & error) { Throw("ReferenceError", error); }
    void ThrowTypeError(const Error & error) { Throw("TypeError", error); }
    bool IsException() const { return !exception.empty(); }
    void RegisterClassAlias(const ASString & alias, Class & cls);
    Class * GetClassByAlias(const ASString & alias) const;
    ASString GetAliasByClass(Class & cls);
    void RemoveClassAlias(const ASString & alias);
    FakeStringManager & GetStringManager() { return stringManager; }
    FakeStringManager stringManager;
    ASStringHash<Class *> ClassAliases;
    HashLH<Class *, ASString, FixedSizeHash<Class *>, StatMV_VM_VM_Mem> ClassAliasNames;
    std::string exception;
};

class GlobalObjectCPP
{
public:
    explicit GlobalObjectCPP(VM & vm) : vm_(vm) {}
    VM & GetVM() { return vm_; }
    void getClassByAlias(SPtr<Class> & result, const ASString & aliasName);
    void registerClassAlias(const Value & result, const ASString & aliasName, const Value & classObject);
private:
    VM & vm_;
};

VMFile::~VMFile()
{
@FILE_TEARDOWN@
}
@VM_BODIES@
@GLOBAL_BODIES@

VM vm;
GlobalObjectCPP global(vm);

void lookup(const char * label, const ASString & alias)
{
    SPtr<Class> result;
    global.getClassByAlias(result, alias);
    if (!vm.exception.empty())
    {
        std::printf("%s %s -> %s\n", label, alias.ToCStr(), vm.exception.c_str());
        vm.exception.clear();
        return;
    }
    std::printf("%s %s -> %s\n", label, alias.ToCStr(), result.GetPtr()->name.c_str());
}

void aliases(const VMFile & file)
{
    std::set<std::string> names;
    for (std::set<ASString>::const_iterator it = file.ClassAliases.values.begin();
         it != file.ClassAliases.values.end(); ++it)
        names.insert(it->ToCStr());
    std::printf("  %s:", file.name.c_str());
    for (std::set<std::string>::const_iterator it = names.begin(); it != names.end(); ++it)
        std::printf(" %s", it->c_str());
    std::printf("\n");
}

void reverse(const char * label, Class & cls)
{
    std::printf("%s %s <- '%s'\n", label, cls.name.c_str(), vm.GetAliasByClass(cls).ToCStr());
}

int main()
{
    VMAbcFile * lobby = new VMAbcFile(vm, "lobby.swf");
    VMAbcFile * settings = new VMAbcFile(vm, "settings");
    Class actionPrice = {"ActionPriceVO", {lobby}};
    Class tankman = {"TankmanVO", {lobby}};
    Class settingsVO = {"SettingsControlProp", {settings}};
    Class object = {"Object", {NULL}};
    const Value undefined;

    lookup("unknown", S("net.wg.gui.components.controls.VO.ActionPriceVO"));
    global.registerClassAlias(undefined, S("net.wg.gui.components.controls.VO.ActionPriceVO"), Value(&actionPrice));
    lookup("registered", S("net.wg.gui.components.controls.VO.ActionPriceVO"));
    reverse("registered", actionPrice);
    reverse("unaliased", tankman);
    // SettingsConfigHelper re-registers its aliases on every construction.
    global.registerClassAlias(undefined, S("net.wg.gui.components.controls.VO.ActionPriceVO"), Value(&actionPrice));
    aliases(*lobby);
    global.registerClassAlias(undefined, S("second"), Value(&actionPrice));
    lookup("second-alias", S("second"));
    reverse("second-alias", actionPrice);
    global.registerClassAlias(undefined, S("tankman"), Value(&tankman));
    global.registerClassAlias(undefined, S("tankman"), Value(&settingsVO));
    lookup("moved", S("tankman"));
    reverse("moved", tankman);
    reverse("moved", settingsVO);
    aliases(*lobby);
    aliases(*settings);
    global.registerClassAlias(undefined, S("ignored"), Value::Null());
    lookup("null-class", S("ignored"));
    global.registerClassAlias(undefined, S("ignored"), Value());
    lookup("null-class", S("ignored"));
    // SDK 4.2.22's thunk coercion passes a String argument through unchanged.
    global.registerClassAlias(undefined, S("coerced"), Value(S("notAClass")));
    std::printf("non-class -> %s\n", vm.exception.c_str());
    vm.exception.clear();
    lookup("non-class", S("coerced"));
    // An exception left by an earlier argument stops the body.
    vm.exception = "pending";
    global.registerClassAlias(undefined, S("pending"), Value(&tankman));
    vm.exception.clear();
    lookup("pending", S("pending"));
    global.registerClassAlias(undefined, ASString(&strings.NullStringNode), Value(&tankman));
    lookup("null-alias", ASString(&strings.NullStringNode));
    reverse("null-alias", tankman);
    lookup("text-null", S("null"));
    global.registerClassAlias(undefined, S("builtin"), Value(&object));
    aliases(*lobby);
    delete settings;
    lookup("settings-gone", S("tankman"));
    lookup("settings-gone", S("second"));
    reverse("settings-gone", settingsVO);
    reverse("settings-gone", actionPrice);
    delete lobby;
    lookup("lobby-gone", S("net.wg.gui.components.controls.VO.ActionPriceVO"));
    lookup("lobby-gone", S("second"));
    lookup("lobby-gone", ASString(&strings.NullStringNode));
    lookup("lobby-gone", S("builtin"));
    reverse("lobby-gone", actionPrice);
    reverse("lobby-gone", tankman);
    reverse("lobby-gone", object);
    std::printf("remaining %u reverse %u\n", unsigned(vm.ClassAliases.values.size()),
                unsigned(vm.ClassAliasNames.values.size()));
    return 0;
}
