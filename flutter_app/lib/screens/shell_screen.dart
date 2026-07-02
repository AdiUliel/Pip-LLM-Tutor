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

  // Which Reports sub-tab to show (0 = דוחות, 1 = מגמות) and a request counter
  // bumped on every dashboard shortcut tap so the hub re-applies it even when
  // the value is unchanged.
  int _reportsTab = 0;
  int _reportsReq = 0;

  static const _tabOrder = [
    NavTab.dashboard,
    NavTab.reports,
    NavTab.material,
    NavTab.device,
    NavTab.settings,
  ];

  void _goTo(int i) => setState(() => _index = i);

  void _goToReports(int subTab) => setState(() {
        _index = _tabOrder.indexOf(NavTab.reports);
        _reportsTab = subTab;
        _reportsReq++;
      });

  @override
  Widget build(BuildContext context) {
    final offline = !context.watch<DeviceProvider>().isOnline;

    final tabs = <Widget>[
      DashboardScreen(
        onNavigateToTab: _goTo,
        onNavigateToReports: _goToReports,
      ),
      ReportsHubScreen(subTab: _reportsTab, request: _reportsReq),
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
        // Tapping the דוחות tab always resets it to the history sub-tab rather
        // than restoring whatever sub-tab was open last.
        onChange: (t) =>
            t == NavTab.reports ? _goToReports(0) : _goTo(_tabOrder.indexOf(t)),
      ),
    );
  }
}
