// ConfigProvider — app-wide settings persisted to shared_preferences.

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

  int get sessionMinutes => _prefs.getInt(_kSessionMin) ?? AppConstants.defaultSessionMinutes;
  int get breakEveryMinutes => _prefs.getInt(_kBreakMin) ?? AppConstants.defaultBreakEveryMinutes;
  int get dailyLimitMinutes => _prefs.getInt(_kDailyLimitMin) ?? AppConstants.defaultDailyLimitMinutes;
  /// Default true so existing users keep getting notifications after the
  /// toggle ships; opt-out is explicit.
  bool get notificationsEnabled => _prefs.getBool(_kNotifEnabled) ?? true;

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
}
