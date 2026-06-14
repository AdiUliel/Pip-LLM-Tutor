// OfflineQueue — minimal FIFO of pending writes that failed because the
// network/Firebase was unreachable. Persisted in shared_preferences as a
// JSON list. Currently handles `saveChild` operations; file uploads
// require connectivity for the Storage put and are NOT queued.
//
// Used by FirebaseServiceReal: write fails → enqueue → app keeps moving.
// On next startup (or when the parent taps "נסה לסנכרן עכשיו" in the
// banner) we attempt to drain the queue.

import 'dart:async';
import 'dart:convert';

import 'package:flutter/foundation.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../constants.dart';
import '../models/child.dart';
import '../services/firebase_service.dart';

// Accept both new (List) and legacy (String) shapes for queued payloads.
List<String> _topicListFromValue(dynamic v) {
  if (v is List) return v.whereType<String>().toList();
  if (v is String && v.isNotEmpty) return [v];
  return const [];
}

class QueuedOp {
  final String id;
  final String type; // 'saveChild' for now
  final Map<String, dynamic> payload;
  QueuedOp(this.id, this.type, this.payload);

  Map<String, dynamic> toJson() => {'id': id, 'type': type, 'payload': payload};
  factory QueuedOp.fromJson(Map<String, dynamic> m) => QueuedOp(
        m['id'] as String,
        m['type'] as String,
        (m['payload'] as Map).cast<String, dynamic>(),
      );
}

class OfflineQueue extends ChangeNotifier {
  OfflineQueue(this._prefs);

  final SharedPreferences _prefs;
  static const _kKey = 'offline.queue.v1';

  List<QueuedOp> _items = [];
  bool _loaded = false;

  int get pendingCount => _items.length;
  bool get hasPending => _items.isNotEmpty;

  void _load() {
    if (_loaded) return;
    _loaded = true;
    final raw = _prefs.getString(_kKey);
    if (raw == null) return;
    try {
      final list = (jsonDecode(raw) as List).cast<Map<String, dynamic>>();
      _items = list.map(QueuedOp.fromJson).toList();
    } catch (_) {
      _items = [];
    }
  }

  Future<void> _persist() async {
    await _prefs.setString(
      _kKey,
      jsonEncode([for (final o in _items) o.toJson()]),
    );
    notifyListeners();
  }

  Future<void> enqueueSaveChild(Child child) async {
    _load();
    if (_items.length >= AppConstants.offlineQueueMaxOps) {
      _items.removeAt(0); // drop oldest
    }
    _items.add(QueuedOp(
      '${DateTime.now().millisecondsSinceEpoch}',
      'saveChild',
      _childToJson(child),
    ));
    await _persist();
  }

  /// Try to send every queued op. Drops successes, keeps failures.
  /// Returns the number of ops still pending after the flush.
  Future<int> flush(FirebaseService fb) async {
    _load();
    final keep = <QueuedOp>[];
    for (final op in _items) {
      try {
        if (op.type == 'saveChild') {
          await fb.saveChild(_childFromJson(op.payload));
        }
      } catch (_) {
        keep.add(op);
      }
    }
    _items = keep;
    await _persist();
    return _items.length;
  }

  // Serialisation helpers (kept inline since the queue is short-lived).

  Map<String, dynamic> _childToJson(Child c) => {
        'id': c.id,
        'parentId': c.parentId,
        'name': c.name,
        'age': c.age,
        'gender': genderId[c.gender],
        'subjectsEnabled': [for (final s in c.subjectsEnabled) subjectMeta[s]!.id],
        'topicFocus': {
          for (final e in c.topicFocus.entries) subjectMeta[e.key]!.id: e.value,
        },
        'level': {
          for (final e in c.level.entries) subjectMeta[e.key]!.id: e.value,
        },
        'settings': c.settings.toMap(),
        'deviceId': c.deviceId,
        'createdAt': c.createdAt.toIso8601String(),
      };

  Child _childFromJson(Map<String, dynamic> m) {
    return Child(
      id: (m['id'] ?? '') as String,
      parentId: (m['parentId'] ?? '') as String,
      name: (m['name'] ?? '') as String,
      age: (m['age'] as num?)?.toInt() ?? AppConstants.defaultChildAge,
      gender: genderFromId(m['gender'] as String?),
      subjectsEnabled: [
        for (final id in (m['subjectsEnabled'] as List? ?? const []).cast<String>())
          if (subjectFromId(id) != null) subjectFromId(id)!,
      ],
      topicFocus: {
        for (final e in ((m['topicFocus'] as Map?)?.cast<String, dynamic>() ?? const {}).entries)
          if (subjectFromId(e.key) != null)
            subjectFromId(e.key)!: _topicListFromValue(e.value),
      },
      level: {
        for (final e in ((m['level'] as Map?)?.cast<String, dynamic>() ?? const {}).entries)
          if (subjectFromId(e.key) != null)
            subjectFromId(e.key)!: (e.value as num?)?.toInt() ?? 1,
      },
      settings: ChildSettings.fromMap(
        (m['settings'] as Map?)?.cast<String, dynamic>() ?? const {},
      ),
      deviceId: (m['deviceId'] ?? '') as String,
      createdAt:
          DateTime.tryParse(m['createdAt'] as String? ?? '') ?? DateTime.now(),
    );
  }
}
