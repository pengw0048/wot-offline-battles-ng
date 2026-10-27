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
enum { StatMV_VM_VMAbcFile_Mem = 1 };

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
struct Value
{
    Value() : text("undefined") {}
    explicit Value(const ASString & value) : text(value.ToCStr()) {}
    std::string text;
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
    enum ErrorID { eClassNotFoundError = 1014 };
    struct Error
    {
        Error(ErrorID id, VM &, const Value & argument) : id(id), argument(argument.text) {}
        ErrorID id;
        std::string argument;
    };
    void ThrowReferenceError(const Error & error)
    {
        assert(exception.empty());
        char text[256];
        std::snprintf(text, sizeof(text), "ReferenceError #%d %s", int(error.id), error.argument.c_str());
        exception = text;
    }
    void RegisterClassAlias(const ASString & alias, Class & cls);
    Class * GetClassByAlias(const ASString & alias) const;
    void RemoveClassAlias(const ASString & alias);
    ASStringHash<Class *> ClassAliases;
    std::string exception;
};

class GlobalObjectCPP
{
public:
    explicit GlobalObjectCPP(VM & vm) : vm_(vm) {}
    VM & GetVM() { return vm_; }
    void getClassByAlias(SPtr<Class> & result, const ASString & aliasName);
    void registerClassAlias(const Value & result, const ASString & aliasName, Class * classObject);
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
    global.registerClassAlias(undefined, S("net.wg.gui.components.controls.VO.ActionPriceVO"), &actionPrice);
    lookup("registered", S("net.wg.gui.components.controls.VO.ActionPriceVO"));
    // SettingsConfigHelper re-registers its aliases on every construction.
    global.registerClassAlias(undefined, S("net.wg.gui.components.controls.VO.ActionPriceVO"), &actionPrice);
    aliases(*lobby);
    global.registerClassAlias(undefined, S("second"), &actionPrice);
    lookup("second-alias", S("second"));
    global.registerClassAlias(undefined, S("tankman"), &tankman);
    global.registerClassAlias(undefined, S("tankman"), &settingsVO);
    lookup("moved", S("tankman"));
    aliases(*lobby);
    aliases(*settings);
    global.registerClassAlias(undefined, S("ignored"), NULL);
    lookup("null-class", S("ignored"));
    global.registerClassAlias(undefined, ASString(&strings.NullStringNode), &tankman);
    lookup("null-alias", ASString(&strings.NullStringNode));
    lookup("text-null", S("null"));
    global.registerClassAlias(undefined, S("builtin"), &object);
    aliases(*lobby);
    delete settings;
    lookup("settings-gone", S("tankman"));
    lookup("settings-gone", S("second"));
    delete lobby;
    lookup("lobby-gone", S("net.wg.gui.components.controls.VO.ActionPriceVO"));
    lookup("lobby-gone", S("second"));
    lookup("lobby-gone", ASString(&strings.NullStringNode));
    lookup("lobby-gone", S("builtin"));
    std::printf("remaining %u\n", unsigned(vm.ClassAliases.values.size()));
    return 0;
}
