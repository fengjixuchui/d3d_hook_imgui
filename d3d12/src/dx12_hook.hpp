#pragma once

#define ______________________________D3D12_NAMESPACE_BEGIN______________________________ \
  namespace D3d12 {

#define ______________________________D3D12_NAMESPACE_END______________________________ \
  }

#include <Windows.h>
#include <atlbase.h>
#include <d3d12.h>
#include <detours/src/detours.h>
#include <dxgi1_4.h>
#include <imgui/backends/imgui_impl_dx12.h>
#include <imgui/backends/imgui_impl_win32.h>
#include <imgui/imgui.h>
#include <string.h>

#include <chrono>
#include <fstream>
#include <thread>
#include <utility>

______________________________D3D12_NAMESPACE_BEGIN______________________________

const wchar_t* WINDOW_NAME = L"Hooked D3d12 Window";

using Present = long(__fastcall*)(IDXGISwapChain3* _SwapChain,
                                  UINT _SyncInterval, UINT _Flags);

using ExecuteCommandLists =
    void(__fastcall*)(ID3D12CommandQueue* queue, UINT NumCommandLists,
                      ID3D12CommandList* ppCommandLists);

using ResizeBuffers = long(__fastcall*)(IDXGISwapChain3* _SwapChain,
                                        UINT BufferCount, UINT Width,
                                        UINT Height, DXGI_FORMAT NewFormat,
                                        UINT SwapChainFlags);
Present OriginalPresent_PTR;
ExecuteCommandLists OriginalExecuteCommandLists_PTR;
ResizeBuffers OriginalResizeBuffers_PTR;

struct FrameContext {
  CComPtr<ID3D12CommandAllocator> CommandAllocator = NULL;
  CComPtr<ID3D12Resource> RenderTargetResource = NULL;
  D3D12_CPU_DESCRIPTOR_HANDLE RenderTargetDescriptor{};
};

std::vector<FrameContext> FrameContext_VEC{};
uint32_t FrameBufferCount = 0;

CComPtr<ID3D12DescriptorHeap> D3DRtvDescHeap_PTR = NULL;
CComPtr<ID3D12DescriptorHeap> D3DSrvDescHeap_PTR = NULL;
CComPtr<ID3D12CommandQueue> D3DCommandQueue_PTR = NULL;
CComPtr<ID3D12GraphicsCommandList> D3DCommandList_PTR = NULL;

WNDPROC OriginalWndProc = nullptr;
HWND TargetWindow = nullptr;

UINT64* MethodsTable_PTR = NULL;
bool Initialized = false;

LRESULT CALLBACK WndProc(HWND _Hwnd, UINT _Message, WPARAM _Wparam,
                         LPARAM _Lparam) {
  if (Initialized) {
    ImGuiIO& io = ImGui::GetIO();
  }
  return CallWindowProc(OriginalWndProc, _Hwnd, _Message, _Wparam, _Lparam);
}

