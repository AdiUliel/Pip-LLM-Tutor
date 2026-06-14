// MaterialDoc — `materials/{materialId}`. Parent app writes it (typed Q/A
// pairs and/or a file uploaded to Storage). The device reads it to draw
// questions during sessions.

import '../constants.dart';

class QAPair {
  final String question;
  final String answer;
  const QAPair({required this.question, required this.answer});

  factory QAPair.fromMap(Map<String, dynamic> m) => QAPair(
        question: (m['question'] ?? '') as String,
        answer: (m['answer'] ?? '') as String,
      );

  Map<String, dynamic> toMap() => {'question': question, 'answer': answer};
}

class MaterialDoc {
  final String id;
  final String childId;
  final Subject subject;
  final String title;
  final List<QAPair> items;
  /// Set when the parent uploaded a file (Firebase Storage download URL).
  final String? fileUrl;
  final DateTime uploadedAt;
  /// When false, the ESP32 device should skip this material during practice
  /// without the parent having to delete it. Legacy docs (no field) → true.
  final bool enabled;

  const MaterialDoc({
    required this.id,
    required this.childId,
    required this.subject,
    required this.title,
    required this.items,
    this.fileUrl,
    required this.uploadedAt,
    this.enabled = true,
  });

  factory MaterialDoc.fromMap(String id, Map<String, dynamic> m) => MaterialDoc(
        id: id,
        childId: (m['childId'] ?? '') as String,
        subject:
            subjectFromId((m['subject'] ?? 'math') as String) ?? Subject.math,
        title: (m['title'] ?? '') as String,
        items: [
          for (final it
              in ((m['items'] as List?) ?? const [])
                  .cast<Map<String, dynamic>>())
            QAPair.fromMap(it),
        ],
        fileUrl: m['fileUrl'] as String?,
        uploadedAt: m['uploadedAt'] is DateTime
            ? m['uploadedAt'] as DateTime
            : DateTime.now(),
        enabled: (m['enabled'] as bool?) ?? true,
      );

  Map<String, dynamic> toMap() => {
        'childId': childId,
        'subject': subjectMeta[subject]!.id,
        'title': title,
        'items': [for (final i in items) i.toMap()],
        'fileUrl': fileUrl,
        'uploadedAt': uploadedAt,
        'enabled': enabled,
      };

  MaterialDoc copyWith({bool? enabled}) => MaterialDoc(
        id: id,
        childId: childId,
        subject: subject,
        title: title,
        items: items,
        fileUrl: fileUrl,
        uploadedAt: uploadedAt,
        enabled: enabled ?? this.enabled,
      );
}
