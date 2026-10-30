import 'dart:async';

import 'package:artech_caller/artech_caller.dart';
import 'package:flutter/material.dart';

void main() {
  runApp(const ArtechCallerExample());
}

class ArtechCallerExample extends StatelessWidget {
  const ArtechCallerExample({super.key});

  @override
  Widget build(BuildContext context) => MaterialApp(
    title: 'Artech Caller ID',
    theme: ThemeData(colorSchemeSeed: Colors.blue, useMaterial3: true),
    home: const CallerPage(),
  );
}

class CallerPage extends StatefulWidget {
  const CallerPage({super.key});

  @override
  State<CallerPage> createState() => _CallerPageState();
}

class _CallerPageState extends State<CallerPage> {
  final ArtechCaller _caller = ArtechCaller();
  final List<ArtechCall> _receivedCalls = <ArtechCall>[];
  StreamSubscription<ArtechCall>? _subscription;
  String? _status;
  int? _deviceCount;
  bool _connecting = false;

  @override
  void initState() {
    super.initState();
    _subscription = _caller.calls.listen(
      (call) {
        if (!mounted) return;
        setState(() {
          _receivedCalls.insert(0, call);
          _status = 'Incoming call received.';
        });
      },
      onError: (Object error) {
        if (!mounted) return;
        setState(() => _status = error.toString());
      },
    );
  }

  Future<void> _toggleListener() async {
    setState(() => _connecting = true);
    try {
      if (_caller.isListening) {
        await _caller.stop();
        if (mounted) {
          setState(() {
            _status = 'Listener stopped.';
            _deviceCount = null;
          });
        }
      } else {
        await _caller.start();
        final count = _caller.deviceCount;
        if (mounted) {
          setState(() {
            _deviceCount = count;
            _status = count == 0
                ? 'SDK started, but no Artech devices were found.'
                : 'Listening for incoming calls.';
          });
        }
      }
    } catch (error) {
      if (mounted) setState(() => _status = error.toString());
    } finally {
      if (mounted) setState(() => _connecting = false);
    }
  }

  @override
  void dispose() {
    unawaited(_subscription?.cancel());
    unawaited(_caller.dispose());
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => Scaffold(
    appBar: AppBar(title: const Text('Artech AD101 / AD102 Caller ID')),
    body: Padding(
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: <Widget>[
          FilledButton.icon(
            onPressed: _connecting ? null : _toggleListener,
            icon: Icon(_caller.isListening ? Icons.stop : Icons.phone_in_talk),
            label: Text(_caller.isListening ? 'Stop listener' : 'Connect'),
          ),
          const SizedBox(height: 12),
          if (_status != null) SelectableText(_status!),
          if (_deviceCount != null) Text('Devices detected: $_deviceCount'),
          const Divider(height: 32),
          Text('Incoming calls', style: Theme.of(context).textTheme.titleLarge),
          const SizedBox(height: 8),
          Expanded(
            child: _receivedCalls.isEmpty
                ? const Center(
                    child: Text('Connect the device and wait for a call.'),
                  )
                : ListView.builder(
                    itemCount: _receivedCalls.length,
                    itemBuilder: (context, index) {
                      final call = _receivedCalls[index];
                      return ListTile(
                        leading: const Icon(Icons.call_received),
                        title: SelectableText(call.phoneNumber),
                        subtitle: Text(
                          'Line ${call.line + 1} · '
                          '${call.receivedAt.toLocal()}',
                        ),
                      );
                    },
                  ),
          ),
        ],
      ),
    ),
  );
}
