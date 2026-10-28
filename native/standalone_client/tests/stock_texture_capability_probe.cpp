// Run against the original RGB24 DDS with a real D3D9 device. The managed
// loader uses this D3DX conversion when direct streaming is unsupported.
#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <cstdio>
#include <fstream>
#include <vector>
#include <iterator>
int main(int argc,char** argv){
 if(argc!=2)return 1;
 std::ifstream file(argv[1],std::ios::binary);std::vector<char> data((std::istreambuf_iterator<char>(file)),{});if(data.empty())return 2;
 HWND window=CreateWindowExA(0,"STATIC","Texture capability probe",WS_POPUP,0,0,64,64,NULL,NULL,GetModuleHandle(NULL),NULL);if(!window)return 3;
 IDirect3D9* api=Direct3DCreate9(D3D_SDK_VERSION);if(!api)return 4;
 D3DDISPLAYMODE mode={};api->GetAdapterDisplayMode(0,&mode);
 D3DPRESENT_PARAMETERS pp={};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=mode.Format;pp.hDeviceWindow=window;
 IDirect3DDevice9* device=NULL;HRESULT hr=api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&device);if(FAILED(hr)){std::printf("device=%08lx\n",(unsigned long)hr);return 5;}
 HRESULT capability=api->CheckDeviceFormat(0,D3DDEVTYPE_HAL,mode.Format,0,D3DRTYPE_TEXTURE,D3DFMT_R8G8B8);
 IDirect3DTexture9* direct=NULL;hr=device->CreateTexture(32,32,6,0,D3DFMT_R8G8B8,D3DPOOL_MANAGED,&direct,NULL);
 std::printf("r8g8b8_capability=%08lx direct=%08lx\n",(unsigned long)capability,(unsigned long)hr);if(direct)direct->Release();
 IDirect3DTexture9* texture=NULL;hr=D3DXCreateTextureFromFileInMemoryEx(device,data.data(),(UINT)data.size(),D3DX_DEFAULT,D3DX_DEFAULT,D3DX_DEFAULT,0,D3DFMT_R8G8B8,D3DPOOL_MANAGED,D3DX_FILTER_BOX|D3DX_FILTER_MIRROR,D3DX_FILTER_BOX|D3DX_FILTER_MIRROR,0,NULL,NULL,&texture);
 std::printf("d3dx=%08lx\n",(unsigned long)hr);if(FAILED(hr))return 6;
 D3DSURFACE_DESC desc={};texture->GetLevelDesc(0,&desc);std::printf("actual_format=%u width=%u height=%u mips=%u\n",unsigned(desc.Format),desc.Width,desc.Height,texture->GetLevelCount());
 D3DLOCKED_RECT locked={};hr=texture->LockRect(0,&locked,NULL,D3DLOCK_READONLY);if(FAILED(hr))return 7;
 unsigned expectedWidth=*reinterpret_cast<const unsigned*>(&data[16]);unsigned expectedHeight=*reinterpret_cast<const unsigned*>(&data[12]);
 unsigned mismatches=0; if(desc.Format!=D3DFMT_X8R8G8B8&&desc.Format!=D3DFMT_A8R8G8B8)return 8;
 if(desc.Width!=expectedWidth||desc.Height!=expectedHeight||texture->GetLevelCount()!=*reinterpret_cast<const unsigned*>(&data[28]))return 9;
 for(unsigned y=0;y<desc.Height;y++)for(unsigned x=0;x<desc.Width;x++)for(unsigned c=0;c<3;c++){
 unsigned char actual=*((unsigned char*)locked.pBits+y*locked.Pitch+x*4+c);
 unsigned char expected=(unsigned char)data[128+(y*desc.Width+x)*3+c];if(actual!=expected)++mismatches;
 }
 texture->UnlockRect(0);texture->Release();device->Release();api->Release();DestroyWindow(window);
 std::printf("rgb_mismatches=%u\n",mismatches);return mismatches?10:0;
}
