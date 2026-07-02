// Real DeviceSyncService — streams `deviceState/{deviceId}` from Firestore
// and writes `command` for optional remote start/stop. The ESP32 device
// writes its `status` + `lastHeartbeat` directly to the same doc.

import 'package:cloud_firestore/cloud_firestore.dart';

import '../constants.dart';
import '../models/device_state.dart';
import 'device_sync_service.dart';

class DeviceSyncServiceReal implements DeviceSyncService {
  DeviceSyncServiceReal({FirebaseFirestore? firestore})
      : _db = firestore ?? FirebaseFirestore.instance;

  final FirebaseFirestore _db;

  Map<String, dynamic> _hydrate(Map<String, dynamic> raw) {
    final out = <String, dynamic>{};
    raw.forEach((k, v) {
      out[k] = v is Timestamp ? v.toDate() : v;
    });
    return out;
  }

  @override
  Stream<DeviceState> watch(String deviceId) => _db
          .collection(AppConstants.colDeviceState)
          .doc(deviceId)
          .snapshots()
          .map((d) {
        final data = d.data();
        if (data == null) return DeviceState.offline(deviceId);
        return DeviceState.fromMap(deviceId, _hydrate(data));
      });

  @override
  Future<DeviceState?> tryFetch(String deviceId) async {
    final snap = await _db
        .collection(AppConstants.colDeviceState)
        .doc(deviceId)
        .get();
    final data = snap.data();
    if (data == null) return null;
    return DeviceState.fromMap(deviceId, _hydrate(data));
  }

  @override
  Future<void> sendCommand(String deviceId, String command) => _db
      .collection(AppConstants.colDeviceState)
      .doc(deviceId)
      .set({'command': command}, SetOptions(merge: true));

  @override
  void dispose() {
    // Firestore snapshot subscriptions are owned by callers — nothing to do.
  }
}
