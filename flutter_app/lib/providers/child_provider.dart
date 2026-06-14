// ChildProvider — holds the currently-active child profile and proxies
// edits to FirebaseService. Step 3+ screens consume this instead of touching
// the service directly. On a Firestore write failure (offline / quota),
// queues the op via [OfflineQueue] and reports success so the UI moves on.
//
// Multi-child: the currently-active child id is persisted to
// SharedPreferences so the same child is restored across launches. The
// AuthGate (lib/app.dart) reconciles this against the parent's actual
// children list, falling back to the first child if the saved id is stale.

import 'dart:async';

import 'package:cloud_firestore/cloud_firestore.dart';
import 'package:flutter/foundation.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../models/child.dart';
import '../services/firebase_service.dart';
import '../utils/offline_queue.dart';

class ChildProvider extends ChangeNotifier {
  ChildProvider(this._fb, this._prefs, {OfflineQueue? offlineQueue})
      : _queue = offlineQueue {
    _activeChildId = _prefs.getString(_kActive);
  }

  final FirebaseService _fb;
  final SharedPreferences _prefs;
  final OfflineQueue? _queue;

  static const _kActive = 'childProvider.activeChildId';

  Child? _child;
  String? _activeChildId;
  StreamSubscription<Child?>? _sub;

  Child? get child => _child;
  String? get activeChildId => _activeChildId;

  /// Switch to a different child, persisting the choice for next launch.
  Future<void> setActive(String childId) async {
    if (_activeChildId == childId && _sub != null) return;
    _activeChildId = childId;
    await _prefs.setString(_kActive, childId);
    load(childId);
  }

  void load(String childId) {
    _sub?.cancel();
    _sub = _fb.watchChild(childId).listen((c) {
      _child = c;
      notifyListeners();
    });
  }

  Future<String> save(Child child) async {
    try {
      return await _fb.saveChild(child);
    } on FirebaseException catch (_) {
      await _queue?.enqueueSaveChild(child);
      return child.id; // optimistic — actual write replays later
    }
  }

  Future<void> update(Child updated) async {
    await _fb.saveChild(updated);
  }

  void clear() {
    _sub?.cancel();
    _sub = null;
    _child = null;
    _activeChildId = null;
    _prefs.remove(_kActive);
    notifyListeners();
  }

  @override
  void dispose() {
    _sub?.cancel();
    super.dispose();
  }
}
