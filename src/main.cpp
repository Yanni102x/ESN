#include "app.h"

#include <commctrl.h>
#include <ole2.h>

int WINAPI wWinMain(HINSTANCE hinst, HINSTANCE, PWSTR, int) {
  HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\ESN.Singleton");
  if (!mutex) return 1;
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    const UINT msg = RegisterWindowMessageW(L"ESN.Activate");
    HWND existing = FindWindowW(App::kMainClass, nullptr);
    if (existing && msg) PostMessageW(existing, msg, 0, 0);
    CloseHandle(mutex);
    return 0;
  }

  const BOOL dpi_ok = SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  logf(L"[main] dpi aware=%d err=%lu (manifest ignored if 1)", dpi_ok, GetLastError());

  const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES | ICC_DATE_CLASSES};
  InitCommonControlsEx(&icc);

  int code = 0;
  App& app = App::get();
  if (!app.init(hinst)) {
    MessageBoxW(nullptr, L"ESN 启动失败。", L"ESN", MB_OK | MB_ICONERROR);
    code = 1;
  } else {
    code = app.run();
  }

  if (SUCCEEDED(com)) CoUninitialize();
  CloseHandle(mutex);
  return code;
}