long __fastcall NewPresent(IDXGISwapChain3* _SwapChain, UINT _SyncInterval,
                           UINT _Flags) {
  if (D3DCommandQueue_PTR == nullptr) {
    return OriginalPresent_PTR(_SwapChain, _SyncInterval, _Flags);
  }
  if (!Initialized) {
    ID3D12Device* _D3DDevice = nullptr;

    if (FAILED(_SwapChain->GetDevice(__uuidof(ID3D12Device),
                                     (void**)&_D3DDevice))) {
      return OriginalPresent_PTR(_SwapChain, _SyncInterval, _Flags);
    }

    {
      DXGI_SWAP_CHAIN_DESC _SwapChainDesc;
      _SwapChain->GetDesc(&_SwapChainDesc);
      TargetWindow = _SwapChainDesc.OutputWindow;
      if (!OriginalWndProc) {
        OriginalWndProc = (WNDPROC)SetWindowLongPtr(
            TargetWindow, GWLP_WNDPROC, (__int3264)(LONG_PTR)WndProc);
      }
      FrameBufferCount = _SwapChainDesc.BufferCount;
      FrameContext_VEC.clear();
      FrameContext_VEC.resize(FrameBufferCount);
    }

    {
      D3D12_DESCRIPTOR_HEAP_DESC _HeapDesc = {};
      _HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
      _HeapDesc.NumDescriptors = FrameBufferCount;
      _HeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

      if (_D3DDevice->CreateDescriptorHeap(
              &_HeapDesc, IID_PPV_ARGS(&D3DSrvDescHeap_PTR)) != S_OK) {
        return OriginalPresent_PTR(_SwapChain, _SyncInterval, _Flags);
      }
    }

    {
      D3D12_DESCRIPTOR_HEAP_DESC _HeapDesc{};
      _HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
      _HeapDesc.NumDescriptors = FrameBufferCount;
      _HeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
      _HeapDesc.NodeMask = 1;

      if (_D3DDevice->CreateDescriptorHeap(
              &_HeapDesc, IID_PPV_ARGS(&D3DRtvDescHeap_PTR)) != S_OK) {
        return OriginalPresent_PTR(_SwapChain, _SyncInterval, _Flags);
      }

      const auto _RtvDescriptorSize =
          _D3DDevice->GetDescriptorHandleIncrementSize(
              D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
      D3D12_CPU_DESCRIPTOR_HANDLE _RtvHandle =
          D3DRtvDescHeap_PTR->GetCPUDescriptorHandleForHeapStart();

      for (uint8_t i = 0; i < FrameBufferCount; i++) {
        FrameContext_VEC[i].RenderTargetDescriptor = _RtvHandle;
        _SwapChain->GetBuffer(
            i, IID_PPV_ARGS(&FrameContext_VEC[i].RenderTargetResource));
        _D3DDevice->CreateRenderTargetView(
            FrameContext_VEC[i].RenderTargetResource, nullptr, _RtvHandle);
        _RtvHandle.ptr += _RtvDescriptorSize;
      }
    }

    {
      ID3D12CommandAllocator* _Allocator;
      if (_D3DDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                             IID_PPV_ARGS(&_Allocator)) !=
          S_OK) {
        return OriginalPresent_PTR(_SwapChain, _SyncInterval, _Flags);
      }

      for (size_t i = 0; i < FrameBufferCount; i++) {
        if (_D3DDevice->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(&FrameContext_VEC[i].CommandAllocator)) != S_OK) {
          return OriginalPresent_PTR(_SwapChain, _SyncInterval, _Flags);
        }
      }

      if (_D3DDevice->CreateCommandList(
              0, D3D12_COMMAND_LIST_TYPE_DIRECT,
              FrameContext_VEC[0].CommandAllocator, NULL,
              IID_PPV_ARGS(&D3DCommandList_PTR)) != S_OK ||
          D3DCommandList_PTR->Close() != S_OK) {
        return OriginalPresent_PTR(_SwapChain, _SyncInterval, _Flags);
      }
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(TargetWindow);
    ImGui_ImplDX12_Init(
        _D3DDevice, FrameBufferCount, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3DSrvDescHeap_PTR,
        D3DSrvDescHeap_PTR->GetCPUDescriptorHandleForHeapStart(),
        D3DSrvDescHeap_PTR->GetGPUDescriptorHandleForHeapStart());

    Initialized = true;

    _D3DDevice->Release();
  }

  ImGui_ImplWin32_NewFrame();
  ImGui_ImplDX12_NewFrame();
  ImGui::NewFrame();

  ImGui::ShowDemoWindow();

  FrameContext& _CurrentFrameContext =
      FrameContext_VEC[_SwapChain->GetCurrentBackBufferIndex()];
  _CurrentFrameContext.CommandAllocator->Reset();

  D3D12_RESOURCE_BARRIER _Barrier{};
  _Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  _Barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  _Barrier.Transition.pResource = _CurrentFrameContext.RenderTargetResource;
  _Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  _Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
  _Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
  D3DCommandList_PTR->Reset(_CurrentFrameContext.CommandAllocator, nullptr);
  D3DCommandList_PTR->ResourceBarrier(1, &_Barrier);
  D3DCommandList_PTR->OMSetRenderTargets(
      1, &_CurrentFrameContext.RenderTargetDescriptor, FALSE, nullptr);
  D3DCommandList_PTR->SetDescriptorHeaps(1, &D3DSrvDescHeap_PTR);
  ImGui::Render();
  ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), D3DCommandList_PTR);
  _Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
  _Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
  D3DCommandList_PTR->ResourceBarrier(1, &_Barrier);
  D3DCommandList_PTR->Close();

  D3DCommandQueue_PTR->ExecuteCommandLists(
      1, (ID3D12CommandList**)&D3DCommandList_PTR);
  return OriginalPresent_PTR(_SwapChain, _SyncInterval, _Flags);
}

void NewExecuteCommandLists(ID3D12CommandQueue* _Queue, UINT _NumCommandLists,
                            ID3D12CommandList* _CommandLists) {
  if (!D3DCommandQueue_PTR &&
      _Queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
    D3DCommandQueue_PTR = _Queue;
  }

  OriginalExecuteCommandLists_PTR(_Queue, _NumCommandLists, _CommandLists);
}

void ResetState() {
  if (Initialized) {
    Initialized = false;
    ImGui_ImplWin32_Shutdown();
    ImGui_ImplDX12_Shutdown();
  }
  D3DCommandQueue_PTR = nullptr;
  FrameContext_VEC.clear();
  D3DCommandList_PTR = nullptr;
  D3DRtvDescHeap_PTR = nullptr;
  D3DSrvDescHeap_PTR = nullptr;
}

