import 'dart:async';
import 'dart:ffi' as ffi;
import 'dart:io';

import 'package:ffi/ffi.dart';

import 'artech_caller_bindings_generated.dart' as bindings;

/// One incoming caller-ID event reported by an AD101/AD102 device.
class ArtechCall {
  const ArtechCall({
    required this.line,
    required this.phoneNumber,
    required this.receivedAt,
  });

  /// Zero-based line index reported by the vendor SDK.
  final int line;

  final String phoneNumber;
  final DateTime receivedAt;
}

/// Listens for incoming caller-ID events from an Artech AD101/AD102 device.
///
/// On Windows, place `artech_bridge_x86.exe` and the vendor's 32-bit
/// `AD101Device.dll` beside the application executable before calling [start].
/// Subscribe to [calls] before starting to avoid missing startup events.
class ArtechCaller {
  ArtechCaller();

  static const int _phoneNumberCapacity = 128;
  static const Duration _pollInterval = Duration(milliseconds: 50);

  final StreamController<ArtechCall> _calls =
      StreamController<ArtechCall>.broadcast();

  Timer? _timer;
  bool _disposed = false;

  /// A broadcast stream of incoming calls.
  Stream<ArtechCall> get calls => _calls.stream;

  bool get isListening => _timer != null;

  /// Number of devices found by the SDK after [start].
  ///
  /// Returns zero if no device was connected when the SDK was initialized.
  int get deviceCount {
    _ensureNotDisposed();
    if (!isListening) {
      throw StateError('Start the Artech listener before reading deviceCount.');
    }
    final count = bindings.artech_device_count();
    if (count < 0) {
      throw _nativeError(bindings.artech_last_error());
    }
    return count;
  }

  /// Initializes the vendor SDK and starts forwarding caller-ID events.
  ///
  /// The SDK currently supports Windows only. The native adapter starts the
  /// 32-bit bridge from the application directory; the bridge loads the
  /// vendor DLL from that same directory.
  Future<void> start() async {
    _ensureNotDisposed();
    if (isListening) return;
    if (!Platform.isWindows) {
      throw UnsupportedError(
        'Artech AD101/AD102 devices are supported on Windows only.',
      );
    }

    if (bindings.artech_start() != 0) {
      throw _nativeError(bindings.artech_last_error());
    }
    _timer = Timer.periodic(_pollInterval, (_) => _drainCalls());
    _drainCalls();
  }

  /// Stops listening. The [calls] stream remains open and can be restarted.
  Future<void> stop() async {
    _timer?.cancel();
    _timer = null;
    bindings.artech_stop();
  }

  /// Stops listening and closes the [calls] stream permanently.
  Future<void> dispose() async {
    if (_disposed) return;
    await stop();
    _disposed = true;
    await _calls.close();
  }

  void _drainCalls() {
    if (!isListening || _calls.isClosed) return;

    final line = calloc<ffi.Int32>();
    final phoneNumber = calloc<ffi.Uint8>(_phoneNumberCapacity);
    try {
      while (true) {
        final result = bindings.artech_next_call(
          line,
          phoneNumber.cast<ffi.Int8>(),
          _phoneNumberCapacity,
        );
        if (result == 0) return;
        if (result < 0) {
          _calls.addError(_nativeError(bindings.artech_last_error()));
          _timer?.cancel();
          _timer = null;
          bindings.artech_stop();
          return;
        }

        final number = phoneNumber.cast<Utf8>().toDartString();
        if (number.isNotEmpty) {
          _calls.add(
            ArtechCall(
              line: line.value,
              phoneNumber: number,
              receivedAt: DateTime.now(),
            ),
          );
        }
      }
    } finally {
      calloc.free(line);
      calloc.free(phoneNumber);
    }
  }

  StateError _nativeError(int code) {
    final description = switch (code) {
      1 => 'The Artech SDK is supported on Windows only.',
      2 =>
        'artech_bridge_x86.exe could not be started (Windows error ${bindings.artech_last_system_error()}). Build the x86 bridge and copy it beside the application executable.',
      3 => _sdkLoadErrorDescription(bindings.artech_last_system_error()),
      4 => 'The Artech device listener is not started.',
      5 => 'The Artech SDK returned an invalid caller-ID value.',
      6 =>
        'The Artech bridge process failed (Windows error ${bindings.artech_last_system_error()}).',
      _ => 'The Artech SDK failed with error code $code.',
    };
    return StateError(description);
  }

  String _sdkLoadErrorDescription(int systemError) {
    final detail = switch (systemError) {
      2 || 3 => 'The SDK DLL or one of its required files could not be found. Check that AD101Device.dll and its dependencies are beside the application executable.',
      5 => 'Windows denied access to the SDK DLL. Check its permissions and whether security software is blocking it.',
      126 => 'Windows could not find AD101Device.dll or one of its dependent DLLs. The SDK file is loaded from beside the application executable, so check the vendor SDK for additional required files.',
      193 => 'The SDK DLL architecture does not match the 32-bit bridge. Use the vendor 32-bit DLL.',
      127 => 'The SDK DLL is missing a required Artech API export. Use the DLL supplied with the Artech SDK.',
      31 => 'The Artech driver or device subsystem did not initialize. Check that the vendor driver is installed and the device is connected.',
      _ => 'Check that AD101Device.dll is beside the application executable and matches its architecture.',
    };
    return 'Could not load AD101Device.dll (Windows error $systemError). $detail';
  }

  void _ensureNotDisposed() {
    if (_disposed) {
      throw StateError('This ArtechCaller instance has been disposed.');
    }
  }
}
