// Derives a list of user-visible alerts from the live providers. Pure
// computation — no I/O, no state.

import 'package:flutter/material.dart';

import '../providers/config_provider.dart';
import '../providers/device_provider.dart';
import '../providers/stats_provider.dart';
import '../theme.dart';

enum AlertSeverity { info, warn, error }

class AppAlert {
  final String id;
  final String title;
  final String sub;
  final IconData icon;
  final AlertSeverity severity;
  const AppAlert({
    required this.id,
    required this.title,
    required this.sub,
    required this.icon,
    required this.severity,
  });

  Color get color {
    switch (severity) {
      case AlertSeverity.error:
        return AppColors.coral;
      case AlertSeverity.warn:
        return const Color(0xFFC98A12);
      case AlertSeverity.info:
        return AppColors.sky;
    }
  }

  Color get tint {
    switch (severity) {
      case AlertSeverity.error:
        return AppColors.coralSoft;
      case AlertSeverity.warn:
        return const Color(0xFFFFF0C9);
      case AlertSeverity.info:
        return AppColors.skySoft;
    }
  }
}

List<AppAlert> buildAlerts({
  required DeviceProvider device,
  required StatsProvider stats,
  required ConfigProvider config,
}) {
  final list = <AppAlert>[];

  // Device went offline — only flag once we've heard from it at least once.
  if (device.state != null &&
      device.state!.lastHeartbeat != null &&
      !device.isOnline) {
    list.add(const AppAlert(
      id: 'device.offline',
      title: 'המכשיר לא פעיל',
      sub: 'לא הייתה פעילות מהמכשיר זמן ממושך',
      icon: Icons.wifi_off_rounded,
      severity: AlertSeverity.error,
    ));
  }

  final last = stats.lastSession;
  if (last != null) {
    if (last.moodSummary <= 2) {
      list.add(AppAlert(
        id: 'session.lowMood',
        title: 'מצב רוח נמוך במפגש האחרון',
        sub: '${last.moodSummary}/5 — תגובות הילד הראו תסכול',
        icon: Icons.mood_bad_rounded,
        severity: AlertSeverity.warn,
      ));
    }
    if (last.questionsAsked > 0 && last.accuracyPct < 60) {
      list.add(AppAlert(
        id: 'session.lowAccuracy',
        title: 'אחוז הצלחה נמוך',
        sub: 'במפגש האחרון: ${last.accuracyPct}%',
        icon: Icons.trending_down_rounded,
        severity: AlertSeverity.warn,
      ));
    }
  }

  return list;
}
