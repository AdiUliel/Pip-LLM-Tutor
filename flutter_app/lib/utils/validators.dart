// Validators — single source of truth for parent-side input checks. Forms
// reuse these so the same Hebrew message appears everywhere.

import '../constants.dart';

class Validators {
  Validators._();

  static String? email(String? v) {
    final s = (v ?? '').trim();
    if (s.isEmpty) return 'נא להזין כתובת אימייל';
    if (!RegExp(r'^\S+@\S+\.\S+$').hasMatch(s)) {
      return 'כתובת אימייל לא תקינה';
    }
    return null;
  }

  static String? password(String? v) {
    final s = v ?? '';
    if (s.isEmpty) return 'נא להזין סיסמה';
    if (s.length < 4) return 'הסיסמה קצרה מדי (לפחות 4 תווים)';
    return null;
  }

  static String? notEmpty(String? v, {String? field}) {
    final s = (v ?? '').trim();
    if (s.isEmpty) return field == null ? 'שדה חובה' : 'נא להזין $field';
    return null;
  }

  static String? childAge(int v) {
    if (v < AppConstants.minChildAge || v > AppConstants.maxChildAge) {
      return 'הגיל חייב להיות בין ${AppConstants.minChildAge} ל-${AppConstants.maxChildAge}';
    }
    return null;
  }

  static String? fileSize(int bytes) {
    if (bytes > AppConstants.maxUploadBytes) {
      final mb = (bytes / 1024 / 1024).toStringAsFixed(1);
      return 'הקובץ גדול מדי ($mb מ״ב). מקסימום ${(AppConstants.maxUploadBytes / 1024 / 1024).round()} מ״ב.';
    }
    return null;
  }

  static String? fileExtension(String filename) {
    final dot = filename.lastIndexOf('.');
    if (dot < 0) return 'אין סיומת לקובץ';
    final ext = filename.substring(dot + 1).toLowerCase();
    if (!AppConstants.allowedFileExtensions.contains(ext)) {
      return 'סוג הקובץ לא נתמך (${AppConstants.allowedFileExtensions.join(", ")})';
    }
    return null;
  }

  /// At least one subject must be enabled.
  static String? subjectsAtLeastOne(Map<Subject, bool> subs) {
    if (subs.values.any((v) => v)) return null;
    return 'בחרו לפחות מקצוע אחד';
  }
}
