// DeviceSyncService — single touchpoint for the ESP32 device's live state
// (read from `deviceState/{deviceId}`) and the optional remote `command`.
// Mock + Real implementations live next to this file.

import '../models/device_state.dart';

abstract class DeviceSyncService {
  /// Streams the latest [DeviceState] for [deviceId]. Emits a synthetic
  /// "offline" state when the device hasn't been heard from.
  Stream<DeviceState> watch(String deviceId);

  /// Optional remote start/stop. Writes `command` to the device's doc;
  /// the device clears it once it has acted on it.
  /// [command] is one of "start" | "stop" | "none".
  Future<void> sendCommand(String deviceId, String command);

  /// Release timers / stream controllers.
  void dispose();
}
