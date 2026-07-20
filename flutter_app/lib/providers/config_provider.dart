// ConfigProvider — app-wide settings persisted to shared_preferences.

import 'dart:convert';

import 'package:flutter/foundation.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../constants.dart';

class ConfigProvider extends ChangeNotifier {
  ConfigProvider(this._prefs);

  final SharedPreferences _prefs;

  static const _kSessionMin = 'config.sessionMin';
  static const _kBreakMin = 'config.breakMin';
  static const _kDailyLimitMin = 'config.dailyLimitMin';
  static const _kNotifEnabled = 'config.notificationsEnabled';
  static const _kSeenIntro = 'config.hasSeenIntro';

  int get sessionMinutes => _prefs.getInt(_kSessionMin) ?? AppConstants.defaultSessionMinutes;
  int get breakEveryMinutes => _prefs.getInt(_kBreakMin) ?? AppConstants.defaultBreakEveryMinutes;
  int get dailyLimitMinutes => _prefs.getInt(_kDailyLimitMin) ?? AppConstants.defaultDailyLimitMinutes;
  /// Default true so existing users keep getting notifications after the
  /// toggle ships; opt-out is explicit.
  bool get notificationsEnabled => _prefs.getBool(_kNotifEnabled) ?? true;

  /// Whether the first-use app intro has been shown once on this device.
  /// Deliberately left out of resetToDefaults so a settings reset doesn't
  /// make the intro reappear.
  bool get hasSeenIntro => _prefs.getBool(_kSeenIntro) ?? false;

  Future<void> setHasSeenIntro(bool v) async {
    await _prefs.setBool(_kSeenIntro, v);
    notifyListeners();
  }

  Future<void> setNotificationsEnabled(bool v) async {
    await _prefs.setBool(_kNotifEnabled, v);
    notifyListeners();
  }

  Future<void> setSessionMinutes(int v) async {
    await _prefs.setInt(_kSessionMin, v);
    notifyListeners();
  }

  Future<void> setBreakEveryMinutes(int v) async {
    await _prefs.setInt(_kBreakMin, v);
    notifyListeners();
  }

  Future<void> setDailyLimitMinutes(int v) async {
    await _prefs.setInt(_kDailyLimitMin, v);
    notifyListeners();
  }

  Future<void> resetToDefaults() async {
    await _prefs.remove(_kSessionMin);
    await _prefs.remove(_kBreakMin);
    await _prefs.remove(_kDailyLimitMin);
    await _prefs.remove(_kNotifEnabled);
    notifyListeners();
  }

  // ── Alert dismissal ────────────────────────────────────────────────────────
  // Alerts are DERIVED live (utils/notifications.dart), so without this they
  // could never be cleared. Persist dismissed (alertId → occurrence
  // fingerprint): the alert stays hidden while the same occurrence persists,
  // and a NEW occurrence (different fingerprint) alerts again.
  static const _kDismissedAlerts = 'config.dismissedAlerts';

  Map<String, String> get _dismissedAlerts {
    final raw = _prefs.getString(_kDismissedAlerts);
    if (raw == null || raw.isEmpty) return const {};
    try {
      return Map<String, String>.from(json.decode(raw) as Map);
    } catch (_) {
      return const {};
    }
  }

  bool isAlertDismissed(String id, String fingerprint) =>
      _dismissedAlerts[id] == fingerprint;

  Future<void> dismissAlert(String id, String fingerprint) async {
    final map = Map<String, String>.from(_dismissedAlerts);
    map[id] = fingerprint;
    await _prefs.setString(_kDismissedAlerts, json.encode(map));
    notifyListeners();
  }
}
