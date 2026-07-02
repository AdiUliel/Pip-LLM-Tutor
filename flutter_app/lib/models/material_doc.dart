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
  /// Set by the `extractQuestionsFromMaterial` Cloud Function once it has
  /// attempted to parse the uploaded file. Null while extraction is still
  /// pending → UI shows "מעבד שאלות…". Non-null with empty [items] means
  /// extraction completed but found nothing (or hit [extractionError]).
  final DateTime? itemsGeneratedAt;
  /// Non-null when extraction failed (download or Gemini error). Used for
  /// surfacing a hint in the UI; safe to ignore.
  final String? extractionError;

  const MaterialDoc({
    required this.id,
    required this.childId,
    required this.subject,
    required this.title,
    required this.items,
    this.fileUrl,
    required this.uploadedAt,
    this.enabled = true,
    this.itemsGeneratedAt,
    this.extractionError,
  });

  /// True when a file was uploaded but extraction hasn't finished yet —
  /// the UI uses this to show "מעבד שאלות…" instead of "0 שאלות".
  bool get isExtractionPending =>
      fileUrl != null && fileUrl!.isNotEmpty &&
      items.isEmpty && itemsGeneratedAt == null;

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
        itemsGeneratedAt: m['itemsGeneratedAt'] is DateTime
            ? m['itemsGeneratedAt'] as DateTime
            : null,
        extractionError: m['extractionError'] as String?,
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
        itemsGeneratedAt: itemsGeneratedAt,
        extractionError: extractionError,
      );
}
