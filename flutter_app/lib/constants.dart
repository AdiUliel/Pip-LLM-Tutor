// All tunable parameters + design enums for the Emotional Tutor parent app.
// No magic numbers should appear elsewhere in the codebase.

import 'package:flutter/material.dart';

class AppConstants {
  AppConstants._();

  // --- Device sync ---
  static const int heartbeatTimeoutSec = 30;
  static const int mockDeviceTickSec = 2;

  // --- Pairing (shared contract with ESP32 firmware) ---
  // The ESP32 displays a 6-digit code derived deterministically from its MAC.
  // The parent app types that code; deviceId is always `{deviceIdPrefix}{code}`.
  // To allow a freshly-booted device a couple of heartbeats to land, the
  // pairing flow accepts a heartbeat up to `pairingMaxHeartbeatAgeSec` old.
  static const String deviceIdPrefix = 'TUTOR-';
  static const int pairingCodeLength = 6;
  static const int pairingMaxHeartbeatAgeSec = 60;

  // --- Offline queue ---
  static const int offlineQueueMaxOps = 100;
  static const int offlineRetryIntervalSec = 5;

  // --- Child age range (User Stories: kids 7-11; widened by 1 each side) ---
  static const int minChildAge = 6;
  static const int maxChildAge = 12;
  static const int defaultChildAge = 8;

  // --- Child gender (Hebrew grammar — masculine vs. feminine) ---
  static const Gender defaultGender = Gender.girl;


  // --- Session defaults (parent can override in Settings) ---
  static const int defaultSessionMinutes = 15;
  static const int defaultBreakEveryMinutes = 7;
  static const int defaultDailyLimitMinutes = 30;

  // --- Difficulty ---
  static const int minLevel = 1;
  static const int maxLevel = 10;
  static const int defaultStartingLevel = 1;

  // --- Material upload ---
  static const int maxUploadBytes = 5 * 1024 * 1024; // 5 MB
  static const List<String> allowedFileExtensions = [
    'pdf', 'txt', 'csv', 'png', 'jpg', 'jpeg',
  ];

  // --- Topic focus (stored as English keys; UI shows Hebrew labels) ---
  static const List<String> mathTopics = [
    'addition', 'subtraction', 'multiplication', 'division',
    'fractions', 'word_problems',
  ];
  static const List<String> englishTopics = [
    'spelling', 'vocabulary', 'reading', 'grammar', 'sentences',
  ];

  // --- UI ---
  static const String defaultLocale = 'he'; // Hebrew RTL

  // --- FCM (push notifications) ---
  // Web Push needs a VAPID public key from
  //   Firebase Console → Project Settings → Cloud Messaging
  //   → Web Push certificates → Generate key pair → copy the public key.
  // Leave empty to skip web push (Android push still works).
  static const String fcmVapidKey = 'BPeGUyxo3QpcedjZRoSHKlpcpZRKN9GFBFN1bq_RbwVWFtxaZd9RxBOzfotuaqxa6Md2f4_JDKDwafWW5Ck8Qks';

  // --- Firestore collection names (shared with the ESP32 device) ---
  static const String colParents = 'parents';
  static const String colChildren = 'children';
  static const String colMaterials = 'materials';
  static const String colDeviceState = 'deviceState';
  static const String colSessions = 'sessions';
  static const String subColQuestions = 'questions';

  // --- Hebrew labels for topic keys (UI display) ---
  static const Map<String, String> topicHeLabels = {
    'addition':       'חיבור',
    'subtraction':    'חיסור',
    'multiplication': 'כפל',
    'division':       'חילוק',
    'fractions':      'שברים',
    'word_problems':  'בעיות מילוליות',
    'spelling':       'איות',
    'vocabulary':     'אוצר מילים',
    'reading':        'קריאה',
    'grammar':        'דקדוק',
    'sentences':      'משפטים',
  };
}

// ════════════════════════════════════════════════
// Gender (matters for Hebrew grammar — the device addresses the child
// in masculine vs. feminine form). Stored as the lowercase string id.
// ════════════════════════════════════════════════

enum Gender { boy, girl }

const Map<Gender, String> genderId = {
  Gender.boy: 'boy',
  Gender.girl: 'girl',
};

const Map<Gender, String> genderHeLabel = {
  Gender.boy: 'בן',
  Gender.girl: 'בת',
};

const Map<Gender, String> genderEmoji = {
  Gender.boy: '👦',
  Gender.girl: '👧',
};

/// Lookup; falls back to [AppConstants.defaultGender] for missing / unknown
/// values so legacy Firestore docs render predictably.
Gender genderFromId(String? s) {
  for (final e in genderId.entries) {
    if (e.value == s) return e.key;
  }
  return AppConstants.defaultGender;
}

// ════════════════════════════════════════════════
// Subjects
// ════════════════════════════════════════════════

enum Subject { math, english }

