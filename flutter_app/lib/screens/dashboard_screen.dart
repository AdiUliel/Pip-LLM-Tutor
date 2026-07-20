// DashboardScreen — the "Home" tab. Device hero card, today-ring, last
// session, 2×2 quick-actions grid. Translates ~/Downloads/ioT/screen-dashboard.jsx.

import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../constants.dart';
import '../models/child.dart';
import '../providers/auth_provider.dart';
import '../providers/child_provider.dart';
import '../providers/config_provider.dart';
import '../providers/device_provider.dart';
import '../providers/stats_provider.dart';
import '../services/firebase_service.dart';
import '../theme.dart';
import '../utils/notifications.dart';
import '../widgets/dev_chip.dart';
import '../widgets/p_card.dart';
import '../widgets/pill.dart';
import '../widgets/robot_face.dart';
import '../widgets/screen_header.dart';
import 'child_config_screen.dart';
import 'setup_wizard_screen.dart';

class DashboardScreen extends StatelessWidget {
  const DashboardScreen({
    super.key,
    required this.onNavigateToTab,
    required this.onNavigateToReports,
  });

  /// Called when the user taps a tile that maps to another bottom-nav tab
  /// (reports, material, device monitor). The shell handles the actual swap.
  final void Function(int tabIndex) onNavigateToTab;

  /// Opens the Reports hub on a specific sub-tab (0 = דוחות, 1 = מגמות), rather
  /// than whatever sub-tab happened to be open last.
  final void Function(int subTab) onNavigateToReports;

