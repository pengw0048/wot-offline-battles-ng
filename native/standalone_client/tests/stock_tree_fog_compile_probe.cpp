// Compile complete pinned public billboard effects through the same D3DX9
// effect compiler as EffectCompiler, including both actual pipeline branches.
// Requires Windows, June 2010 DirectX headers and the matching d3dx9_43.dll.
#include <windows.h>
#include <d3dx9.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using CreateCompiler = HRESULT (WINAPI *)(LPCSTR, UINT, const D3DXMACRO*,
    ID3DXInclude*, DWORD, ID3DXEffectCompiler**, ID3DXBuffer**);

class Includes : public ID3DXInclude {
public:
    Includes(fs::path root, fs::path current):root_(std::move(root)),current_(std::move(current)){}
    HRESULT WINAPI Open(D3DXINCLUDE_TYPE, LPCSTR name, LPCVOID,
                        LPCVOID* data, UINT* bytes) override {
        *data=nullptr;*bytes=0;
        // SDK EffectIncludes resolves resource IDs first, then the main FX
        // directory and resources.xml system/shaderIncludePaths in order.
        const fs::path filename=fs::path(name).filename();
        for(const auto& path:{root_/name,current_/filename,
                root_/"shaders/std_effects"/filename,root_/"shaders/terrain"/filename}) {
            std::ifstream stream(path,std::ios::binary);
            if(!stream)continue;
            auto contents=std::make_unique<std::vector<char>>(
                std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>());
            if(contents->empty())return E_FAIL;
            *data=contents->data();*bytes=static_cast<UINT>(contents->size());
            open_[*data]=std::move(contents);return S_OK;
        }
        std::cerr<<"Include missing: "<<name<<"\n";return E_FAIL;
    }
    HRESULT WINAPI Close(LPCVOID data) override {open_.erase(data);return S_OK;}
private:
    fs::path root_,current_;
    std::map<LPCVOID,std::unique_ptr<std::vector<char>>> open_;
};

static void diagnostics(ID3DXBuffer* messages) {
    if(messages){std::cout<<static_cast<const char*>(messages->GetBufferPointer())<<"\n";messages->Release();}
}

int wmain(int argc,wchar_t**argv) {
    if(argc!=2){std::cerr<<"Usage: stock_tree_fog_compile_probe.exe RESOURCE_ROOT\n";return 2;}
    wchar_t configured[MAX_PATH];
    DWORD length=GetEnvironmentVariableW(L"WOT_D3DX9_DLL",configured,MAX_PATH);
    if(length>=MAX_PATH){std::cerr<<"WOT_D3DX9_DLL is too long\n";return 2;}
    HMODULE module=LoadLibraryW(length?configured:L"d3dx9_43.dll");
    if(!module){std::cerr<<"Cannot load d3dx9_43.dll: "<<GetLastError()<<"\n";return 2;}
    auto create=reinterpret_cast<CreateCompiler>(GetProcAddress(module,"D3DXCreateEffectCompiler"));
    if(!create){std::cerr<<"D3DXCreateEffectCompiler export missing\n";return 2;}
    const fs::path root=argv[1];
    unsigned compiled=0,failed=0;
    for(const char* name:{"billboards.fx","billboards_opt.fx"}) {
        const fs::path sourcePath=root/"shaders/speedtree"/name;
        std::ifstream stream(sourcePath,std::ios::binary);
        if(!stream){std::cerr<<"Missing source: "<<sourcePath<<"\n";return 2;}
        const std::string source((std::istreambuf_iterator<char>(stream)),std::istreambuf_iterator<char>());
        for(unsigned forward=0;forward<2;++forward) {
            // #1513 00941355/009414a8 define DEFERRED="1", FORWARD="0".
            // The FXO suffix uses option indices 0/1, not these macro values.
            const char* deferredValue=forward?"0":"1";
            const D3DXMACRO macros[]={
                {"WG_DEFERRED_SHADING",deferredValue},
                {"BW_DEFERRED_SHADING",deferredValue},
                {"SHADER_DEBUG","0"},{"IN_GAME","1"},{nullptr,nullptr}};
            Includes includes(root,sourcePath.parent_path());
            ID3DXEffectCompiler* compiler=nullptr;
            ID3DXBuffer* errors=nullptr;
            std::cout<<name<<" pipeline="<<(forward?"forward":"deferred")
                <<" WG_DEFERRED_SHADING="<<deferredValue
                <<" BW_DEFERRED_SHADING="<<deferredValue<<"\n";
            HRESULT result=create(source.data(),static_cast<UINT>(source.size()),macros,
                &includes,D3DXSHADER_USE_LEGACY_D3DX9_31_DLL,&compiler,&errors);
            diagnostics(errors);errors=nullptr;
            if(FAILED(result)) {
                if(compiler){compiler->Release();compiler=nullptr;}
                result=create(source.data(),static_cast<UINT>(source.size()),macros,&includes,0,&compiler,&errors);
                diagnostics(errors);errors=nullptr;
            }
            ID3DXBuffer* effect=nullptr;
            if(SUCCEEDED(result)) {
                result=compiler->CompileEffect(0,&effect,&errors);
                diagnostics(errors);
            }
            if(FAILED(result)){++failed;std::cout<<"FAIL hr="<<std::hex<<result<<std::dec<<"\n";}
            else {++compiled;std::cout<<"PASS bytes="<<effect->GetBufferSize()<<"\n";}
            if(effect)effect->Release();
            if(compiler)compiler->Release();
        }
    }
    FreeLibrary(module);
    if(failed)return 1;
    std::cout<<compiled<<" complete billboard/pipeline effects compiled\n";
    return compiled==4?0:1;
}
