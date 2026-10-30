#include "artech_caller.h"
#include "artech_protocol.h"

#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

enum {
  ARTECH_ERROR_NONE = 0,
  ARTECH_ERROR_UNSUPPORTED_PLATFORM = 1,
  ARTECH_ERROR_BRIDGE_NOT_FOUND = 2,
  ARTECH_ERROR_SDK_LOAD_FAILED = 3,
  ARTECH_ERROR_NOT_STARTED = 4,
  ARTECH_ERROR_INVALID_ARGUMENT = 5,
  ARTECH_ERROR_BRIDGE_FAILED = 6
};

static HANDLE pipe_handle = INVALID_HANDLE_VALUE;
static HANDLE bridge_process;
static int32_t last_error;
static int32_t last_system_error;
static int32_t device_count;
static int started;

static int build_sibling_path(
    const wchar_t *filename,
    wchar_t *path,
    DWORD capacity) {
  HMODULE module = GetModuleHandleW(L"artech_caller.dll");
  DWORD length;
  wchar_t *separator = NULL;
  DWORD index;
  size_t filename_length = wcslen(filename) + 1;

  if (module == NULL) {
    SetLastError(ERROR_MOD_NOT_FOUND);
    return 0;
  }
  length = GetModuleFileNameW(module, path, capacity);
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

static int append_wide(
    wchar_t *buffer,
    size_t capacity,
    size_t *length,
    const wchar_t *value) {
  while (*value != L'\0') {
    if (*length + 1 >= capacity) {
      return 0;
    }
    buffer[(*length)++] = *value++;
  }
  buffer[*length] = L'\0';
  return 1;
}

static int append_process_id(
    wchar_t *buffer,
    size_t capacity,
    size_t *length,
    DWORD process_id) {
  wchar_t digits[10];
  size_t count = 0;
  do {
    digits[count++] = (wchar_t)(L'0' + process_id % 10);
    process_id /= 10;
  } while (process_id != 0 && count < sizeof(digits) / sizeof(digits[0]));
  while (count > 0) {
    wchar_t digit[2] = {digits[--count], L'\0'};
    if (!append_wide(buffer, capacity, length, digit)) {
      return 0;
    }
  }
  return 1;
}

static void close_bridge(int send_stop) {
  if (pipe_handle != INVALID_HANDLE_VALUE) {
    if (send_stop) {
      ArtechPacket packet = {0};
      DWORD written = 0;
      packet.type = ARTECH_PACKET_STOP;
      WriteFile(pipe_handle, &packet, sizeof(packet), &written, NULL);
    }
  }
  if (bridge_process != NULL) {
    if (WaitForSingleObject(bridge_process, 5000) == WAIT_TIMEOUT) {
      TerminateProcess(bridge_process, 1);
      WaitForSingleObject(bridge_process, INFINITE);
    }
    CloseHandle(bridge_process);
    bridge_process = NULL;
  }
  if (pipe_handle != INVALID_HANDLE_VALUE) {
    DisconnectNamedPipe(pipe_handle);
    CloseHandle(pipe_handle);
    pipe_handle = INVALID_HANDLE_VALUE;
  }
  started = 0;
  device_count = 0;
}

static int read_packet(ArtechPacket *packet) {
  DWORD available = 0;
  DWORD read = 0;
  if (!PeekNamedPipe(pipe_handle, NULL, 0, NULL, &available, NULL)) {
    last_system_error = (int32_t)GetLastError();
    last_error = ARTECH_ERROR_BRIDGE_FAILED;
    return -1;
  }
  if (available < sizeof(*packet)) {
    return 0;
  }
  if (!ReadFile(pipe_handle, packet, sizeof(*packet), &read, NULL) ||
      read != sizeof(*packet)) {
    last_system_error = (int32_t)GetLastError();
    last_error = ARTECH_ERROR_BRIDGE_FAILED;
    return -1;
  }
  return 1;
}

static int wait_for_ready(ArtechPacket *packet) {
  ULONGLONG deadline = GetTickCount64() + 10000;
  while (GetTickCount64() < deadline) {
    int result = read_packet(packet);
    if (result != 0) {
      return result;
    }
    if (WaitForSingleObject(bridge_process, 0) == WAIT_OBJECT_0) {
      last_error = ARTECH_ERROR_BRIDGE_FAILED;
      last_system_error = ERROR_PROCESS_ABORTED;
      return -1;
    }
    Sleep(10);
  }
  last_error = ARTECH_ERROR_BRIDGE_FAILED;
  last_system_error = ERROR_TIMEOUT;
  return -1;
}

int32_t artech_start(void) {
  wchar_t bridge_path[32768];
  wchar_t pipe_name[128];
  wchar_t command_line[33024];
  size_t pipe_length = 0;
  size_t command_length = 0;
  STARTUPINFOW startup = {0};
  PROCESS_INFORMATION process = {0};
  ArtechPacket packet = {0};
  DWORD error;

  if (started) {
    last_error = ARTECH_ERROR_NONE;
    last_system_error = ERROR_SUCCESS;
    return 0;
  }

  last_error = ARTECH_ERROR_NONE;
  last_system_error = ERROR_SUCCESS;
  if (!build_sibling_path(L"artech_bridge_x86.exe", bridge_path,
                          (DWORD)(sizeof(bridge_path) /
                                  sizeof(bridge_path[0])))) {
    last_error = ARTECH_ERROR_BRIDGE_NOT_FOUND;
    last_system_error = (int32_t)GetLastError();
    return -1;
  }

  pipe_name[0] = L'\0';
  if (!append_wide(pipe_name, sizeof(pipe_name) / sizeof(pipe_name[0]),
                   &pipe_length, L"\\\\.\\pipe\\ArtechCaller_") ||
      !append_process_id(pipe_name, sizeof(pipe_name) / sizeof(pipe_name[0]),
                         &pipe_length, GetCurrentProcessId())) {
    last_error = ARTECH_ERROR_BRIDGE_FAILED;
    last_system_error = ERROR_INSUFFICIENT_BUFFER;
    return -1;
  }
  pipe_handle = CreateNamedPipeW(
      pipe_name, PIPE_ACCESS_DUPLEX,
      PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 1,
      sizeof(ArtechPacket) * 8, sizeof(ArtechPacket) * 8, 0, NULL);
  if (pipe_handle == INVALID_HANDLE_VALUE) {
    last_error = ARTECH_ERROR_BRIDGE_FAILED;
    last_system_error = (int32_t)GetLastError();
    return -1;
  }

  command_line[0] = L'\0';
  if (!append_wide(command_line,
                   sizeof(command_line) / sizeof(command_line[0]),
                   &command_length, L"\"") ||
      !append_wide(command_line,
                   sizeof(command_line) / sizeof(command_line[0]),
                   &command_length, bridge_path) ||
      !append_wide(command_line,
                   sizeof(command_line) / sizeof(command_line[0]),
                   &command_length, L"\" \"") ||
      !append_wide(command_line,
                   sizeof(command_line) / sizeof(command_line[0]),
                   &command_length, pipe_name) ||
      !append_wide(command_line,
                   sizeof(command_line) / sizeof(command_line[0]),
                   &command_length, L"\"")) {
    last_error = ARTECH_ERROR_BRIDGE_FAILED;
    last_system_error = ERROR_INSUFFICIENT_BUFFER;
    close_bridge(0);
    return -1;
  }
  startup.cb = sizeof(startup);
  if (!CreateProcessW(bridge_path, command_line, NULL, NULL, FALSE,
                      CREATE_NO_WINDOW, NULL, NULL, &startup, &process)) {
    error = GetLastError();
    last_error = ARTECH_ERROR_BRIDGE_NOT_FOUND;
    last_system_error = (int32_t)error;
    close_bridge(0);
    return -1;
  }
  bridge_process = process.hProcess;
  CloseHandle(process.hThread);

  if (!ConnectNamedPipe(pipe_handle, NULL) &&
      GetLastError() != ERROR_PIPE_CONNECTED) {
    error = GetLastError();
    last_error = ARTECH_ERROR_BRIDGE_FAILED;
    last_system_error = (int32_t)error;
    close_bridge(0);
    return -1;
  }

  if (wait_for_ready(&packet) <= 0) {
    int32_t saved_error = last_error;
    int32_t saved_system_error = last_system_error;
    if (saved_error == ARTECH_ERROR_NONE) {
      saved_error = ARTECH_ERROR_BRIDGE_FAILED;
      saved_system_error = ERROR_INVALID_DATA;
    }
    close_bridge(0);
    last_error = saved_error;
    last_system_error = saved_system_error;
    return -1;
  }
  if (packet.type == ARTECH_PACKET_ERROR) {
    int32_t system_error = packet.code;
    close_bridge(0);
    last_error = ARTECH_ERROR_SDK_LOAD_FAILED;
    last_system_error = system_error;
    return -1;
  }
  if (packet.type != ARTECH_PACKET_READY) {
    close_bridge(0);
    last_error = ARTECH_ERROR_BRIDGE_FAILED;
    last_system_error = ERROR_INVALID_DATA;
    return -1;
  }

  device_count = packet.line;
  started = 1;
  return 0;
}

void artech_stop(void) {
  close_bridge(1);
  last_error = ARTECH_ERROR_NONE;
  last_system_error = ERROR_SUCCESS;
}

int32_t artech_device_count(void) {
  if (!started) {
    last_error = ARTECH_ERROR_NOT_STARTED;
    return -1;
  }
  last_error = ARTECH_ERROR_NONE;
  return device_count;
}

int32_t artech_next_call(
    int32_t *line,
    char *phone_number,
    int32_t phone_number_capacity) {
  ArtechPacket packet;
  int result;
  size_t length;

  if (line == NULL || phone_number == NULL || phone_number_capacity <= 0) {
    last_error = ARTECH_ERROR_INVALID_ARGUMENT;
    return -1;
  }
  if (!started) {
    last_error = ARTECH_ERROR_NOT_STARTED;
    return -1;
  }

  result = read_packet(&packet);
  if (result <= 0) {
    return result;
  }
  if (packet.type == ARTECH_PACKET_ERROR) {
    last_error = ARTECH_ERROR_SDK_LOAD_FAILED;
    last_system_error = packet.code;
    return -1;
  }
  if (packet.type != ARTECH_PACKET_CALL) {
    last_error = ARTECH_ERROR_BRIDGE_FAILED;
    last_system_error = ERROR_INVALID_DATA;
    return -1;
  }

  packet.phone_number[ARTECH_PHONE_NUMBER_CAPACITY - 1] = '\0';
  length = strlen(packet.phone_number);
  if (length + 1 > (size_t)phone_number_capacity) {
    last_error = ARTECH_ERROR_INVALID_ARGUMENT;
    return -1;
  }
  *line = packet.line;
  memcpy(phone_number, packet.phone_number, length + 1);
  last_error = ARTECH_ERROR_NONE;
  last_system_error = ERROR_SUCCESS;
  return 1;
}

int32_t artech_last_error(void) { return last_error; }

int32_t artech_last_system_error(void) { return last_system_error; }

#else

enum {
  ARTECH_ERROR_UNSUPPORTED_PLATFORM = 1,
  ARTECH_ERROR_NOT_STARTED = 4
};
static int32_t last_error = ARTECH_ERROR_UNSUPPORTED_PLATFORM;

int32_t artech_start(void) {
  last_error = ARTECH_ERROR_UNSUPPORTED_PLATFORM;
  return -1;
}

void artech_stop(void) {}

int32_t artech_device_count(void) {
  last_error = ARTECH_ERROR_NOT_STARTED;
  return -1;
}

int32_t artech_next_call(int32_t *line, char *phone_number, int32_t capacity) {
  (void)line;
  (void)phone_number;
  (void)capacity;
  last_error = ARTECH_ERROR_UNSUPPORTED_PLATFORM;
  return -1;
}

int32_t artech_last_error(void) { return last_error; }

int32_t artech_last_system_error(void) { return 0; }

#endif
