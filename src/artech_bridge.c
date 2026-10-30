#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "artech_protocol.h"

typedef BOOL(WINAPI *InitDeviceFn)(HWND);
typedef int(WINAPI *GetDeviceFn)(void);
typedef void(WINAPI *FreeDeviceFn)(void);
typedef int(WINAPI *GetDeviceCountFn)(void);
typedef int(WINAPI *GetCallerIdFn)(int, char *, char *, char *);
typedef void(CALLBACK *DeviceEventCallback)(int, int, int);
typedef void(WINAPI *SetEventCallbackFn)(DeviceEventCallback);

enum { MCU_BACKCID = 0x09 };

static HANDLE pipe_handle;
static HMODULE sdk_module;
static FreeDeviceFn sdk_free_device;
static GetCallerIdFn sdk_get_caller_id;
static SetEventCallbackFn sdk_set_event_callback;
static CRITICAL_SECTION pipe_lock;
static int sdk_initialized;
static volatile LONG callbacks_active;

static int send_packet(const ArtechPacket *packet) {
  DWORD written = 0;
  int success;
  EnterCriticalSection(&pipe_lock);
  success = WriteFile(pipe_handle, packet, sizeof(*packet), &written, NULL) &&
            written == sizeof(*packet);
  LeaveCriticalSection(&pipe_lock);
  return success;
}

static void send_error(DWORD error) {
  ArtechPacket packet = {0};
  packet.type = ARTECH_PACKET_ERROR;
  packet.code = (int32_t)error;
  send_packet(&packet);
}

static void CALLBACK on_device_event(int line, int event, int parameter) {
  ArtechPacket packet = {0};
  char name[ARTECH_PHONE_NUMBER_CAPACITY] = {0};
  char time[ARTECH_PHONE_NUMBER_CAPACITY] = {0};
  int length;
  (void)parameter;

  InterlockedIncrement(&callbacks_active);
  if (event != MCU_BACKCID || sdk_get_caller_id == NULL) {
    goto done;
  }

  length = sdk_get_caller_id(line, packet.phone_number, name, time);
  if (length <= 0 || packet.phone_number[0] == '\0') {
    goto done;
  }
  packet.phone_number[ARTECH_PHONE_NUMBER_CAPACITY - 1] = '\0';
  packet.type = ARTECH_PACKET_CALL;
  packet.line = line;
  send_packet(&packet);
done:
  InterlockedDecrement(&callbacks_active);
}

static int build_sdk_path(wchar_t *path, DWORD capacity) {
  DWORD length = GetModuleFileNameW(NULL, path, capacity);
  wchar_t *separator = NULL;
  DWORD index;
  static const wchar_t filename[] = L"AD101Device.dll";
  size_t filename_length = sizeof(filename) / sizeof(filename[0]);

  if (length == 0 || length >= capacity) {
    return 0;
  }
  for (index = 0; index < length; index++) {
    if (path[index] == L'\\' || path[index] == L'/') {
      separator = &path[index + 1];
    }
  }
  if (separator == NULL ||
      (size_t)(path + capacity - separator) < filename_length) {
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return 0;
  }
  memcpy(separator, filename, filename_length * sizeof(wchar_t));
  return 1;
}