  @override
  Widget build(BuildContext context) {
    final auth = context.watch<AuthProvider>();
    final child = context.watch<ChildProvider>().child;
    final device = context.watch<DeviceProvider>();
    final stats = context.watch<StatsProvider>();
    final config = context.watch<ConfigProvider>();

    if (child == null) {
      return const Center(child: CircularProgressIndicator());
    }

    return Scaffold(
      body: SafeArea(
        bottom: false,
        child: SingleChildScrollView(
          padding: const EdgeInsets.only(bottom: 24),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              ScreenHeader(
                title: 'שלום ${auth.user?.displayName ?? ''} 👋',
                subtitleWidget: _ChildSwitcher(active: child),
                right: Builder(builder: (innerCtx) {
                  final alerts = buildAlerts(
                    device: device,
                    stats: stats,
                    config: config,
                  );
                  return _NotificationsBell(
                    count: alerts.length,
                    onTap: () => _showAlerts(innerCtx, alerts),
                  );
                }),
              ),
              Padding(
                padding: const EdgeInsets.fromLTRB(20, 0, 20, 0),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    _DeviceHeroCard(
                      childName: child.name,
                      online: device.isOnline,
                      activeForThisChild: device.isActiveFor(child.id),
                      busyWithOther: device.isBusyWithOther(child.id),
                      activeChildName: device.activeChildName,
                      status: device.state?.status,
                      onTap: () => onNavigateToTab(NavTabIndex.device.index),
                    ),
                    const SizedBox(height: 14),
                    _TodayCard.fromStats(stats),
                    const SizedBox(height: 22),
                    _SectionTitle('המפגש האחרון'),
                    if (stats.lastSession != null)
                      _LastSessionCard(
                        session: stats.lastSession!,
                        onTap: () => onNavigateToTab(NavTabIndex.reports.index),
                      )
                    else
                      PCard(
                        child: Text(
                          'עוד אין מפגשים. ההתקן ישלח דיווח אוטומטית בסיום המפגש הראשון.',
                          style: AppTextStyles.hint(context),
                        ),
                      ),
                    const SizedBox(height: 22),
                    _SectionTitle('פעולות מהירות'),
                    _QuickActionsGrid(
                      onChildConfig: () => Navigator.of(context).push(
                        MaterialPageRoute(
                          builder: (_) => const ChildConfigScreen(),
                        ),
                      ),
                      onMaterials: () =>
                          onNavigateToTab(NavTabIndex.material.index),
                      // Trends lives as a sub-tab inside the Reports hub; each
                      // shortcut opens its own sub-tab (0 = דוחות, 1 = מגמות).
                      onReports: () => onNavigateToReports(0),
                      onTrends: () => onNavigateToReports(1),
                    ),
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

/// Stable indices matching the bottom-nav order so callers can pass them
/// to the shell without coupling to the NavTab enum.
enum NavTabIndex { dashboard, reports, material, device, settings }

class _NotificationsBell extends StatelessWidget {
  const _NotificationsBell({required this.onTap, required this.count});
  final VoidCallback onTap;
  final int count;

  @override
  Widget build(BuildContext context) {
    return InkWell(
      onTap: onTap,
      borderRadius: BorderRadius.circular(AppRadii.pill),
      child: Container(
        width: 44,
        height: 44,
        decoration: const BoxDecoration(
          color: Colors.white,
          shape: BoxShape.circle,
          boxShadow: AppShadow.soft,
        ),
        child: Stack(
          alignment: Alignment.center,
          children: [
            const Icon(Icons.notifications_outlined,
                color: AppColors.ink, size: 21),
            if (count > 0)
              Positioned(
                top: 9,
                right: 9,
                child: Container(
                  padding: EdgeInsets.symmetric(
                      horizontal: count > 9 ? 4 : 0, vertical: 0),
                  constraints: const BoxConstraints(minWidth: 14, minHeight: 14),
                  decoration: BoxDecoration(
                    color: AppColors.coral,
                    borderRadius: BorderRadius.circular(7),
                    border: Border.all(color: Colors.white, width: 1.5),
                  ),
                  alignment: Alignment.center,
                  child: Text(
                    count > 9 ? '9+' : '$count',
                    style: const TextStyle(
                      color: Colors.white,
                      fontWeight: FontWeight.w800,
                      fontSize: 9.5,
                      height: 1.1,
                    ),
                  ),
                ),
              ),
          ],
        ),
      ),
    );
  }
}

Future<void> _showAlerts(BuildContext context, List<AppAlert> alerts) async {
  await showModalBottomSheet<void>(
    context: context,
    showDragHandle: true,
    builder: (sheetCtx) {
      return SafeArea(
        child: Padding(
          padding: const EdgeInsets.fromLTRB(20, 4, 20, 20),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              Text(
                'התראות',
                textAlign: TextAlign.right,
                style: AppTextStyles.title(sheetCtx).copyWith(fontSize: 18),
              ),
              const SizedBox(height: 12),
              if (alerts.isEmpty)
                Padding(
                  padding: const EdgeInsets.symmetric(vertical: 30),
                  child: Column(
                    children: [
                      Icon(Icons.check_circle_outline_rounded,
                          color: AppColors.sky, size: 44),
                      const SizedBox(height: 8),
                      Text(
                        'אין התראות חדשות',
                        style: AppTextStyles.title(sheetCtx).copyWith(fontSize: 16),
                      ),
                      const SizedBox(height: 4),
                      Text(
                        'הכל תקין אצל הילד וההתקן.',
                        style: AppTextStyles.hint(sheetCtx),
                      ),
                    ],
                  ),
                )
              else
                for (final a in alerts) ...[
                  _AlertRow(alert: a),
                  const SizedBox(height: 10),
                ],
            ],
          ),
        ),
      );
    },
  );
}

class _AlertRow extends StatelessWidget {
  const _AlertRow({required this.alert});
  final AppAlert alert;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 12),
      decoration: BoxDecoration(
        color: alert.tint,
        borderRadius: BorderRadius.circular(AppRadii.md),
      ),
      child: Row(
        children: [
          Icon(alert.icon, color: alert.color, size: 22),
          const SizedBox(width: 12),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              mainAxisSize: MainAxisSize.min,
              children: [
                Text(
                  alert.title,
                  style: TextStyle(
                    fontWeight: FontWeight.w800,
                    fontSize: 14.5,
                    color: alert.color,
                  ),
                ),
                const SizedBox(height: 2),
                Text(
                  alert.sub,
                  style: TextStyle(
                    fontSize: 12.5,
                    color: AppColors.inkSoft,
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

class _DeviceHeroCard extends StatelessWidget {
  const _DeviceHeroCard({
    required this.childName,
    required this.online,
    required this.activeForThisChild,
    required this.busyWithOther,
    required this.activeChildName,
    required this.status,
    required this.onTap,
  });

  final String childName;
  final bool online;
  final bool activeForThisChild;
  final bool busyWithOther;
  final String? activeChildName;
  final DeviceStatus? status;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final emo = !online
        ? RobotEmotion.neutral
        : activeForThisChild
            ? (status == DeviceStatus.listening
                ? RobotEmotion.listening
                : RobotEmotion.happy)
            : busyWithOther
                ? RobotEmotion.neutral
                : RobotEmotion.happy;
    // Per-child separation: "connected + activity" ONLY when the device is
    // actually running THIS child's session; if a sibling is active, say so.
    final chipLabel = !online
        ? 'לא מחובר'
        : activeForThisChild
            ? 'מחובר · ${deviceStatusHe[status ?? DeviceStatus.idle] ?? 'במנוחה'}'
            : busyWithOther
                ? 'ההתקן פעיל עם ${activeChildName ?? 'ילד אחר'}'
                : 'מחובר · במנוחה';
    return PCard(
      onTap: onTap,
      borderColor: online ? AppColors.skySoft : AppColors.coralSoft,
      padding: const EdgeInsets.all(16),
      child: Row(
        children: [
          SizedBox(
            width: 78,
            height: 78,
            child: RobotFace(emotion: emo, size: 78),
          ),
          const SizedBox(width: 14),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              mainAxisSize: MainAxisSize.min,
              children: [
                Text(
                  childName,
                  style: AppTextStyles.title(context),
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                ),
                const SizedBox(height: 7),
                DevChip(
                  label: chipLabel,
                  state: activeForThisChild
                      ? DevChipState.online
                      : busyWithOther
                          ? DevChipState.searching
                          : online
                              ? DevChipState.online
                              : DevChipState.offline,
                ),
              ],
            ),
          ),
          const Icon(Icons.chevron_left, color: AppColors.inkSoft),
        ],
      ),
    );
  }
}

class _TodayCard extends StatelessWidget {
  const _TodayCard({
    required this.minutes,
    required this.sessionsCount,
    required this.subjects,
    required this.accuracyPct,
    required this.stars,
    required this.mood,
  });

  final int minutes;
  final int sessionsCount;
  final List<Subject> subjects;
  final int? accuracyPct;
  final int stars;
  final int? mood;

  /// Derive today's snapshot from the live stats provider — only sessions
  /// that started after midnight count.
  factory _TodayCard.fromStats(StatsProvider stats) {
    final now = DateTime.now();
    final startOfDay = DateTime(now.year, now.month, now.day);
    final today = stats.sessions
        .where((s) => !s.startedAt.isBefore(startOfDay))
        .toList();
    final subjects = <Subject>{for (final s in today) s.subject}.toList()
      ..sort((a, b) => a.index.compareTo(b.index));
    final totalQ = today.fold<int>(0, (a, s) => a + s.questionsAsked);
    final correctQ = today.fold<int>(0, (a, s) => a + s.correctCount);
    final acc = totalQ == 0 ? null : (correctQ * 100 / totalQ).round();
    final starsTotal = today.fold<int>(0, (a, s) => a + s.starsEarned);
    // Mood: use the most recent of today's sessions; null if no sessions today.
    final mood = today.isEmpty ? null : today.first.moodSummary;
    return _TodayCard(
      minutes: stats.minutesToday,
      sessionsCount: today.length,
      subjects: subjects,
      accuracyPct: acc,
      stars: starsTotal,
      mood: mood,
    );
  }

  @override
  Widget build(BuildContext context) {
    return PCard(
      padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text('סיכום היום',
              style: AppTextStyles.title(context).copyWith(fontSize: 15.5)),
          const SizedBox(height: 8),
          if (sessionsCount == 0)
            Text(
              'עוד לא היו מפגשים היום',
              style: AppTextStyles.hint(context).copyWith(fontSize: 13.5),
            )
          else ...[
            Row(
              crossAxisAlignment: CrossAxisAlignment.end,
              children: [
                Text(
                  '$minutes',
                  style: AppTextStyles.display(context).copyWith(
                    fontSize: 32,
                    color: AppColors.sky,
                  ),
                ),
                const SizedBox(width: 4),
                Padding(
                  padding: const EdgeInsets.only(bottom: 6),
                  child: Text(
                    'דקות',
                    style:
                        AppTextStyles.hint(context).copyWith(fontSize: 14),
                  ),
                ),
                const SizedBox(width: 14),
                Expanded(
                  child: Padding(
                    padding: const EdgeInsets.only(bottom: 6),
                    child: Text(
                      '$sessionsCount ${sessionsCount == 1 ? 'מפגש' : 'מפגשים'}'
                      '${accuracyPct != null ? ' · $accuracyPct% דיוק' : ''}',
                      style: AppTextStyles.hint(context)
                          .copyWith(fontSize: 13.5),
                    ),
                  ),
                ),
              ],
            ),
            const SizedBox(height: 12),
            Wrap(
              spacing: 8,
              runSpacing: 8,
              children: [
                for (final s in subjects)
                  Pill(
                    label: subjectMeta[s]!.heLabel,
                    color: subjectMeta[s]!.ink,
                    background: subjectMeta[s]!.tint,
                    icon: Text(
                      subjectMeta[s]!.emoji,
                      style: const TextStyle(fontSize: 13),
                    ),
                  ),
                if (stars > 0)
                  Pill(
                    label: '$stars ${stars == 1 ? 'כוכב' : 'כוכבים'}',
                    color: const Color(0xFFC98A12),
                    background: AppColors.sunSoft,
                    icon: const Icon(Icons.star,
                        color: Color(0xFFE6A91E), size: 13),
                  ),
                if (mood != null)
                  Pill(
                    label: MoodScale.heLabel[mood!] ?? '',
                    icon: MoodDot(color: MoodScale.color[mood!]!),
                  ),
              ],
            ),
          ],
        ],
      ),
    );
  }
}

class _LastSessionCard extends StatelessWidget {
  const _LastSessionCard({required this.session, required this.onTap});
  final dynamic session; // Session — avoid circular import for stub layouts
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final s = session;
    final meta = subjectMeta[s.subject]!;
    final dateLabel = _heDate(s.startedAt);
    return PCard(
      onTap: onTap,
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 14),
      child: Row(
        children: [
          Container(
            width: 50,
            height: 50,
            decoration: BoxDecoration(
              color: meta.tint,
              borderRadius: BorderRadius.circular(16),
            ),
            alignment: Alignment.center,
            child: Text(meta.emoji, style: const TextStyle(fontSize: 26)),
          ),
          const SizedBox(width: 12),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              mainAxisSize: MainAxisSize.min,
              children: [
                Text('${meta.heLabel} · $dateLabel',
                    style: AppTextStyles.title(context).copyWith(fontSize: 15.5)),
                const SizedBox(height: 2),
                Text(
                  '${s.correctCount}/${s.questionsAsked} נכון · ${s.durationMinutes} דק׳ · רצף ${s.longestStreak}',
                  style: AppTextStyles.hint(context).copyWith(fontSize: 13),
                ),
              ],
            ),
          ),
          Column(
            children: [
              Text(
                '${s.accuracyPct}%',
                style: AppTextStyles.display(context).copyWith(
                  fontSize: 22,
                  color: AppColors.mint,
                ),
              ),
              Text('דיוק',
                  style: AppTextStyles.hint(context).copyWith(fontSize: 11)),
            ],
          ),
        ],
      ),
    );
  }

