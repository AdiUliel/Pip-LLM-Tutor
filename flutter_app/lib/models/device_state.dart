// DeviceState — `deviceState/{deviceId}`. The device writes its `status` +
// `lastHeartbeat` periodically. The app derives `online` from the heartbeat
// freshness and may write `command` for optional remote start/stop.

import '../constants.dart';

class DeviceState {
  final String deviceId;
  final DateTime? lastHeartbeat;
  final DeviceStatus status;
  final Subject? activeSubject;
  final String? currentQuestion;
  /// Display name of the child currently using the device (set after identify;
  /// null when idle/unpaired). Lets the app show the live session under the
  /// child actually in front of the device, not just the device owner.
  final String? activeChildName;
  /// Doc id of the child currently using the device (matches children/{id}).
  /// Lets the app show the live device ONLY under the child it's working with.
  final String? activeChildId;
  /// "start" | "stop" | "none" (parent app writes; device clears it).
  final String command;

  const DeviceState({
    required this.deviceId,
    required this.lastHeartbeat,
    required this.status,
    required this.activeSubject,
    required this.currentQuestion,
    this.activeChildName,
    this.activeChildId,
    required this.command,
  });

  /// Derived: device is online iff we heard a heartbeat within the timeout.
  bool get online {
    final h = lastHeartbeat;
    if (h == null) return false;
    final age = DateTime.now().difference(h).inSeconds;
    return age <= AppConstants.heartbeatTimeoutSec;
  }

  factory DeviceState.fromMap(String id, Map<String, dynamic> m) => DeviceState(
        deviceId: id,
        lastHeartbeat: m['lastHeartbeat'] is DateTime
            ? m['lastHeartbeat'] as DateTime
            : null,
        status: deviceStatusFromId((m['status'] ?? 'idle') as String),
        activeSubject:
            m['activeSubject'] == null ? null : subjectFromId(m['activeSubject'] as String),
        currentQuestion: m['currentQuestion'] as String?,
        activeChildName: m['activeChildName'] as String?,
        activeChildId: m['activeChildId'] as String?,
        command: (m['command'] ?? 'none') as String,
      );

  Map<String, dynamic> toMap() => {
        'lastHeartbeat': lastHeartbeat,
        'status': deviceStatusId[status],
        'activeSubject':
            activeSubject == null ? null : subjectMeta[activeSubject!]!.id,
        'currentQuestion': currentQuestion,
        'activeChildName': activeChildName,
        'activeChildId': activeChildId,
        'command': command,
      };

  DeviceState copyWith({
    DateTime? lastHeartbeat,
    DeviceStatus? status,
    Subject? activeSubject,
    String? currentQuestion,
    String? activeChildName,
    String? activeChildId,
    String? command,
  }) =>
      DeviceState(
        deviceId: deviceId,
        lastHeartbeat: lastHeartbeat ?? this.lastHeartbeat,
        status: status ?? this.status,
        activeSubject: activeSubject ?? this.activeSubject,
        currentQuestion: currentQuestion ?? this.currentQuestion,
        activeChildName: activeChildName ?? this.activeChildName,
        activeChildId: activeChildId ?? this.activeChildId,
        command: command ?? this.command,
      );

  factory DeviceState.offline(String deviceId) => DeviceState(
        deviceId: deviceId,
        lastHeartbeat: null,
        status: DeviceStatus.idle,
        activeSubject: null,
        currentQuestion: null,
        command: 'none',
      );
}
