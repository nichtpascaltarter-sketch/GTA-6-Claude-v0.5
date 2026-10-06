#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <stdio.h>
typedef HRESULT (WINAPI *PFN_D3DCompile)(LPCVOID,SIZE_T,LPCSTR,const D3D_SHADER_MACRO*,ID3DInclude*,LPCSTR,LPCSTR,UINT,UINT,ID3DBlob**,ID3DBlob**);
static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l){ if(m==WM_DESTROY){PostQuitMessage(0);return 0;} return DefWindowProcA(h,m,w,l);}
int main(){
  HMODULE dc = LoadLibraryA("d3dcompiler_47.dll"); printf("d3dcompiler_47: %p\n", dc);
  PFN_D3DCompile D3DCompileF = dc ? (PFN_D3DCompile)GetProcAddress(dc,"D3DCompile") : 0;
  WNDCLASSA wc={0}; wc.lpfnWndProc=WndProc; wc.hInstance=GetModuleHandle(0); wc.lpszClassName="t";
  RegisterClassA(&wc);
  HWND hwnd=CreateWindowA("t","t",WS_OVERLAPPEDWINDOW,0,0,640,360,0,0,wc.hInstance,0);
  ShowWindow(hwnd,SW_SHOW);
  DXGI_SWAP_CHAIN_DESC sd={0}; sd.BufferCount=2; sd.BufferDesc.Width=640; sd.BufferDesc.Height=360; sd.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.OutputWindow=hwnd; sd.SampleDesc.Count=1; sd.Windowed=TRUE; sd.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
  ID3D11Device* dev=0; ID3D11DeviceContext* ctx=0; IDXGISwapChain* sc=0; D3D_FEATURE_LEVEL fl;
  D3D_FEATURE_LEVEL fls[]={D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};
  HRESULT hr=D3D11CreateDeviceAndSwapChain(0,D3D_DRIVER_TYPE_HARDWARE,0,0,fls,2,D3D11_SDK_VERSION,&sd,&sc,&dev,&fl,&ctx);
  printf("create hr=%08lx fl=%x\n",hr,fl);
  if(FAILED(hr)) return 1;
  const char* src="float4 vs(uint id:SV_VertexID):SV_Position{float2 p=float2(id&2,(id<<1)&2); return float4(p*2-1,0,1);} float4 ps(float4 p:SV_Position):SV_Target{return float4(p.x/640,p.y/360,0.5,1);} [numthreads(8,8,1)] void cs(uint3 id:SV_DispatchThreadID){}";
  ID3DBlob *vb=0,*pb=0,*cb=0,*err=0;
  hr=D3DCompileF(src,strlen(src),0,0,0,"vs","vs_5_0",0,0,&vb,&err); printf("vs hr=%08lx\n",hr);
  hr=D3DCompileF(src,strlen(src),0,0,0,"ps","ps_5_0",0,0,&pb,&err); printf("ps hr=%08lx\n",hr);
  hr=D3DCompileF(src,strlen(src),0,0,0,"cs","cs_5_0",0,0,&cb,&err); printf("cs hr=%08lx\n",hr);
  ID3D11VertexShader* vs; ID3D11PixelShader* ps; ID3D11ComputeShader* cs;
  dev->CreateVertexShader(vb->GetBufferPointer(),vb->GetBufferSize(),0,&vs);
  dev->CreatePixelShader(pb->GetBufferPointer(),pb->GetBufferSize(),0,&ps);
  hr=dev->CreateComputeShader(cb->GetBufferPointer(),cb->GetBufferSize(),0,&cs); printf("create cs hr=%08lx\n",hr);
  ID3D11Texture2D* bb; sc->GetBuffer(0,__uuidof(ID3D11Texture2D),(void**)&bb);
  ID3D11RenderTargetView* rtv; dev->CreateRenderTargetView(bb,0,&rtv);
  D3D11_VIEWPORT vp={0,0,640,360,0,1};
  ctx->OMSetRenderTargets(1,&rtv,0); ctx->RSSetViewports(1,&vp);
  ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  ctx->VSSetShader(vs,0,0); ctx->PSSetShader(ps,0,0); ctx->Draw(3,0);
  D3D11_TEXTURE2D_DESC td; bb->GetDesc(&td); td.Usage=D3D11_USAGE_STAGING; td.BindFlags=0; td.CPUAccessFlags=D3D11_CPU_ACCESS_READ; td.MiscFlags=0;
  ID3D11Texture2D* st; dev->CreateTexture2D(&td,0,&st); ctx->CopyResource(st,bb);
  D3D11_MAPPED_SUBRESOURCE ms; hr=ctx->Map(st,0,D3D11_MAP_READ,0,&ms); printf("map hr=%08lx\n",hr);
  unsigned char* p=(unsigned char*)ms.pData; printf("pixel(320,180)=%d %d %d\n",p[180*ms.RowPitch+320*4],p[180*ms.RowPitch+320*4+1],p[180*ms.RowPitch+320*4+2]);
  sc->Present(1,0);
  return 0;
}
