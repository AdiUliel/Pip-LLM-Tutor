// FirebaseService — single touchpoint for Firestore + Storage. Implemented by
// [FirebaseServiceReal]. Screens never depend on this directly — only
// providers do.

import 'dart:typed_data';

import '../models/child.dart';
import '../models/material_doc.dart';
import '../models/parent_user.dart';
import '../models/question_log.dart';
import '../models/session.dart';

abstract class FirebaseService {
  // ── parents ────────────────────────────────────────────────────────────
  Stream<ParentUser?> watchParent(String parentId);
  Future<void> saveParent(ParentUser parent);

  /// Adds an FCM token to `parents/{parentId}.fcmTokens` (array union).
  /// Idempotent — re-adding an existing token is a no-op.
  Future<void> addParentFcmToken(String parentId, String token);

  /// Removes an FCM token from `parents/{parentId}.fcmTokens` (array remove).
  /// Called on sign-out so notifications stop reaching this device.
  Future<void> removeParentFcmToken(String parentId, String token);

  // ── children ───────────────────────────────────────────────────────────
  Stream<Child?> watchChild(String childId);
  Stream<List<Child>> watchChildrenOfParent(String parentId);

  /// Saves the child. If [Child.id] is empty, generates a new id and
  /// returns the assigned id; otherwise returns the same id.
  Future<String> saveChild(Child child);

  /// Deletes the child document. Caller is expected to have ensured at
  /// least one other child remains; orphan materials/sessions are left as
  /// dangling refs (a later cleanup pass can prune them by childId).
  Future<void> deleteChild(String childId);

  // ── materials ──────────────────────────────────────────────────────────
  Stream<List<MaterialDoc>> watchMaterials(String childId);

  /// Uploads [fileBytes] (optional) to Storage, attaches its download URL
  /// to [material.fileUrl], and saves the document. Returns the id.
  Future<String> uploadMaterial(
    MaterialDoc material, {
    Uint8List? fileBytes,
    String? fileName,
  });

  /// Deletes the Firestore doc and (if [material.fileUrl] is set) the
  /// underlying file in Storage.
  Future<void> deleteMaterial(MaterialDoc material);

  /// Toggles the [MaterialDoc.enabled] flag without rewriting the rest of
  /// the doc — the ESP32 device reads this to decide whether to include
  /// the material in practice.
  Future<void> setMaterialEnabled(String materialId, bool enabled);

  // ── sessions ───────────────────────────────────────────────────────────
  Stream<List<Session>> watchSessions(String childId, {int limit = 50});
  Stream<List<QuestionLog>> watchSessionQuestions(String sessionId);
}
