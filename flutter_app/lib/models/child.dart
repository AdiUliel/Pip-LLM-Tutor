// Child — `children/{childId}` in Firestore. Written by the parent app, read
// by both app and the ESP32 device.

import '../constants.dart';

// Legacy docs stored topicFocus[subject] as a single String. Accept both
// shapes so old children continue to load after the multi-select rollout.
List<String> _topicsFromValue(dynamic v) {
  if (v is List) return v.whereType<String>().toList();
  if (v is String && v.isNotEmpty) return [v];
  return const [];
}

class ChildSettings {
  final int sessionMinutes;
  final int breakEveryMinutes;
  final int dailyLimitMinutes;

  // Break policy honored by the cloud tutor engine (and, in turn, the device):
  // offer a break after the first [breakFirstQuestions] questions, then every
  // [breakEveryQuestions] questions — OR after [breakAfterMinutes] minutes of
  // session, whichever comes first.
  final int breakFirstQuestions;
  final int breakEveryQuestions;
  final int breakAfterMinutes;

  const ChildSettings({
    required this.sessionMinutes,
    required this.breakEveryMinutes,
    required this.dailyLimitMinutes,
    required this.breakFirstQuestions,
    required this.breakEveryQuestions,
    required this.breakAfterMinutes,
  });

  factory ChildSettings.defaults() => const ChildSettings(
        sessionMinutes: AppConstants.defaultSessionMinutes,
        breakEveryMinutes: AppConstants.defaultBreakEveryMinutes,
        dailyLimitMinutes: AppConstants.defaultDailyLimitMinutes,
        breakFirstQuestions: AppConstants.defaultBreakFirstQuestions,
        breakEveryQuestions: AppConstants.defaultBreakEveryQuestions,
        breakAfterMinutes: AppConstants.defaultBreakAfterMinutes,
      );

  factory ChildSettings.fromMap(Map<String, dynamic> m) => ChildSettings(
        sessionMinutes: (m['sessionMinutes'] as num?)?.toInt() ??
            AppConstants.defaultSessionMinutes,
        breakEveryMinutes: (m['breakEveryMinutes'] as num?)?.toInt() ??
            AppConstants.defaultBreakEveryMinutes,
        dailyLimitMinutes: (m['dailyLimitMinutes'] as num?)?.toInt() ??
            AppConstants.defaultDailyLimitMinutes,
        breakFirstQuestions: (m['breakFirstQuestions'] as num?)?.toInt() ??
            AppConstants.defaultBreakFirstQuestions,
        breakEveryQuestions: (m['breakEveryQuestions'] as num?)?.toInt() ??
            AppConstants.defaultBreakEveryQuestions,
        breakAfterMinutes: (m['breakAfterMinutes'] as num?)?.toInt() ??
            AppConstants.defaultBreakAfterMinutes,
      );

  Map<String, dynamic> toMap() => {
        'sessionMinutes': sessionMinutes,
        'breakEveryMinutes': breakEveryMinutes,
        'dailyLimitMinutes': dailyLimitMinutes,
        'breakFirstQuestions': breakFirstQuestions,
        'breakEveryQuestions': breakEveryQuestions,
        'breakAfterMinutes': breakAfterMinutes,
      };

  ChildSettings copyWith({
    int? sessionMinutes,
    int? breakEveryMinutes,
    int? dailyLimitMinutes,
    int? breakFirstQuestions,
    int? breakEveryQuestions,
    int? breakAfterMinutes,
  }) =>
      ChildSettings(
        sessionMinutes: sessionMinutes ?? this.sessionMinutes,
        breakEveryMinutes: breakEveryMinutes ?? this.breakEveryMinutes,
        dailyLimitMinutes: dailyLimitMinutes ?? this.dailyLimitMinutes,
        breakFirstQuestions: breakFirstQuestions ?? this.breakFirstQuestions,
        breakEveryQuestions: breakEveryQuestions ?? this.breakEveryQuestions,
        breakAfterMinutes: breakAfterMinutes ?? this.breakAfterMinutes,
      );
}

class Child {
  final String id;
  final String parentId;
  final String name;
  final int age;
  final Gender gender;
  final List<Subject> subjectsEnabled;
  final Map<Subject, List<String>> topicFocus; // English topic keys (see AppConstants); empty list = no preset focus
  final Map<Subject, int> level;
  final ChildSettings settings;
  final String deviceId;
  final DateTime createdAt;

  const Child({
    required this.id,
    required this.parentId,
    required this.name,
    required this.age,
    required this.gender,
    required this.subjectsEnabled,
    required this.topicFocus,
    required this.level,
    required this.settings,
    required this.deviceId,
    required this.createdAt,
  });

  factory Child.fromMap(String id, Map<String, dynamic> m) {
    final enabledIds = (m['subjectsEnabled'] as List?)?.cast<String>() ?? const [];
    final enabled = <Subject>[
      for (final id in enabledIds)
        if (subjectFromId(id) != null) subjectFromId(id)!,
    ];

    final focusRaw = (m['topicFocus'] as Map?)?.cast<String, dynamic>() ?? const {};
    final focus = <Subject, List<String>>{
      for (final e in focusRaw.entries)
        if (subjectFromId(e.key) != null)
          subjectFromId(e.key)!: _topicsFromValue(e.value),
    };

    final levelRaw = (m['level'] as Map?)?.cast<String, dynamic>() ?? const {};
    final level = <Subject, int>{
      for (final e in levelRaw.entries)
        if (subjectFromId(e.key) != null)
          subjectFromId(e.key)!: (e.value as num?)?.toInt() ?? 1,
    };

    return Child(
      id: id,
      parentId: (m['parentId'] ?? '') as String,
      name: (m['name'] ?? '') as String,
      age: (m['age'] as num?)?.toInt() ?? AppConstants.defaultChildAge,
      gender: genderFromId(m['gender'] as String?),
      subjectsEnabled: enabled,
      topicFocus: focus,
      level: level,
      settings: ChildSettings.fromMap(
        (m['settings'] as Map?)?.cast<String, dynamic>() ?? const {},
      ),
      deviceId: (m['deviceId'] ?? '') as String,
      createdAt: m['createdAt'] is DateTime
          ? m['createdAt'] as DateTime
          : DateTime.now(),
    );
  }

  Map<String, dynamic> toMap() => {
        'parentId': parentId,
        'name': name,
        'age': age,
        'gender': genderId[gender],
        'subjectsEnabled': [for (final s in subjectsEnabled) subjectMeta[s]!.id],
        'topicFocus': {
          for (final e in topicFocus.entries) subjectMeta[e.key]!.id: e.value,
        }, // Map<String, List<String>>; legacy docs may have plain String.
        'level': {
          for (final e in level.entries) subjectMeta[e.key]!.id: e.value,
        },
        'settings': settings.toMap(),
        'deviceId': deviceId,
        'createdAt': createdAt,
      };

  Child copyWith({
    String? name,
    int? age,
    Gender? gender,
    List<Subject>? subjectsEnabled,
    Map<Subject, List<String>>? topicFocus,
    Map<Subject, int>? level,
    ChildSettings? settings,
    String? deviceId,
  }) =>
      Child(
        id: id,
        parentId: parentId,
        name: name ?? this.name,
        age: age ?? this.age,
        gender: gender ?? this.gender,
        subjectsEnabled: subjectsEnabled ?? this.subjectsEnabled,
        topicFocus: topicFocus ?? this.topicFocus,
        level: level ?? this.level,
        settings: settings ?? this.settings,
        deviceId: deviceId ?? this.deviceId,
        createdAt: createdAt,
      );
}
