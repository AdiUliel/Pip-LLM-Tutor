// QuestionLog — `sessions/{sid}/questions/{qid}` subcollection.
// Written by the ESP32 device. Read by the app's session-detail screen.

import '../constants.dart';

class QuestionLog {
  final String id;
  final String prompt;
  final String expectedAnswer;
  final String childAnswerTranscript;
  final bool correct;
  /// 1..5 mood at the moment of the question.
  final int mood;
  /// Difficulty 1..5 (device-set).
  final int difficulty;
  final DateTime askedAt;

  const QuestionLog({
    required this.id,
    required this.prompt,
    required this.expectedAnswer,
    required this.childAnswerTranscript,
    required this.correct,
    required this.mood,
    required this.difficulty,
    required this.askedAt,
  });

  factory QuestionLog.fromMap(String id, Map<String, dynamic> m) => QuestionLog(
        id: id,
        prompt: (m['prompt'] ?? '') as String,
        expectedAnswer: (m['expectedAnswer'] ?? '') as String,
        childAnswerTranscript:
            (m['childAnswerTranscript'] ?? '') as String,
        correct: (m['correct'] ?? false) as bool,
        mood: MoodScale.clamp((m['mood'] as num?)?.toInt() ?? 3),
        difficulty: (m['difficulty'] as num?)?.toInt() ?? 1,
        askedAt:
            m['askedAt'] is DateTime ? m['askedAt'] as DateTime : DateTime.now(),
      );

  Map<String, dynamic> toMap() => {
        'prompt': prompt,
        'expectedAnswer': expectedAnswer,
        'childAnswerTranscript': childAnswerTranscript,
        'correct': correct,
        'mood': mood,
        'difficulty': difficulty,
        'askedAt': askedAt,
      };
}
