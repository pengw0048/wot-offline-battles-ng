// Exercise the actual Win32 registry APIs through process-private test hives.
#ifndef WOT_REPLAY_CONTROLLER_HOST_BUILD
#define WOT_REPLAY_CONTROLLER_HOST_BUILD
#endif
#include "stock_replay_controller.hpp"
#include <windows.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
LSTATUS deleteTree(HKEY root,const wchar_t* name){
    // The engine targets an older SDK floor; this fixture runs on Windows 11.
    using DeleteTree=LSTATUS(WINAPI*)(HKEY,LPCWSTR);
    static auto function=reinterpret_cast<DeleteTree>(GetProcAddress(GetModuleHandleW(L"advapi32.dll"),"RegDeleteTreeW"));
    require(function!=NULL,"Private-hive cleanup API is unavailable");return function(root,name);
}
struct Hive {
    HKEY original=NULL,root=NULL,user=NULL,classes=NULL;
    std::wstring name;
    bool redirected=false;
    Hive(){
        name=L"Software\\WoTIndependent\\ReplayProbe-"+std::to_wstring(GetCurrentProcessId());
        require(RegOpenCurrentUser(KEY_ALL_ACCESS,&original)==ERROR_SUCCESS,"Cannot open original test registry root");
        require(RegCreateKeyExW(original,name.c_str(),0,NULL,0,KEY_ALL_ACCESS,NULL,&root,NULL)==ERROR_SUCCESS,"Cannot create private registry test owner");
        require(RegCreateKeyExW(root,L"User",0,NULL,0,KEY_ALL_ACCESS,NULL,&user,NULL)==ERROR_SUCCESS,"Cannot create private user hive");
        require(RegCreateKeyExW(root,L"Classes",0,NULL,0,KEY_ALL_ACCESS,NULL,&classes,NULL)==ERROR_SUCCESS,"Cannot create private merged-classes hive");
        require(RegOverridePredefKey(HKEY_CURRENT_USER,user)==ERROR_SUCCESS,"Cannot redirect process-local user registry");
        require(RegOverridePredefKey(HKEY_CLASSES_ROOT,classes)==ERROR_SUCCESS,"Cannot redirect process-local classes registry");
        redirected=true;
    }
    ~Hive(){
        if(redirected){RegOverridePredefKey(HKEY_CLASSES_ROOT,NULL);RegOverridePredefKey(HKEY_CURRENT_USER,NULL);}
        if(user)RegCloseKey(user);if(classes)RegCloseKey(classes);if(root)RegCloseKey(root);
        if(original){deleteTree(original,name.c_str());RegCloseKey(original);}
    }
};
void put(HKEY root,const wchar_t* path,const wchar_t* name,const std::wstring& value){
    HKEY key=NULL;require(RegCreateKeyExW(root,path,0,NULL,0,KEY_ALL_ACCESS,NULL,&key,NULL)==ERROR_SUCCESS,"Cannot create test value key");
    const auto result=RegSetValueExW(key,name,0,REG_SZ,reinterpret_cast<const BYTE*>(value.c_str()),static_cast<DWORD>((value.size()+1)*2));
    RegCloseKey(key);require(result==ERROR_SUCCESS,"Cannot write test registry value");
}
std::wstring get(HKEY root,const wchar_t* path,const wchar_t* name=NULL){
    HKEY key=NULL;if(RegOpenKeyExW(root,path,0,KEY_QUERY_VALUE,&key)!=ERROR_SUCCESS)return {};
    wchar_t value[2048]{};DWORD type=0,size=sizeof(value);const auto result=RegQueryValueExW(key,name,NULL,&type,reinterpret_cast<BYTE*>(value),&size);RegCloseKey(key);
    require(result==ERROR_SUCCESS||result==ERROR_FILE_NOT_FOUND,"Cannot inspect actual registry output");
    return result==ERROR_FILE_NOT_FOUND?std::wstring():std::wstring(value);
}
void remove(HKEY root,const wchar_t* path,const wchar_t* name=NULL){
    HKEY key=NULL;if(RegOpenKeyExW(root,path,0,KEY_SET_VALUE,&key)!=ERROR_SUCCESS)return;
    const auto result=RegDeleteValueW(key,name);RegCloseKey(key);require(result==ERROR_SUCCESS||result==ERROR_FILE_NOT_FOUND,"Cannot reset test association value");
}
}
int main(){
    try{
        Hive hive;std::uint32_t error=0;
        const std::wstring image=L"C:\\Native Test\\\u5766\u514b\\WorldOfTanksStandalone.exe";
        const wchar_t* extension=L"Software\\Classes\\.wotreplay";
        const wchar_t* command=L"Software\\Classes\\WoTIndependent.Replay.0922\\shell\\open\\command";
        require(wot::stockRegisterReplayExtension(image,error)&&error==0,"Actual registry registration failed");
        require(get(HKEY_CURRENT_USER,extension)==L"WoTIndependent.Replay.0922","Unclaimed extension did not acquire the independent owner");
        require(get(HKEY_CURRENT_USER,command)==L"\""+image+L"\" \"%1\"","Current Unicode image or quoted replay argument changed");
        require(get(HKEY_CURRENT_USER,L"Software\\Classes\\WoTIndependent.Replay.0922\\DefaultIcon")==L"\""+image+L"\",0","Independent icon did not use the current image");
        HKEY openWith=NULL;require(RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\Classes\\.wotreplay\\OpenWithProgids",0,KEY_QUERY_VALUE,&openWith)==ERROR_SUCCESS,"Missing actual Open With registration");
        DWORD type=0,size=0;require(RegQueryValueExW(openWith,L"WoTIndependent.Replay.0922",NULL,&type,NULL,&size)==ERROR_SUCCESS&&type==REG_NONE&&size==0,"Open With entry has the wrong native registry type");RegCloseKey(openWith);
        put(HKEY_CURRENT_USER,extension,NULL,L"Existing.Player");
        put(HKEY_CURRENT_USER,L"Software\\Classes\\.wotreplay\\shell\\open\\command",NULL,L"existing.exe");
        require(wot::stockRegisterReplayExtension(L"C:/Next Client/client.exe",error),"Relocated independent image could not update its owner");
        require(get(HKEY_CURRENT_USER,extension)==L"Existing.Player"&&get(HKEY_CURRENT_USER,L"Software\\Classes\\.wotreplay\\shell\\open\\command")==L"existing.exe","Registration replaced an existing user player");
        require(get(HKEY_CURRENT_USER,command)==L"\"C:\\Next Client\\client.exe\" \"%1\"","Independent relocation used a stale executable");
        remove(HKEY_CURRENT_USER,extension);put(HKEY_CLASSES_ROOT,L".wotreplay",NULL,L"Machine.Player");
        require(wot::stockRegisterReplayExtension(image,error)&&get(HKEY_CURRENT_USER,extension).empty(),"Registration replaced a merged machine association");
        remove(HKEY_CLASSES_ROOT,L".wotreplay");
        const wchar_t* choice=L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\.wotreplay\\UserChoice";
        put(HKEY_CURRENT_USER,choice,L"ProgId",L"Protected.Player");
        require(wot::stockRegisterReplayExtension(image,error)&&get(HKEY_CURRENT_USER,extension).empty()&&get(HKEY_CURRENT_USER,choice,L"ProgId")==L"Protected.Player","Registration modified Windows UserChoice");
        require(!wot::stockRegisterReplayExtension(L"",error)&&error==ERROR_INVALID_PARAMETER,"Empty image path was accepted");
        require(!wot::stockRegisterReplayExtension(L"C:\\bad\"image.exe",error)&&error==ERROR_INVALID_PARAMETER,"Quote injection was accepted");
        require(deleteTree(hive.root,L"User")==ERROR_SUCCESS,"Cannot create actual deleted-key failure boundary");
        require(!wot::stockRegisterReplayExtension(image,error)&&error!=ERROR_SUCCESS,"A failed actual registry write reported success");
        std::cout<<"Actual Win32 replay registration passed: Unicode image, OpenWith, relocation, preserved user/machine/UserChoice defaults, real error; process-private hives removed"<<std::endl;
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<std::endl;return 1;}
}
