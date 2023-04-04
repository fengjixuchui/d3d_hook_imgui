#include "dx12_hook.hpp"

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "detours.lib")

DWORD WINAPI CreateConsole() {
  AllocConsole();
  AttachConsole(GetCurrentProcessId());
  freopen_s(reinterpret_cast<FILE**>(stdin), "CONIN$", "r", stdin);
  freopen_s(reinterpret_cast<FILE**>(stdout), "CONOUT$", "w", stdout);
  freopen_s(reinterpret_cast<FILE**>(stderr), "CONOUT$", "w", stderr);
  DetourRestoreAfterWith();
  return 0;
}

DWORD WINAPI AttachThread(LPVOID lParam) {
    if (D3d12::Init() == TRUE ){
       D3d12::InstallHooks();
   }
  return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call,
                      LPVOID lpReserved) {
  switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH: {
#ifndef NDEBUG
      CreateConsole();
#endif
      DisableThreadLibraryCalls(hModule);
      CreateThread(nullptr, 0, &AttachThread, static_cast<LPVOID>(hModule), 0,
                   nullptr);
      break;
    }
    case DLL_PROCESS_DETACH: {
     D3d12::RemoveHooks();
      break;
    }
  }
  return TRUE;
}
