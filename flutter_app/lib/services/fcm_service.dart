// FcmService — owns push-notification setup for the parent app.
//
// Lifecycle:
//   - call attach(parentId) once the parent is signed in
//   - call detach(parentId) on sign-out
// In between, the service requests permission (first time only), fetches the
// FCM token, writes it to parents/{parentId}.fcmTokens, listens for refresh,
// and forwards foreground messages to a callback (used to show a SnackBar).
//
// Background / closed-app delivery is handled by the OS — no code needed
// here; the Cloud Functions in firebase/functions/notifications.js produce
// the notifications.

import 'dart:async';

import 'package:firebase_messaging/firebase_messaging.dart';
import 'package:flutter/foundation.dart';

import '../constants.dart';
import 'firebase_service.dart';

class FcmService {
  FcmService(this._fb);

  final FirebaseService _fb;
  final FirebaseMessaging _msg = FirebaseMessaging.instance;

  String? _parentId;
  String? _currentToken;
  StreamSubscription<String>? _refreshSub;
  StreamSubscription<RemoteMessage>? _foregroundSub;

  /// Called once a foreground message arrives — wire this to a SnackBar
  /// or in-app banner from the app shell. Optional.
  void Function(RemoteMessage)? onForegroundMessage;

  /// Request permission, fetch a token, and register it for [parentId].
  /// Safe to call multiple times — same parentId returns immediately;
  /// re-registering the same token is a no-op in Firestore (arrayUnion).
  Future<void> attach(String parentId) async {
    if (_parentId == parentId && _currentToken != null) return;
    _parentId = parentId;

    // iOS / web require an explicit permission request; on Android (API 33+)
    // requestPermission triggers the runtime notifications dialog too.
    final settings = await _msg.requestPermission(
      alert: true,
      badge: true,
      sound: true,
    );
    if (settings.authorizationStatus == AuthorizationStatus.denied) {
      debugPrint('[fcm] user denied notification permission');
      return;
    }

    try {
      final token = await _msg.getToken(
        vapidKey: AppConstants.fcmVapidKey.isEmpty
            ? null
            : AppConstants.fcmVapidKey,
      );
      if (token == null) {
        debugPrint('[fcm] no token returned (likely missing VAPID on web)');
        return;
      }
      _currentToken = token;
      await _fb.addParentFcmToken(parentId, token);
      debugPrint('[fcm] registered token for $parentId');
    } catch (e) {
      debugPrint('[fcm] getToken failed: $e');
    }

    _refreshSub?.cancel();
    _refreshSub = _msg.onTokenRefresh.listen((newToken) async {
      // Token rotated — register the new one, drop the old.
      if (_parentId != null) {
        if (_currentToken != null && _currentToken != newToken) {
          await _fb.removeParentFcmToken(_parentId!, _currentToken!);
        }
        await _fb.addParentFcmToken(_parentId!, newToken);
        _currentToken = newToken;
      }
    });

    _foregroundSub?.cancel();
    _foregroundSub = FirebaseMessaging.onMessage.listen((m) {
      onForegroundMessage?.call(m);
    });
  }

  /// Unregister the current token and stop listening.
  Future<void> detach() async {
    final parentId = _parentId;
    final token = _currentToken;
    _parentId = null;
    _currentToken = null;
    await _refreshSub?.cancel();
    _refreshSub = null;
    await _foregroundSub?.cancel();
    _foregroundSub = null;
    if (parentId != null && token != null) {
      try {
        await _fb.removeParentFcmToken(parentId, token);
      } catch (e) {
        debugPrint('[fcm] removeToken failed: $e');
      }
    }
  }
}
