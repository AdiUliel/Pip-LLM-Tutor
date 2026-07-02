// ReportsHubScreen — the single "דוחות" bottom-nav tab. Hosts two sub-tabs in
// a pill-style segmented TabBar: "דוחות" (session history) and "מגמות"
// (charts). Replaces the two former standalone bottom tabs.

import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../providers/child_provider.dart';
import '../theme.dart';
import '../widgets/screen_header.dart';
import 'reports_screen.dart';
import 'trends_screen.dart';

class ReportsHubScreen extends StatefulWidget {
  const ReportsHubScreen({super.key, this.subTab = 0, this.request = 0});

  /// Which sub-tab to show: 0 = דוחות (history), 1 = מגמות (trends).
  final int subTab;

  /// Bumped by the shell on every dashboard shortcut tap so we re-apply
  /// [subTab] even when its value matches the previous request.
  final int request;

  @override
  State<ReportsHubScreen> createState() => _ReportsHubScreenState();
}

class _ReportsHubScreenState extends State<ReportsHubScreen>
    with SingleTickerProviderStateMixin {
  late final TabController _tab;

  @override
  void initState() {
    super.initState();
    _tab = TabController(length: 2, vsync: this, initialIndex: widget.subTab);
  }

  @override
  void didUpdateWidget(ReportsHubScreen old) {
    super.didUpdateWidget(old);
    // Only jump when the shell issues a fresh request — leaves the user free to
    // swipe between sub-tabs the rest of the time.
    if (widget.request != old.request && _tab.index != widget.subTab) {
      _tab.index = widget.subTab;
    }
  }

  @override
  void dispose() {
    _tab.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final child = context.watch<ChildProvider>().child;
    return Scaffold(
      body: SafeArea(
        bottom: false,
        child: Column(
          children: [
            ScreenHeader(
              title: 'דוחות',
              subtitle: child == null ? null : 'מפגשים ומגמות · ${child.name}',
            ),
            _HubTabBar(controller: _tab),
            Expanded(
              child: TabBarView(
                controller: _tab,
                children: const [
                  ReportsScreen(embedded: true),
                  TrendsScreen(embedded: true),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }
}

/// Pill segmented control styled to match the rest of the app (sky accent).
class _HubTabBar extends StatelessWidget {
  const _HubTabBar({required this.controller});

  final TabController controller;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.fromLTRB(20, 0, 20, 10),
      child: Container(
        padding: const EdgeInsets.all(4),
        decoration: BoxDecoration(
          color: Colors.white,
          borderRadius: BorderRadius.circular(AppRadii.pill),
          border: Border.all(color: AppColors.skySoft, width: 2),
        ),
        child: TabBar(
          controller: controller,
          dividerColor: Colors.transparent,
          indicatorSize: TabBarIndicatorSize.tab,
          indicatorPadding: EdgeInsets.zero,
          indicator: BoxDecoration(
            color: AppColors.sky,
            borderRadius: BorderRadius.circular(AppRadii.pill),
          ),
          labelColor: Colors.white,
          unselectedLabelColor: AppColors.inkSoft,
          labelStyle:
              const TextStyle(fontWeight: FontWeight.w800, fontSize: 14),
          unselectedLabelStyle:
              const TextStyle(fontWeight: FontWeight.w700, fontSize: 14),
          tabs: const [
            Tab(text: 'דוחות'),
            Tab(text: 'מגמות'),
          ],
        ),
      ),
    );
  }
}