static int load_sdk(HWND window) {
  wchar_t path[32768];
  InitDeviceFn init_device;
  GetDeviceFn get_device;
  FreeDeviceFn free_device;
  GetDeviceCountFn get_device_count;
  SetEventCallbackFn set_event_callback;
  BOOL initialized;

  if (!build_sdk_path(path, (DWORD)(sizeof(path) / sizeof(path[0])))) {
    send_error(GetLastError());
    return 0;
  }
  sdk_module = LoadLibraryExW(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
  if (sdk_module == NULL) {
    send_error(GetLastError());
    return 0;
  }

  init_device = (InitDeviceFn)GetProcAddress(sdk_module, "AD101_InitDevice");
  get_device = (GetDeviceFn)GetProcAddress(sdk_module, "AD101_GetDevice");
  free_device = (FreeDeviceFn)GetProcAddress(sdk_module, "AD101_FreeDevice");
  get_device_count =
      (GetDeviceCountFn)GetProcAddress(sdk_module, "AD101_GetCurDevCount");
  sdk_get_caller_id =
      (GetCallerIdFn)GetProcAddress(sdk_module, "AD101_GetCallerID");
  set_event_callback =
      (SetEventCallbackFn)GetProcAddress(sdk_module,
                                         "AD101_SetEventCallbackFun");
  if (init_device == NULL || get_device == NULL || free_device == NULL ||
      get_device_count == NULL || sdk_get_caller_id == NULL ||
      set_event_callback == NULL) {
    send_error(ERROR_PROC_NOT_FOUND);
    return 0;
  }

  sdk_set_event_callback = set_event_callback;
  sdk_free_device = free_device;
  sdk_set_event_callback(on_device_event);
  initialized = init_device(window);
  if (!initialized) {
    send_error(ERROR_GEN_FAILURE);
    return 0;
  }
  sdk_initialized = 1;

  get_device();
  {
    ArtechPacket packet = {0};
    packet.type = ARTECH_PACKET_READY;
    packet.line = get_device_count();
    return send_packet(&packet);
  }
}

static int pipe_has_packet(void) {
  DWORD available = 0;
  if (!PeekNamedPipe(pipe_handle, NULL, 0, NULL, &available, NULL)) {
    return -1;
  }
  return available >= sizeof(ArtechPacket);
}

static int stop_requested(void) {
  ArtechPacket packet;
  DWORD read = 0;
  int available = pipe_has_packet();
  if (available < 0) {
    return 1;
  }
  if (available == 0) {
    return 0;
  }
  if (!ReadFile(pipe_handle, &packet, sizeof(packet), &read, NULL) ||
      read != sizeof(packet)) {
    return 1;
  }
  return packet.type == ARTECH_PACKET_STOP;
}

static void pump_messages(void) {
  MSG message;
  while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
    if (message.message == WM_QUIT) {
      PostQuitMessage((int)message.wParam);
      return;
    }
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
}

static void release_sdk(void) {
  if (sdk_set_event_callback != NULL) {
    sdk_set_event_callback(NULL);
    sdk_set_event_callback = NULL;
  }
  if (sdk_initialized && sdk_free_device != NULL) {
    sdk_free_device();
  }
  sdk_initialized = 0;
  while (InterlockedCompareExchange(&callbacks_active, 0, 0) != 0) {
    Sleep(1);
  }
  sdk_free_device = NULL;
  if (sdk_module != NULL) {
    FreeLibrary(sdk_module);
    sdk_module = NULL;
  }
}

int wmain(int argc, wchar_t **argv) {
  DWORD mode = PIPE_READMODE_MESSAGE;
  WNDCLASSW window_class = {0};
  HWND window;

  if (argc != 2) {
    return 2;
  }
  pipe_handle = CreateFileW(argv[1], GENERIC_READ | GENERIC_WRITE, 0, NULL,
                            OPEN_EXISTING, 0, NULL);
  if (pipe_handle == INVALID_HANDLE_VALUE) {
    return 3;
  }
  if (!SetNamedPipeHandleState(pipe_handle, &mode, NULL, NULL)) {
    CloseHandle(pipe_handle);
    return 4;
  }

  InitializeCriticalSection(&pipe_lock);
  window_class.lpfnWndProc = DefWindowProcW;
  window_class.hInstance = GetModuleHandleW(NULL);
  window_class.lpszClassName = L"ArtechCallerBridgeWindow";
  RegisterClassW(&window_class);
  window = CreateWindowExW(0, window_class.lpszClassName, L"", 0, 0, 0, 0, 0,
                           HWND_MESSAGE, NULL, window_class.hInstance, NULL);

  if (window == NULL || !load_sdk(window)) {
    if (window != NULL) {
      DestroyWindow(window);
    }
    release_sdk();
    DeleteCriticalSection(&pipe_lock);
    CloseHandle(pipe_handle);
    return 5;
  }

  while (!stop_requested()) {
    pump_messages();
    MsgWaitForMultipleObjectsEx(0, NULL, 50, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
  }

  release_sdk();
  DestroyWindow(window);
  UnregisterClassW(window_class.lpszClassName, window_class.hInstance);
  DeleteCriticalSection(&pipe_lock);
  CloseHandle(pipe_handle);
  return 0;
}
