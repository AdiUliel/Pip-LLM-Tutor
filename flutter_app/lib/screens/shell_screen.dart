// ShellScreen — top-level scaffold once a parent is authed AND a child
// profile exists. Hosts the 5 bottom-nav tabs in an IndexedStack so each
// tab's state survives switching.

import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../providers/device_provider.dart';
import '../widgets/bottom_nav.dart';
import '../widgets/offline_banner.dart';
import 'dashboard_screen.dart';
import 'device_monitor_screen.dart';
import 'material_upload_screen.dart';
import 'reports_hub_screen.dart';
import 'settings_screen.dart';

class ShellScreen extends StatefulWidget {
  const ShellScreen({super.key});

  @override
  State<ShellScreen> createState() => _ShellScreenState();
}

class _ShellScreenState extends State<ShellScreen> {
  int _index = 0;

  static const _tabOrder = [
    NavTab.dashboard,
    NavTab.reports,
    NavTab.material,
    NavTab.device,
    NavTab.settings,
  ];

  void _goTo(int i) => setState(() => _index = i);

  @override
  Widget build(BuildContext context) {
    final offline = !context.watch<DeviceProvider>().isOnline;

    final tabs = <Widget>[
      DashboardScreen(onNavigateToTab: _goTo),
      const ReportsHubScreen(),
      const MaterialUploadScreen(isRootTab: true),
      const DeviceMonitorScreen(),
      const SettingsScreen(),
    ];

    return Scaffold(
      body: Column(
        children: [
          const OfflineBanner(),
          Expanded(
            child: IndexedStack(index: _index, children: tabs),
          ),
        ],
      ),
      bottomNavigationBar: BottomNav(
        active: _tabOrder[_index],
        deviceOffline: offline,
        onChange: (t) => _goTo(_tabOrder.indexOf(t)),
      ),
    );
  }
}