  static String _heDate(DateTime d) {
    final now = DateTime.now();
    final today = DateTime(now.year, now.month, now.day);
    final that = DateTime(d.year, d.month, d.day);
    final daysAgo = today.difference(that).inDays;
    if (daysAgo == 0) return 'היום';
    if (daysAgo == 1) return 'אתמול';
    const dayNames = ['ראשון', 'שני', 'שלישי', 'רביעי', 'חמישי', 'שישי', 'שבת'];
    return 'יום ${dayNames[d.weekday % 7]}';
  }
}

class _SectionTitle extends StatelessWidget {
  const _SectionTitle(this.text);
  final String text;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.fromLTRB(2, 0, 2, 10),
      child: Text(
        text,
        style: AppTextStyles.title(context).copyWith(fontSize: 16),
      ),
    );
  }
}

class _QuickActionsGrid extends StatelessWidget {
  const _QuickActionsGrid({
    required this.onChildConfig,
    required this.onMaterials,
    required this.onReports,
    required this.onTrends,
  });
  final VoidCallback onChildConfig;
  final VoidCallback onMaterials;
  final VoidCallback onReports;
  final VoidCallback onTrends;

  @override
  Widget build(BuildContext context) {
    final items = [
      _Action(
        icon: Icons.face_outlined,
        label: 'מה לומדים',
        sub: 'מקצועות ורמה',
        tint: AppColors.skySoft,
        ink: AppColors.sky,
        onTap: onChildConfig,
      ),
      _Action(
        icon: Icons.upload_outlined,
        label: 'חומרי לימוד',
        sub: 'שיעורי בית',
        tint: AppColors.sunSoft,
        ink: const Color(0xFFC98A12),
        onTap: onMaterials,
      ),
      _Action(
        icon: Icons.description_outlined,
        label: 'דוחות',
        sub: 'היסטוריית מפגשים',
        tint: AppColors.mintSoft,
        ink: const Color(0xFF1E9C7E),
        onTap: onReports,
      ),
      _Action(
        icon: Icons.trending_up_rounded,
        label: 'מגמות',
        sub: 'מצב רוח והתקדמות',
        tint: const Color(0xFFECE5FB),
        ink: AppColors.grape,
        onTap: onTrends,
      ),
    ];
    return GridView.count(
      crossAxisCount: 2,
      crossAxisSpacing: 10,
      mainAxisSpacing: 10,
      childAspectRatio: 2.3,
      shrinkWrap: true,
      physics: const NeverScrollableScrollPhysics(),
      children: [
        for (final a in items)
          PCard(
            onTap: a.onTap,
            padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
            child: Row(
              children: [
                Container(
                  width: 38,
                  height: 38,
                  decoration: BoxDecoration(
                    color: a.tint,
                    borderRadius: BorderRadius.circular(11),
                  ),
                  alignment: Alignment.center,
                  child: Icon(a.icon, color: a.ink, size: 20),
                ),
                const SizedBox(width: 10),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      Text(
                        a.label,
                        style: AppTextStyles.title(context)
                            .copyWith(fontSize: 14),
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                      ),
                      const SizedBox(height: 1),
                      Text(
                        a.sub,
                        style: AppTextStyles.hint(context)
                            .copyWith(fontSize: 11.5),
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                      ),
                    ],
                  ),
                ),
              ],
            ),
          ),
      ],
    );
  }
}

