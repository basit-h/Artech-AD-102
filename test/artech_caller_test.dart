import 'package:artech_caller/artech_caller.dart';
import 'package:test/test.dart';

void main() {
  test('ArtechCall stores the incoming caller ID details', () {
    final receivedAt = DateTime(2026, 10, 30, 12);
    final call = ArtechCall(
      line: 1,
      phoneNumber: '+15551234567',
      receivedAt: receivedAt,
    );

    expect(call.line, 1);
    expect(call.phoneNumber, '+15551234567');
    expect(call.receivedAt, receivedAt);
  });
}
