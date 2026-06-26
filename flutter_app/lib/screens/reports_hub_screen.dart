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

class ReportsHubScreen extends StatelessWidget {
  const ReportsHubScreen({super.key});

  @override
  Widget build(BuildContext context) {
    final child = context.watch<ChildProvider>().child;
    return DefaultTabController(
      length: 2,
      child: Scaffold(
        body: SafeArea(
          bottom: false,
          child: Column(
            children: [
              ScreenHeader(
                title: 'דוחות',
                subtitle:
                    child == null ? null : 'מפגשים ומגמות · ${child.name}',
              ),
              const _HubTabBar(),
              const Expanded(
                child: TabBarView(
                  children: [
                    ReportsScreen(embedded: true),
                    TrendsScreen(embedded: true),
                  ],
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

/// Pill segmented control styled to match the rest of the app (sky accent).
class _HubTabBar extends StatelessWidget {
  const _HubTabBar();

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
