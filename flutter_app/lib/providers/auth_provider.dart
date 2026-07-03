// AuthProvider — wraps Firebase Auth. The authStateChanges() stream keeps
// [_user] and [_status] in sync after sign-in / sign-out.

import 'package:firebase_auth/firebase_auth.dart' as fb;
import 'package:flutter/foundation.dart';
import 'package:google_sign_in/google_sign_in.dart';

import '../models/parent_user.dart';
import '../services/firebase_service.dart';

/// Web OAuth client ID for the Firebase project (`llm-tutor-d721e`). Required on
/// Android as `serverClientId` so Google returns a Firebase-usable idToken.
/// Populate after enabling the Google provider in the Firebase Console
/// (Project Settings → Web app → OAuth client ID). Unused on Web.
const String _kGoogleWebClientId =
    '233818904977-9i9has8t4uu9n2oalsv2c5qechilnsph.apps.googleusercontent.com';

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

  /// Sign in with a Google account. On Web this uses Firebase's native popup
  /// flow; on Android it runs the google_sign_in picker and exchanges the
  /// credential with Firebase. First-time Google users get a `parents/{uid}`
  /// doc; the authStateChanges() listener sets [_user] + [_status] on success.
  Future<void> signInWithGoogle() async {
    _status = AuthStatus.authenticating;
    _errorMessage = null;
    notifyListeners();

    try {
      final fb.UserCredential cred;
      if (kIsWeb) {
        cred = await fb.FirebaseAuth.instance
            .signInWithPopup(fb.GoogleAuthProvider());
      } else {
        final gUser =
            await GoogleSignIn(serverClientId: _kGoogleWebClientId).signIn();
        if (gUser == null) {
          // User dismissed the account picker — restore prior state, no error.
          _status =
              _user != null ? AuthStatus.authenticated : AuthStatus.unauthenticated;
          notifyListeners();
          return;
        }
        final gAuth = await gUser.authentication;
        cred = await fb.FirebaseAuth.instance.signInWithCredential(
          fb.GoogleAuthProvider.credential(
            idToken: gAuth.idToken,
            accessToken: gAuth.accessToken,
          ),
        );
      }

      // First-time Google user → create the parent doc (set(merge:true), safe).
      if (cred.additionalUserInfo?.isNewUser == true && cred.user != null) {
        await firebase.saveParent(ParentUser(
          id: cred.user!.uid,
          name: cred.user!.displayName ?? '',
          email: cred.user!.email ?? '',
          createdAt: DateTime.now(),
        ));
      }
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

  Future<void> signOut() async {
    if (!kIsWeb) {
      // Clear the cached Google account so the picker reappears next time.
      await GoogleSignIn().signOut();
    }
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
      case 'account-exists-with-different-credential':
        return 'האימייל כבר רשום עם סיסמה. התחברו עם אימייל וסיסמה';
      case 'weak-password':
        return 'הסיסמה חלשה מדי (לפחות 6 תווים)';
      case 'network-request-failed':
        return 'אין חיבור לאינטרנט';
      default:
        return 'שגיאת התחברות';
    }
  }
}
