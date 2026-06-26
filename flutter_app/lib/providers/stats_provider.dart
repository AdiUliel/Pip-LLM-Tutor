// StatsProvider — subscribes to sessions for a child and exposes computed
// aggregates needed by the Dashboard / Reports / Trends screens.

import 'dart:async';

import 'package:flutter/foundation.dart';

import '../models/session.dart';
import '../services/firebase_service.dart';

class StatsProvider extends ChangeNotifier {
  StatsProvider(this._fb);

  final FirebaseService _fb;

  List<Session> _sessions = const [];
  StreamSubscription<List<Session>>? _sub;

  List<Session> get sessions => _sessions;
  Session? get lastSession => _sessions.isEmpty ? null : _sessions.first;
  bool get hasAny => _sessions.isNotEmpty;

  /// Minutes the child has used the device today (across sessions).
  int get minutesToday {
    final now = DateTime.now();
    final startOfDay = DateTime(now.year, now.month, now.day);
    return _sessions
        .where((s) => !s.startedAt.isBefore(startOfDay))
        .fold<int>(0, (acc, s) => acc + s.durationMinutes);
  }

  /// Mean accuracy across all known sessions (0..100), null if no data.
  double? get averageAccuracy {
    if (_sessions.isEmpty) return null;
    final sum = _sessions.fold<int>(0, (a, s) => a + s.accuracyPct);
    return sum / _sessions.length;
  }

  /// Newest-first list of (date, accuracy) for charting.
  List<({DateTime date, int accuracy})> get accuracySeries =>
      [for (final s in _sessions.reversed) (date: s.startedAt, accuracy: s.accuracyPct)];

  /// Newest-first list of (date, mood 1..5) for charting.
  List<({DateTime date, int mood})> get moodSeries =>
      [for (final s in _sessions.reversed) (date: s.startedAt, mood: s.moodSummary)];

  void load(String childId, {int limit = 50}) {
    _sub?.cancel();
    _sub = _fb.watchSessions(childId, limit: limit).listen((list) {
      // A session where no question was asked isn't a meaningful report —
      // hide it from the list, charts, and dashboard summary.
      _sessions = list.where((s) => s.questionsAsked > 0).toList();
      notifyListeners();
    });
  }

  void clear() {
    _sub?.cancel();
    _sub = null;
    _sessions = const [];
    notifyListeners();
  }

  @override
  void dispose() {
    _sub?.cancel();
    super.dispose();
  }
}
