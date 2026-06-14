// Real Firestore + Storage implementation of FirebaseService.
// Never instantiated when Mock mode is ON. Verified end-to-end in Step 8.

import 'dart:typed_data';

import 'package:cloud_firestore/cloud_firestore.dart';
import 'package:firebase_storage/firebase_storage.dart';

import '../constants.dart';
import '../models/child.dart';
import '../models/material_doc.dart';
import '../models/parent_user.dart';
import '../models/question_log.dart';
import '../models/session.dart';
import 'firebase_service.dart';

class FirebaseServiceReal implements FirebaseService {
  FirebaseServiceReal({FirebaseFirestore? firestore, FirebaseStorage? storage})
      : _db = firestore ?? FirebaseFirestore.instance,
        _storage = storage ?? FirebaseStorage.instance;

  final FirebaseFirestore _db;
  final FirebaseStorage _storage;

  CollectionReference<Map<String, dynamic>> get _parents =>
      _db.collection(AppConstants.colParents);
  CollectionReference<Map<String, dynamic>> get _children =>
      _db.collection(AppConstants.colChildren);
  CollectionReference<Map<String, dynamic>> get _materials =>
      _db.collection(AppConstants.colMaterials);
  CollectionReference<Map<String, dynamic>> get _sessions =>
      _db.collection(AppConstants.colSessions);

  // Convert Firestore Timestamps in a doc to DateTime for the models.
  Map<String, dynamic> _hydrate(Map<String, dynamic> raw) {
    final out = <String, dynamic>{};
    raw.forEach((k, v) {
      if (v is Timestamp) {
        out[k] = v.toDate();
      } else if (v is Map) {
        out[k] = _hydrate(v.cast<String, dynamic>());
      } else {
        out[k] = v;
      }
    });
    return out;
  }

  // ── parents ────────────────────────────────────────────────────────────
  @override
  Stream<ParentUser?> watchParent(String parentId) =>
      _parents.doc(parentId).snapshots().map((d) {
        final data = d.data();
        if (data == null) return null;
        return ParentUser.fromMap(d.id, _hydrate(data));
      });

  @override
  Future<void> saveParent(ParentUser parent) =>
      _parents.doc(parent.id).set(parent.toMap(), SetOptions(merge: true));

  // ── children ───────────────────────────────────────────────────────────
  @override
  Stream<Child?> watchChild(String childId) =>
      _children.doc(childId).snapshots().map((d) {
        final data = d.data();
        if (data == null) return null;
        return Child.fromMap(d.id, _hydrate(data));
      });

  @override
  Stream<List<Child>> watchChildrenOfParent(String parentId) => _children
      .where('parentId', isEqualTo: parentId)
      .snapshots()
      .map((q) => q.docs
          .map((d) => Child.fromMap(d.id, _hydrate(d.data())))
          .toList());

  @override
  Future<String> saveChild(Child child) async {
    final ref = child.id.isEmpty ? _children.doc() : _children.doc(child.id);
    await ref.set(child.toMap(), SetOptions(merge: true));
    return ref.id;
  }

  @override
  Future<void> deleteChild(String childId) =>
      _children.doc(childId).delete();

  // ── materials ──────────────────────────────────────────────────────────
  @override
  Stream<List<MaterialDoc>> watchMaterials(String childId) => _materials
      .where('childId', isEqualTo: childId)
      .orderBy('uploadedAt', descending: true)
      .snapshots()
      .map((q) => q.docs
          .map((d) => MaterialDoc.fromMap(d.id, _hydrate(d.data())))
          .toList());

  @override
  Future<String> uploadMaterial(
    MaterialDoc material, {
    Uint8List? fileBytes,
    String? fileName,
  }) async {
    String? fileUrl = material.fileUrl;
    if (fileBytes != null) {
      final path =
          'materials/${material.childId}/${DateTime.now().millisecondsSinceEpoch}_${fileName ?? 'upload'}';
      final ref = _storage.ref(path);
      await ref.putData(fileBytes);
      fileUrl = await ref.getDownloadURL();
    }

    final docRef =
        material.id.isEmpty ? _materials.doc() : _materials.doc(material.id);
    final toStore = MaterialDoc(
      id: docRef.id,
      childId: material.childId,
      subject: material.subject,
      title: material.title,
      items: material.items,
      fileUrl: fileUrl,
      uploadedAt: material.uploadedAt,
      enabled: material.enabled,
    );
    await docRef.set(toStore.toMap(), SetOptions(merge: true));
    return docRef.id;
  }

  @override
  Future<void> setMaterialEnabled(String materialId, bool enabled) =>
      _materials.doc(materialId).set(
            {'enabled': enabled},
            SetOptions(merge: true),
          );

  @override
  Future<void> deleteMaterial(MaterialDoc material) async {
    final fileUrl = material.fileUrl;
    if (fileUrl != null && fileUrl.isNotEmpty) {
      try {
        await _storage.refFromURL(fileUrl).delete();
      } on FirebaseException {
        // Storage object already gone (manual cleanup, etc.) — proceed
        // with the Firestore delete so the tile disappears from the list.
      }
    }
    await _materials.doc(material.id).delete();
  }

  // ── sessions ───────────────────────────────────────────────────────────
  @override
  Stream<List<Session>> watchSessions(String childId, {int limit = 50}) =>
      _sessions
          .where('childId', isEqualTo: childId)
          .orderBy('startedAt', descending: true)
          .limit(limit)
          .snapshots()
          .map((q) => q.docs
              .map((d) => Session.fromMap(d.id, _hydrate(d.data())))
              .toList());

  @override
  Stream<List<QuestionLog>> watchSessionQuestions(String sessionId) => _sessions
      .doc(sessionId)
      .collection(AppConstants.subColQuestions)
      .orderBy('askedAt')
      .snapshots()
      .map((q) => q.docs
          .map((d) => QuestionLog.fromMap(d.id, _hydrate(d.data())))
          .toList());
}