long NewResizeBuffers(IDXGISwapChain3* _SwapChain, UINT _BufferCount,
                      UINT _Width, UINT _Height, DXGI_FORMAT _NewFormat,
                      UINT _SwapChainFlags) {
  ResetState();
  return OriginalResizeBuffers_PTR(_SwapChain, _BufferCount, _Width, _Height,
                                   _NewFormat, _SwapChainFlags);
}

int Init() {
  WNDCLASSEX _WindowClass;
  _WindowClass.cbSize = sizeof(WNDCLASSEX);
  _WindowClass.style = CS_HREDRAW | CS_VREDRAW;
  _WindowClass.lpfnWndProc = DefWindowProc;
  _WindowClass.cbClsExtra = 0;
  _WindowClass.cbWndExtra = 0;
  _WindowClass.hInstance = GetModuleHandle(NULL);
  _WindowClass.hIcon = NULL;
  _WindowClass.hCursor = NULL;
  _WindowClass.hbrBackground = NULL;
  _WindowClass.lpszMenuName = NULL;
  _WindowClass.lpszClassName = WINDOW_NAME;
  _WindowClass.hIconSm = NULL;

  ::RegisterClassEx(&_WindowClass);

  HWND window =
      ::CreateWindow(_WindowClass.lpszClassName, L"Fake DirectX TargetWindow",
                     WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL,
                     _WindowClass.hInstance, NULL);

  HMODULE libDXGI;
  HMODULE libD3D12;

  if ((libDXGI = ::GetModuleHandle(L"dxgi.dll")) == NULL) {
    ::DestroyWindow(window);
    ::UnregisterClass(_WindowClass.lpszClassName, _WindowClass.hInstance);
    return FALSE;
  }

  if ((libD3D12 = ::GetModuleHandle(L"d3d12.dll")) == NULL) {
    ::DestroyWindow(window);
    ::UnregisterClass(_WindowClass.lpszClassName, _WindowClass.hInstance);
    return FALSE;
  }

  void* CreateDXGIFactory;
  if ((CreateDXGIFactory = ::GetProcAddress(libDXGI, "CreateDXGIFactory")) ==
      NULL) {
    ::DestroyWindow(window);
    ::UnregisterClass(_WindowClass.lpszClassName, _WindowClass.hInstance);
    return FALSE;
  }

  CComPtr<IDXGIFactory> factory;
  if (((long(__stdcall*)(const IID&, void**))(CreateDXGIFactory))(
          __uuidof(IDXGIFactory), (void**)&factory) < 0) {
    ::DestroyWindow(window);
    ::UnregisterClass(_WindowClass.lpszClassName, _WindowClass.hInstance);
    return FALSE;
  }

  CComPtr<IDXGIAdapter> adapter;
  if (factory->EnumAdapters(0, &adapter) == DXGI_ERROR_NOT_FOUND) {
    ::DestroyWindow(window);
    ::UnregisterClass(_WindowClass.lpszClassName, _WindowClass.hInstance);
    return FALSE;
  }

  void* D3D12CreateDevice;
  if ((D3D12CreateDevice = ::GetProcAddress(libD3D12, "D3D12CreateDevice")) ==
      NULL) {
    ::DestroyWindow(window);
    ::UnregisterClass(_WindowClass.lpszClassName, _WindowClass.hInstance);
    return FALSE;
  }

  CComPtr<ID3D12Device> device;
  if (((long(__stdcall*)(IUnknown*, D3D_FEATURE_LEVEL, const IID&, void**))(
          D3D12CreateDevice))(adapter, D3D_FEATURE_LEVEL_11_0,
                              __uuidof(ID3D12Device), (void**)&device) < 0) {
    ::DestroyWindow(window);
    ::UnregisterClass(_WindowClass.lpszClassName, _WindowClass.hInstance);
    return FALSE;
  }

  D3D12_COMMAND_QUEUE_DESC queueDesc;
  queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  queueDesc.Priority = 0;
  queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  queueDesc.NodeMask = 0;

  CComPtr<ID3D12CommandQueue> commandQueue;
  if (device->CreateCommandQueue(&queueDesc, __uuidof(ID3D12CommandQueue),
                                 (void**)&commandQueue) < 0) {
    ::DestroyWindow(window);
    ::UnregisterClass(_WindowClass.lpszClassName, _WindowClass.hInstance);
    return FALSE;
  }

  CComPtr<ID3D12CommandAllocator> commandAllocator;
  if (device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                     __uuidof(ID3D12CommandAllocator),
                                     (void**)&commandAllocator) < 0) {
    ::DestroyWindow(window);
    ::UnregisterClass(_WindowClass.lpszClassName, _WindowClass.hInstance);
    return FALSE;
  }

  CComPtr<ID3D12GraphicsCommandList> commandList;
  if (device->CreateCommandList(
          0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator, NULL,
          __uuidof(ID3D12GraphicsCommandList), (void**)&commandList) < 0) {
    ::DestroyWindow(window);
    ::UnregisterClass(_WindowClass.lpszClassName, _WindowClass.hInstance);
    return FALSE;
  }

  DXGI_RATIONAL _RefreshRate;
  _RefreshRate.Numerator = 60;
  _RefreshRate.Denominator = 1;

  DXGI_MODE_DESC _BufferDesc;
  _BufferDesc.Width = 100;
  _BufferDesc.Height = 100;
  _BufferDesc.RefreshRate = _RefreshRate;
  _BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  _BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
  _BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;

  DXGI_SAMPLE_DESC _SampleDesc;
  _SampleDesc.Count = 1;
  _SampleDesc.Quality = 0;

  DXGI_SWAP_CHAIN_DESC _SwapChainDesc = {};
  _SwapChainDesc.BufferDesc = _BufferDesc;
  _SwapChainDesc.SampleDesc = _SampleDesc;
  _SwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  _SwapChainDesc.BufferCount = 2;
  _SwapChainDesc.OutputWindow = window;
  _SwapChainDesc.Windowed = 1;
  _SwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  _SwapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

  CComPtr<IDXGISwapChain> swapChain;
  if (factory->CreateSwapChain(commandQueue, &_SwapChainDesc, &swapChain) < 0) {
    ::DestroyWindow(window);
    ::UnregisterClass(_WindowClass.lpszClassName, _WindowClass.hInstance);
    return FALSE;
  }

  MethodsTable_PTR = (uint64_t*)::calloc(150, sizeof(uint64_t));
  if (MethodsTable_PTR == 0) {
    return FALSE;
  }
  ::memcpy(MethodsTable_PTR, *(uint64_t**)(void*)device, 44 * sizeof(uint64_t));
  ::memcpy(MethodsTable_PTR + 44, *(uint64_t**)(void*)commandQueue,
           19 * sizeof(uint64_t));
  ::memcpy(MethodsTable_PTR + 44 + 19, *(uint64_t**)(void*)commandAllocator,
           9 * sizeof(uint64_t));
  ::memcpy(MethodsTable_PTR + 44 + 19 + 9, *(uint64_t**)(void*)commandList,
           60 * sizeof(uint64_t));
  ::memcpy(MethodsTable_PTR + 44 + 19 + 9 + 60, *(uint64_t**)(void*)swapChain,
           18 * sizeof(uint64_t));

  ::DestroyWindow(window);
  ::UnregisterClass(_WindowClass.lpszClassName, _WindowClass.hInstance);
  return TRUE;
}