class SubjectMeta {
  final String id;
  final String heLabel;
  final String emoji;
  final Color tint;
  final Color ink;
  const SubjectMeta(this.id, this.heLabel, this.emoji, this.tint, this.ink);
}

const Map<Subject, SubjectMeta> subjectMeta = {
  Subject.math:    SubjectMeta('math',    'חשבון',  '➕', Color(0xFFFFF0C9), Color(0xFFC98A12)),
  Subject.english: SubjectMeta('english', 'אנגלית', '🔤', Color(0xFFD2F4EA), Color(0xFF1E9C7E)),
};

Subject? subjectFromId(String id) =>
    Subject.values.where((s) => subjectMeta[s]!.id == id).firstOrNull;

// ════════════════════════════════════════════════
// Device lifecycle (written by the device, read by the app)
// "break" is a reserved word in Dart → renamed to onBreak; serialized as "break".
// ════════════════════════════════════════════════

enum DeviceStatus { idle, asking, listening, feedback, onBreak, error }

const Map<DeviceStatus, String> deviceStatusId = {
  DeviceStatus.idle:      'idle',
  DeviceStatus.asking:    'asking',
  DeviceStatus.listening: 'listening',
  DeviceStatus.feedback:  'feedback',
  DeviceStatus.onBreak:   'break',
  DeviceStatus.error:     'error',
};

const Map<DeviceStatus, String> deviceStatusHe = {
  DeviceStatus.idle:      'במנוחה',
  DeviceStatus.asking:    'שואל שאלה',
  DeviceStatus.listening: 'מקשיב',
  DeviceStatus.feedback:  'נותן משוב',
  DeviceStatus.onBreak:   'בהפסקה',
  DeviceStatus.error:     'תקלה',
};

DeviceStatus deviceStatusFromId(String s) {
  for (final entry in deviceStatusId.entries) {
    if (entry.value == s) return entry.key;
  }
  return DeviceStatus.idle;
}

// ════════════════════════════════════════════════
// Mood scale (1..5 integer, stored verbatim in Firestore)
// 1 = worst (frustrated), 5 = best (enjoying)
// ════════════════════════════════════════════════

class MoodScale {
  static const int min = 1;
  static const int max = 5;

  static const Map<int, String> heLabel = {
    1: 'מתוסכל',
    2: 'מאותגר',
    3: 'רגוע',
    4: 'מרוצה',
    5: 'נהנה',
  };

  static const Map<int, Color> color = {
    1: Color(0xFFFF7E7E), // coral
    2: Color(0xFFFFAE7A),
    3: Color(0xFFFFC93C), // sun
    4: Color(0xFF5BC8B2),
    5: Color(0xFF36C9A0), // mint
  };

  // Mood number → robot emotion to display (used by RobotFace widget).
  static const Map<int, RobotEmotion> emotion = {
    1: RobotEmotion.concerned,
    2: RobotEmotion.concerned,
    3: RobotEmotion.neutral,
    4: RobotEmotion.happy,
    5: RobotEmotion.celebrating,
  };

  static int clamp(int v) => v < min ? min : (v > max ? max : v);
}

// ════════════════════════════════════════════════
// Robot mascot emotions (display states)
// ════════════════════════════════════════════════

enum RobotEmotion {
  neutral, speaking, listening, happy,
  proud, encouraging, concerned, celebrating, sleepy,
}

const Map<RobotEmotion, String> robotEmotionHe = {
  RobotEmotion.neutral:     'ניטרלי',
  RobotEmotion.speaking:    'מדבר',
  RobotEmotion.listening:   'מקשיב',
  RobotEmotion.happy:       'שמח',
  RobotEmotion.proud:       'גאה',
  RobotEmotion.encouraging: 'מעודד',
  RobotEmotion.concerned:   'מודאג',
  RobotEmotion.celebrating: 'חוגג',
  RobotEmotion.sleepy:      'הפסקה',
};

// ════════════════════════════════════════════════
// Starting level options (used in the wizard)
// ════════════════════════════════════════════════

class LevelOption {
  final String id;
  final RobotEmotion emotion;
  final String heTitle;
  final String heSub;
  final int startingLevel;
  const LevelOption(this.id, this.emotion, this.heTitle, this.heSub, this.startingLevel);
}

const List<LevelOption> levelOptions = [
  LevelOption('easy',   RobotEmotion.happy,   'מתחילים', 'קל ועדין, הרבה עידוד',    1),
  LevelOption('medium', RobotEmotion.neutral, 'בינוני',  'מתאים לרוב הילדים',       3),
  LevelOption('hard',   RobotEmotion.proud,   'מתקדמים', 'אתגר גדול יותר',          5),
];

extension _FirstOrNull<T> on Iterable<T> {
  T? get firstOrNull {
    final it = iterator;
    return it.moveNext() ? it.current : null;
  }
}
