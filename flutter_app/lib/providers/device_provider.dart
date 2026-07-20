// DeviceProvider — wraps the live `deviceState/{deviceId}` stream and exposes
// derived booleans (`isOnline`, `status`) for the dashboard / monitor.

import 'dart:async';

import 'package:flutter/foundation.dart';

import '../constants.dart';
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

  /// Doc id / name of the child the device is CURRENTLY working with (from
  /// deviceState.activeChild*). Null/empty when the device is idle.
  String? get activeChildId => _state?.activeChildId;
  String? get activeChildName => _state?.activeChildName;

  /// True when the device is online AND currently working with [childId].
  /// Drives per-child separation: a child sees "connected + activity" only while
  /// the device is actually running their session, not a sibling's.
  ///
  /// Fallback: firmware that does not report activeChildId gives no
  /// attribution info, so every linked child is treated as active. Once the
  /// device reports the field, strict per-child separation applies.
  bool isActiveFor(String childId) {
    if (!isOnline) return false;
    final a = _state?.activeChildId ?? '';
    if (a.isEmpty) return true; // legacy firmware — no attribution reported
    return a == childId;
  }

  /// Online but running a DIFFERENT child's session (device is "busy").
  /// Requires a non-idle status: a device that is merely powered on (boot,
  /// between sessions) isn't "busy with" anyone.
  bool isBusyWithOther(String childId) {
    final a = _state?.activeChildId ?? '';
    return isOnline &&
        a.isNotEmpty &&
        a != childId &&
        (_state?.status ?? DeviceStatus.idle) != DeviceStatus.idle;
  }

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
