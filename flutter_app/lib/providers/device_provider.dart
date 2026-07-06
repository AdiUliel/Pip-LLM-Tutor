// DeviceProvider — wraps the live `deviceState/{deviceId}` stream and exposes
// derived booleans (`isOnline`, `status`) for the dashboard / monitor.

import 'dart:async';

import 'package:flutter/foundation.dart';

import '../models/device_state.dart';
import '../services/device_sync_service.dart';

class DeviceProvider extends ChangeNotifier {
  DeviceProvider(this._svc);

  final DeviceSyncService _svc;

  DeviceState? _state;
  StreamSubscription<DeviceState>? _sub;
  String? _deviceId;

  DeviceState? get state => _state;
  String? get deviceId => _deviceId;
  bool get isOnline => _state?.online ?? false;

  /// Switch to (or start watching) a different device.
  void watch(String deviceId) {
    if (_deviceId == deviceId) return;
    _deviceId = deviceId;
    _sub?.cancel();
    _state = null;
    notifyListeners();
    _sub = _svc.watch(deviceId).listen((s) {
      _state = s;
      notifyListeners();
    });
  }

  /// Reactively follow whichever device the active child is linked to. Wired to
  /// ChildProvider in main.dart, so pairing / device-swap / switching child
  /// updates the "connected" state IMMEDIATELY — no logout+login needed. An
  /// empty/absent id (child not yet paired) stops watching. Idempotent.
  void syncTo(String? deviceId) {
    if (deviceId == null || deviceId.isEmpty) {
      _stop();
      return;
    }
    watch(deviceId);
  }

  void _stop() {
    if (_sub == null && _deviceId == null && _state == null) return;
    _sub?.cancel();
    _sub = null;
    _deviceId = null;
    _state = null;
    notifyListeners();
  }

  Future<void> sendStart() async {
    if (_deviceId != null) await _svc.sendCommand(_deviceId!, 'start');
  }

  Future<void> sendStop() async {
    if (_deviceId != null) await _svc.sendCommand(_deviceId!, 'stop');
  }

  @override
  void dispose() {
    _sub?.cancel();
    super.dispose();
  }
}