int Hook(uint16_t _index, void** _original, void* _function) {
  *_original = (void*)MethodsTable_PTR[_index];
  DetourTransactionBegin();
  DetourUpdateThread(GetCurrentThread());
  DetourAttach(&(PVOID&)*_original, _function);
  DetourTransactionCommit();

  return TRUE;
}

int Unhook(uint16_t _index, void** _original, void* _function) {
  void* target = (void*)MethodsTable_PTR[_index];

  DetourTransactionBegin();
  DetourUpdateThread(GetCurrentThread());
  DetourDetach(&(PVOID&)*_original, _function);
  DetourTransactionCommit();

  return TRUE;
}

int InstallHooks() {
  DetourRestoreAfterWith();

  Hook(54, (void**)&OriginalExecuteCommandLists_PTR, NewExecuteCommandLists);
  Hook(140, (void**)&OriginalPresent_PTR, NewPresent);
  Hook(145, (void**)&OriginalResizeBuffers_PTR, NewResizeBuffers);

  return TRUE;
}

static int RemoveHooks() {
  Unhook(54, (void**)&OriginalExecuteCommandLists_PTR, NewExecuteCommandLists);
  Unhook(140, (void**)&OriginalPresent_PTR, NewPresent);
  Unhook(145, (void**)&OriginalResizeBuffers_PTR, NewResizeBuffers);

  if (TargetWindow && OriginalWndProc) {
    SetWindowLongPtr(TargetWindow, GWLP_WNDPROC,
                     (__int64)(LONG_PTR)OriginalWndProc);
  }

  ResetState();
  ImGui::DestroyContext();

  // wait for hooks to finish if in one. maybe not needed, but seemed more
  // stable after adding it.
  Sleep(1000);
  return TRUE;
}
______________________________D3D12_NAMESPACE_END______________________________
