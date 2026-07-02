// App root — MaterialApp configured for Hebrew RTL + auth-gate routing that
// (a) shows the login screen when not authenticated,
// (b) shows the setup wizard when authenticated but no child profile exists,
// (c) shows the dashboard otherwise.

import 'package:flutter/material.dart';
import 'package:flutter_localizations/flutter_localizations.dart';
import 'package:provider/provider.dart';

import 'models/child.dart';
import 'providers/auth_provider.dart';
import 'providers/child_provider.dart';
import 'providers/device_provider.dart';
import 'providers/stats_provider.dart';
import 'screens/login_screen.dart';
import 'screens/setup_wizard_screen.dart';
import 'screens/shell_screen.dart';
import 'services/firebase_service.dart';
import 'theme.dart';

class EmotionalTutorApp extends StatelessWidget {
  const EmotionalTutorApp({super.key, this.navigatorKey});

  final GlobalKey<NavigatorState>? navigatorKey;

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Emotional Tutor',
      debugShowCheckedModeBanner: false,
      navigatorKey: navigatorKey,
      theme: buildAppTheme(),
      locale: const Locale('he'),
      supportedLocales: const [Locale('he'), Locale('en')],
      localizationsDelegates: const <LocalizationsDelegate<Object?>>[
        GlobalMaterialLocalizations.delegate,
        GlobalWidgetsLocalizations.delegate,
        GlobalCupertinoLocalizations.delegate,
      ],
      // RTL across the app regardless of the device locale.
      builder: (context, child) => Directionality(
        textDirection: TextDirection.rtl,
        child: child ?? const SizedBox.shrink(),
      ),
      home: const AuthGate(),
    );
  }
}

class AuthGate extends StatelessWidget {
  const AuthGate({super.key});

  @override
  Widget build(BuildContext context) {
    final auth = context.watch<AuthProvider>();
    if (!auth.isSignedIn) return const LoginScreen();
    return _ChildLoader(parentId: auth.user!.uid);
  }
}

/// Once a parent is signed in, watch their children list and route to
/// either the wizard (no children yet) or the dashboard (≥1 child).
class _ChildLoader extends StatelessWidget {
  const _ChildLoader({required this.parentId});
  final String parentId;

  @override
  Widget build(BuildContext context) {
    final fb = context.read<FirebaseService>();
    return StreamBuilder<List<Child>>(
      stream: fb.watchChildrenOfParent(parentId),
      builder: (context, snap) {
        if (snap.connectionState == ConnectionState.waiting) {
          return const _Loading();
        }
        final children = snap.data ?? const <Child>[];
        if (children.isEmpty) return const SetupWizardScreen();
        // Pick the saved active child if still present; otherwise fall back
        // to the first one. This survives across launches via prefs.
        final childProv = context.read<ChildProvider>();
        final savedId = childProv.activeChildId;
        final active = children.firstWhere(
          (c) => c.id == savedId,
          orElse: () => children.first,
        );
        WidgetsBinding.instance.addPostFrameCallback((_) {
          if (childProv.activeChildId != active.id || childProv.child == null) {
            childProv.setActive(active.id);
          }
          context.read<DeviceProvider>().watch(active.deviceId);
          context.read<StatsProvider>().load(active.id);
        });
        return const ShellScreen();
      },
    );
  }
}

class _Loading extends StatelessWidget {
  const _Loading();

  @override
  Widget build(BuildContext context) {
    return const Scaffold(
      body: Center(child: CircularProgressIndicator()),
    );
  }
}
