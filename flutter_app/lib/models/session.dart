// Session — `sessions/{sessionId}`. Written by the ESP32 device; read by the
// parent app for the Reports + Trends screens.

import '../constants.dart';

class Session {
  final String id;
  final String childId;
  final Subject subject;
  final DateTime startedAt;
  final DateTime? endedAt;
  final int questionsAsked;
  final int correctCount;
  final int wrongCount;
  final int starsEarned;
  final int longestStreak;
  /// 1..5 mood summary (see [MoodScale]).
  final int moodSummary;

  const Session({
    required this.id,
    required this.childId,
    required this.subject,
    required this.startedAt,
    this.endedAt,
    required this.questionsAsked,
    required this.correctCount,
    required this.wrongCount,
    required this.starsEarned,
    required this.longestStreak,
    required this.moodSummary,
  });

  /// 0..100 percent rounded. Recomputed from counts so the model has no
  /// internal redundancy (single source of truth).
  int get accuracyPct =>
      questionsAsked == 0 ? 0 : ((correctCount / questionsAsked) * 100).round();

  Duration? get duration => endedAt?.difference(startedAt);

  int get durationMinutes => (duration?.inSeconds ?? 0) ~/ 60;

  factory Session.fromMap(String id, Map<String, dynamic> m) => Session(
        id: id,
        childId: (m['childId'] ?? '') as String,
        subject: subjectFromId((m['subject'] ?? 'math') as String) ?? Subject.math,
        startedAt:
            m['startedAt'] is DateTime ? m['startedAt'] as DateTime : DateTime.now(),
        endedAt: m['endedAt'] is DateTime ? m['endedAt'] as DateTime : null,
        questionsAsked: (m['questionsAsked'] as num?)?.toInt() ?? 0,
        correctCount: (m['correctCount'] as num?)?.toInt() ?? 0,
        wrongCount: (m['wrongCount'] as num?)?.toInt() ?? 0,
        starsEarned: (m['starsEarned'] as num?)?.toInt() ?? 0,
        longestStreak: (m['longestStreak'] as num?)?.toInt() ?? 0,
        moodSummary:
            MoodScale.clamp((m['moodSummary'] as num?)?.toInt() ?? 3),
      );

  Map<String, dynamic> toMap() => {
        'childId': childId,
        'subject': subjectMeta[subject]!.id,
        'startedAt': startedAt,
        'endedAt': endedAt,
        'questionsAsked': questionsAsked,
        'correctCount': correctCount,
        'wrongCount': wrongCount,
        'starsEarned': starsEarned,
        'longestStreak': longestStreak,
        'moodSummary': moodSummary,
      };
}
