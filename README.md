# artech_caller

Flutter/Dart FFI package for receiving incoming caller-ID events from Artech
AD101 and AD102 devices using the vendor AD101 Windows SDK.

The vendor SDK is Windows-only. This package does not implement a guessed USB
or serial protocol; it uses the SDK's `AD101_SetEventCallbackFun` callback and
reads caller IDs with `AD101_GetCallerID` when the SDK reports event `0x09`
(`MCU_BACKCID`).

## Windows setup

1. Install the Artech device driver/software required by the device.
2. Build the 32-bit SDK bridge with a Visual Studio C++ toolchain:

   ```powershell
   cmake -S bridge -B build\bridge-x86 -A Win32
   cmake --build build\bridge-x86 --config Release
   ```

3. Copy `build\bridge-x86\Release\artech_bridge_x86.exe` and the vendor's
   32-bit `AD101Device.dll` beside the generated application `.exe`. For the
   example's default Debug build, this is usually
   `example\build\windows\x64\runner\Debug\`.
4. Connect and power the device, connect the telephone line, then start the
   application. Click **Connect**; the example shows the detected device count
   and adds each incoming caller number to the list.

The DLL is not bundled in this repository. Use the vendor-supplied DLL and
follow its licensing and redistribution terms. Flutter's Windows app is x64;
the helper runs as a separate x86 process so it can load the 32-bit Artech DLL.
The helper and DLL are both loaded from the application directory. On startup,
Windows loader errors are reported explicitly (for example, error 126 means a
DLL or one of its dependent files could not be found).

## Dart API

Subscribe before starting the listener:

```dart
import 'package:artech_caller/artech_caller.dart';

final caller = ArtechCaller();
final subscription = caller.calls.listen(
  (call) => print('Line ${call.line + 1}: ${call.phoneNumber}'),
  onError: (Object error) => print('Artech listener error: $error'),
);

await caller.start();
print('Devices found: ${caller.deviceCount}');

// When finished:
await caller.dispose();
await subscription.cancel();
```

`ArtechCall.line` is the zero-based line index from the vendor SDK,
`phoneNumber` is the received caller number, and `receivedAt` is the local
receipt time. Calls are delivered through a broadcast stream; the native SDK
callback is forwarded through the local bridge and polled every 50 ms.

## Use from another Flutter project

Add the GitHub repository as a dependency in the consuming app's
`pubspec.yaml`:

```yaml
dependencies:
  artech_caller:
    git:
      url: https://github.com/basit-h/Artech-AD-102.git
      ref: main
```

Then fetch the dependency:

```powershell
flutter pub get
```

Import it with `package:artech_caller/artech_caller.dart` and follow the
[Windows setup](#windows-setup) to provide the bridge executable and
vendor-supplied DLL beside the consuming application's executable. The SDK
DLL is not included in this repository.

## Run the example

From the package root, build and run the Windows sample:

```powershell
cd example
flutter pub get
flutter run -d windows
```

Complete the Windows setup above before launching the app so both the bridge
and vendor DLL are beside `artech_caller_example.exe`. An actual incoming call
and a connected Artech device are required to verify caller-ID delivery.

## Native bindings

The x64 C wrapper in `src/artech_caller.c` communicates with the x86
`src/artech_bridge.c` helper over a local named pipe. The helper loads the
vendor SDK and forwards caller-ID events to Dart. Regenerate the FFI bindings
after changing `src/artech_caller.h`:

```powershell
dart run ffigen --config ffigen.yaml
```
