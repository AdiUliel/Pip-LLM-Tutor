// Entry point — initialises shared_preferences, Firebase, the service layer,
// registers providers, and runs the app.

import 'dart:async';

import 'package:firebase_core/firebase_core.dart' hide FirebaseService;
import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'app.dart';
import 'firebase_options.dart';
import 'providers/auth_provider.dart';
import 'providers/child_provider.dart';
import 'providers/config_provider.dart';
import 'providers/device_provider.dart';
import 'providers/stats_provider.dart';
import 'utils/offline_queue.dart';
import 'services/device_sync_service.dart';
import 'services/device_sync_service_real.dart';
import 'services/firebase_service.dart';
import 'services/firebase_service_real.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();

  await Firebase.initializeApp(
    options: DefaultFirebaseOptions.currentPlatform,
  );

  final prefs = await SharedPreferences.getInstance();
  final config = ConfigProvider(prefs);

  final FirebaseService firebaseService = FirebaseServiceReal();
  final DeviceSyncService deviceSync = DeviceSyncServiceReal();

  final offlineQueue = OfflineQueue(prefs);
  // Drain any leftovers from a prior session. Failures stay queued.
  unawaited(offlineQueue.flush(firebaseService));

  runApp(
    MultiProvider(
      providers: [
        ChangeNotifierProvider.value(value: config),
        ChangeNotifierProvider.value(value: offlineQueue),
        Provider<FirebaseService>.value(value: firebaseService),
        Provider<DeviceSyncService>.value(value: deviceSync),
        ChangeNotifierProvider(
          create: (_) => AuthProvider(firebase: firebaseService),
        ),
        ChangeNotifierProvider(
          create: (_) => ChildProvider(firebaseService, prefs, offlineQueue: offlineQueue),
        ),
        ChangeNotifierProvider(create: (_) => DeviceProvider(deviceSync)),
        ChangeNotifierProvider(create: (_) => StatsProvider(firebaseService)),
      ],
      child: EmotionalTutorApp(navigatorKey: _navKey),
    ),
  );
}

final GlobalKey<NavigatorState> _navKey = GlobalKey<NavigatorState>();
