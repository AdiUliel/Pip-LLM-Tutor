// StatsProvider — subscribes to sessions for a child and exposes computed
// aggregates needed by the Dashboard / Reports / Trends screens.

import 'dart:async';

import 'package:flutter/foundation.dart';

import '../constants.dart';
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

  // ── Lifetime totals (over all loaded sessions) ─────────────────────────────
  int get totalSessions => _sessions.length;
  int get totalQuestions => _sessions.fold<int>(0, (a, s) => a + s.questionsAsked);
  int get totalCorrect => _sessions.fold<int>(0, (a, s) => a + s.correctCount);
  int get totalStars => _sessions.fold<int>(0, (a, s) => a + s.starsEarned);

  /// Sessions started in the last 7 days.
  int get sessionsThisWeek {
    final since = DateTime.now().subtract(const Duration(days: 7));
    return _sessions.where((s) => s.startedAt.isAfter(since)).length;
  }

  /// Mean session length in minutes over sessions that have ended, 0 if none.
  int get avgSessionMinutes {
    final ended = _sessions.where((s) => s.durationMinutes > 0).toList();
    if (ended.isEmpty) return 0;
    return (ended.fold<int>(0, (a, s) => a + s.durationMinutes) / ended.length).round();
  }

  /// Consecutive calendar days (ending today or the most recent session day)
  /// on which the child practised at least once — the "5 days in a row" streak.
  int get activeDayStreak {
    if (_sessions.isEmpty) return 0;
    final days = _sessions
        .map((s) => DateTime(s.startedAt.year, s.startedAt.month, s.startedAt.day))
        .toSet();
    final now = DateTime.now();
    var cursor = DateTime(now.year, now.month, now.day);
    // Allow the streak to be "current" if they practised today OR yesterday.
    if (!days.contains(cursor)) {
      cursor = cursor.subtract(const Duration(days: 1));
      if (!days.contains(cursor)) return 0;
    }
    var streak = 0;
    while (days.contains(cursor)) {
      streak++;
      cursor = cursor.subtract(const Duration(days: 1));
    }
    return streak;
  }

  /// Count of sessions per subject id ("math"/"english"), for the balance split.
  Map<String, int> get sessionsBySubject {
    final out = <String, int>{};
    for (final s in _sessions) {
      final id = subjectMeta[s.subject]!.id;
      out[id] = (out[id] ?? 0) + 1;
    }
    return out;
  }

  /// How sessions ended, keyed by endReason (null/active grouped as "active").
  Map<String, int> get endReasonBreakdown {
    final out = <String, int>{};
    for (final s in _sessions) {
      final key = s.endReason ?? 'active';
      out[key] = (out[key] ?? 0) + 1;
    }
    return out;
  }

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
