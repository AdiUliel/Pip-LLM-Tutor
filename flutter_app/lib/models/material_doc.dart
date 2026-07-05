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

  /// A parent-facing Hebrew description of an extraction problem, or null when
  /// the material is fine or still pending. Maps the technical codes written by
  /// the extractQuestionsFromMaterial Cloud Function into readable text.
  String? get extractionIssueHe {
    if (isExtractionPending) return null;
    // A material that already has questions is usable — never flag a failure
    // for it, even if a stale extractionError lingers from an earlier attempt.
    if (items.isNotEmpty) return null;
    final err = extractionError;
    if (err != null && err.isNotEmpty) {
      if (err.startsWith('inappropriate')) {
        final i = err.indexOf(':');
        final reason = i >= 0 ? err.substring(i + 1).trim() : '';
        return reason.isNotEmpty
            ? 'התוכן לא סומן כמתאים לילד ($reason). החומר לא הופעל.'
            : 'התוכן לא סומן כמתאים לילד. החומר לא הופעל.';
      }
      if (err.startsWith('no_questions')) {
        return 'לא נמצאו שאלות במסמך. נסה קובץ ברור יותר.';
      }
      if (err.startsWith('download')) {
        return 'הקובץ לא נטען. נסה להעלות שוב.';
      }
      if (err.startsWith('gemini')) {
        return 'עיבוד השאלות נכשל. נסה שוב.';
      }
      return 'אירעה תקלה בעיבוד החומר.';
    }
    // Legacy docs (extracted before the no_questions flag existed): a processed
    // file that yielded nothing still deserves the same hint.
    if (fileUrl != null && fileUrl!.isNotEmpty &&
        items.isEmpty && itemsGeneratedAt != null) {
      return 'לא נמצאו שאלות במסמך. נסה קובץ ברור יותר.';
    }
    return null;
  }

  bool get hasExtractionIssue => extractionIssueHe != null;

  /// True when extraction flagged the content as unsuitable for the child
  /// (wrong subject / not age-appropriate). The Cloud Function force-disables
  /// such a material; the parent must NOT be able to re-include it in practice,
  /// so the UI locks its enable toggle. Distinct from a plain parent-disabled
  /// material (enabled == false with no such error), which stays re-enableable.
  bool get isBlockedForContent =>
      (extractionError ?? '').startsWith('inappropriate');

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