class _Action {
  final IconData icon;
  final String label;
  final String sub;
  final Color tint;
  final Color ink;
  final VoidCallback onTap;
  _Action({
    required this.icon,
    required this.label,
    required this.sub,
    required this.tint,
    required this.ink,
    required this.onTap,
  });
}


class _ChildSwitcher extends StatelessWidget {
  const _ChildSwitcher({required this.active});
  final Child active;

  @override
  Widget build(BuildContext context) {
    return InkWell(
      onTap: () => _openSheet(context),
      borderRadius: BorderRadius.circular(AppRadii.pill),
      child: Padding(
        padding: const EdgeInsets.symmetric(vertical: 2),
        child: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            Flexible(
              child: Text(
                'הנה מה שקורה אצל ${active.name}',
                style: AppTextStyles.hint(context).copyWith(fontSize: 14),
                overflow: TextOverflow.ellipsis,
              ),
            ),
            const SizedBox(width: 4),
            const Icon(
              Icons.expand_more_rounded,
              size: 18,
              color: AppColors.sky,
            ),
          ],
        ),
      ),
    );
  }

  Future<void> _confirmDeleteChild(
    BuildContext ctx,
    BuildContext sheetCtx,
    Child target,
    List<Child> all,
    Child active,
  ) async {
    final messenger = ScaffoldMessenger.of(ctx);
    final fb = ctx.read<FirebaseService>();
    final childProv = ctx.read<ChildProvider>();
    final deviceProv = ctx.read<DeviceProvider>();
    final statsProv = ctx.read<StatsProvider>();

    final ok = await showDialog<bool>(
      context: sheetCtx,
      builder: (dCtx) => AlertDialog(
        title: const Text('מחיקת ילד'),
        content: Text(
          'למחוק את ${target.name}? פעולה זו אינה הפיכה.',
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.of(dCtx).pop(false),
            child: const Text('ביטול'),
          ),
          TextButton(
            style: TextButton.styleFrom(foregroundColor: AppColors.coral),
            onPressed: () => Navigator.of(dCtx).pop(true),
            child: const Text('מחיקה'),
          ),
        ],
      ),
    );
    if (ok != true) return;

    try {
      // If we're deleting the currently-active child, hand off to a sibling
      // first so the dashboard never tries to read a deleted doc.
      if (target.id == active.id) {
        final next = all.firstWhere((c) => c.id != target.id);
        await childProv.setActive(next.id);
        deviceProv.watch(next.deviceId);
        statsProv.load(next.id);
      }
      await fb.deleteChild(target.id);
    } catch (e) {
      messenger.showSnackBar(
        SnackBar(content: Text('המחיקה נכשלה: $e')),
      );
    }
  }

  Future<void> _openSheet(BuildContext context) async {
    final auth = context.read<AuthProvider>();
    final fb = context.read<FirebaseService>();
    final parentId = auth.user?.uid;
    if (parentId == null) return;
    await showModalBottomSheet<void>(
      context: context,
      showDragHandle: true,
      builder: (sheetCtx) {
        return SafeArea(
          child: StreamBuilder<List<Child>>(
            stream: fb.watchChildrenOfParent(parentId),
            builder: (ctx, snap) {
              final list = snap.data ?? [active];
              return Column(
                mainAxisSize: MainAxisSize.min,
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  Padding(
                    padding: const EdgeInsets.fromLTRB(20, 4, 20, 8),
                    child: Text(
                      'הילדים שלי',
                      textAlign: TextAlign.right,
                      style: AppTextStyles.title(context).copyWith(fontSize: 16),
                    ),
                  ),
                  for (final c in list)
                    ListTile(
                      leading: CircleAvatar(
                        backgroundColor:
                            c.id == active.id ? AppColors.sky : AppColors.skySoft,
                        foregroundColor:
                            c.id == active.id ? Colors.white : AppColors.sky,
                        child: Text(genderEmoji[c.gender] ?? '🙂'),
                      ),
                      title: Text(
                        c.name,
                        textAlign: TextAlign.right,
                        style: TextStyle(
                          fontWeight: FontWeight.w800,
                          color: AppColors.ink,
                        ),
                      ),
                      subtitle: Text(
                        'גיל ${c.age}',
                        textAlign: TextAlign.right,
                        style: AppTextStyles.hint(context).copyWith(fontSize: 12.5),
                      ),
                      trailing: Row(
                        mainAxisSize: MainAxisSize.min,
                        children: [
                          if (list.length > 1)
                            IconButton(
                              onPressed: () => _confirmDeleteChild(
                                  ctx, sheetCtx, c, list, active),
                              icon: const Icon(
                                Icons.delete_outline_rounded,
                                color: AppColors.coral,
                                size: 22,
                              ),
                              tooltip: 'מחיקה',
                              padding: EdgeInsets.zero,
                              constraints: const BoxConstraints(
                                  minWidth: 36, minHeight: 36),
                            ),
                          if (c.id == active.id)
                            const Padding(
                              padding: EdgeInsets.only(right: 4, left: 4),
                              child: Icon(Icons.check_circle,
                                  color: AppColors.sky),
                            ),
                        ],
                      ),
                      onTap: () async {
                        Navigator.of(sheetCtx).pop();
                        if (c.id == active.id) return;
                        await ctx.read<ChildProvider>().setActive(c.id);
                        // ignore: use_build_context_synchronously
                        ctx.read<DeviceProvider>().watch(c.deviceId);
                        // ignore: use_build_context_synchronously
                        ctx.read<StatsProvider>().load(c.id);
                      },
                    ),
                  const Divider(height: 1),
                  ListTile(
                    leading: const CircleAvatar(
                      backgroundColor: AppColors.skySoft,
                      foregroundColor: AppColors.sky,
                      child: Icon(Icons.add),
                    ),
                    title: const Text(
                      'הוסף ילד',
                      textAlign: TextAlign.right,
                      style: TextStyle(
                        fontWeight: FontWeight.w800,
                        color: AppColors.sky,
                      ),
                    ),
                    onTap: () {
                      Navigator.of(sheetCtx).pop();
                      Navigator.of(ctx).push(
                        MaterialPageRoute(
                          builder: (_) => const SetupWizardScreen(),
                        ),
                      );
                    },
                  ),
                  const SizedBox(height: 8),
                ],
              );
            },
          ),
        );
      },
    );
  }
}
