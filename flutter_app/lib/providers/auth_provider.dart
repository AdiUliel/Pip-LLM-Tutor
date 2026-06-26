// AuthProvider — wraps Firebase Auth. The authStateChanges() stream keeps
// [_user] and [_status] in sync after sign-in / sign-out.

import 'package:firebase_auth/firebase_auth.dart' as fb;
import 'package:flutter/foundation.dart';

import '../models/parent_user.dart';
import '../services/firebase_service.dart';

enum AuthStatus { unauthenticated, authenticating, authenticated, error }

class CurrentParent {
  final String uid;
  final String email;
  final String? displayName;
  const CurrentParent({required this.uid, required this.email, this.displayName});
}

class AuthProvider extends ChangeNotifier {
  AuthProvider({required this.firebase}) {
    _sub = fb.FirebaseAuth.instance.authStateChanges().listen((u) {
      if (u == null) {
        _user = null;
        _status = AuthStatus.unauthenticated;
      } else {
        _user = CurrentParent(
          uid: u.uid,
          email: u.email ?? '',
          displayName: u.displayName,
        );
        _status = AuthStatus.authenticated;
      }
      notifyListeners();
    });
  }

  final FirebaseService firebase;

  AuthStatus _status = AuthStatus.unauthenticated;
  CurrentParent? _user;
  String? _errorMessage;
  // ignore: cancel_subscriptions
  // ^ disposed in [dispose]
  // ignore: prefer_final_fields
  var _sub = const Stream<void>.empty().listen((_) {});

  AuthStatus get status => _status;
  CurrentParent? get user => _user;
  String? get errorMessage => _errorMessage;
  bool get isSignedIn => _status == AuthStatus.authenticated && _user != null;

  Future<void> signIn({
    required String email,
    required String password,
  }) async {
    _status = AuthStatus.authenticating;
    _errorMessage = null;
    notifyListeners();

    try {
      await fb.FirebaseAuth.instance
          .signInWithEmailAndPassword(email: email, password: password);
      // authStateChanges() listener will set _user + status.
    } on fb.FirebaseAuthException catch (e) {
      _errorMessage = _heFromAuthError(e.code);
      _status = AuthStatus.error;
    } catch (_) {
      _errorMessage = 'אירעה שגיאה לא צפויה';
      _status = AuthStatus.error;
    }
    notifyListeners();
  }

  Future<void> signUp({
    required String name,
    required String email,
    required String password,
  }) async {
    _status = AuthStatus.authenticating;
    _errorMessage = null;
    notifyListeners();

    try {
      final cred = await fb.FirebaseAuth.instance
          .createUserWithEmailAndPassword(email: email, password: password);
      await cred.user?.updateDisplayName(name);
      if (cred.user != null) {
        await firebase.saveParent(ParentUser(
          id: cred.user!.uid,
          name: name,
          email: email,
          createdAt: DateTime.now(),
        ));
      }
      // authStateChanges() will fire; we set displayName explicitly here in case
      // the listener races us.
      _user = CurrentParent(
        uid: cred.user!.uid,
        email: email,
        displayName: name,
      );
      _status = AuthStatus.authenticated;
    } on fb.FirebaseAuthException catch (e) {
      _errorMessage = _heFromAuthError(e.code);
      _status = AuthStatus.error;
    } catch (_) {
      _errorMessage = 'אירעה שגיאה לא צפויה';
      _status = AuthStatus.error;
    }
    notifyListeners();
  }

  Future<void> signOut() async {
    await fb.FirebaseAuth.instance.signOut();
    _user = null;
    _status = AuthStatus.unauthenticated;
    _errorMessage = null;
    notifyListeners();
  }

  /// Sends a Firebase password-reset email. Returns null on success or the
  /// localised Hebrew error message on failure.
  Future<String?> resetPassword(String email) async {
    try {
      await fb.FirebaseAuth.instance.sendPasswordResetEmail(email: email);
      return null;
    } on fb.FirebaseAuthException catch (e) {
      return _heFromAuthError(e.code);
    } catch (_) {
      return 'אירעה שגיאה לא צפויה';
    }
  }

  @override
  void dispose() {
    _sub.cancel();
    super.dispose();
  }

  String _heFromAuthError(String code) {
    switch (code) {
      case 'invalid-email':
        return 'כתובת אימייל לא תקינה';
      case 'user-not-found':
      case 'wrong-password':
      case 'invalid-credential':
        return 'אימייל או סיסמה שגויים';
      case 'email-already-in-use':
        return 'האימייל כבר רשום במערכת';
      case 'weak-password':
        return 'הסיסמה חלשה מדי (לפחות 6 תווים)';
      case 'network-request-failed':
        return 'אין חיבור לאינטרנט';
      default:
        return 'שגיאת התחברות';
    }
  }
}
