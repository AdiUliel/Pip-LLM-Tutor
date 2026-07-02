// ReportsScreen — list of past sessions, filterable by subject. Tap a row to
// open the per-question SessionDetailScreen. Layout from
// ~/Downloads/ioT/screen-reports.jsx.

import 'package:flutter/material.dart';
import 'package:intl/intl.dart';
import 'package:provider/provider.dart';

import '../constants.dart';
import '../models/session.dart';
import '../providers/child_provider.dart';
import '../providers/stats_provider.dart';
import '../theme.dart';
import '../widgets/p_card.dart';
import '../widgets/pill.dart';
import '../widgets/screen_header.dart';
import 'session_detail_screen.dart';

class ReportsScreen extends StatefulWidget {
  const ReportsScreen({super.key, this.embedded = false});

  /// When true the screen is hosted inside [ReportsHubScreen]'s TabBarView, so
  /// it renders without its own header/Scaffold (the hub supplies them).
  final bool embedded;

  @override
  State<ReportsScreen> createState() => _ReportsScreenState();
}

class _ReportsScreenState extends State<ReportsScreen> {
  Subject? _filter; // null == all

  @override
  Widget build(BuildContext context) {
    final child = context.watch<ChildProvider>().child;
    final all = context.watch<StatsProvider>().sessions;
    final filtered = _filter == null
        ? all
        : all.where((s) => s.subject == _filter).toList();
    final content = Column(
          children: [
            if (!widget.embedded)
              ScreenHeader(
                title: 'דוחות',
                subtitle: child == null
                    ? null
                    : 'היסטוריית המפגשים של ${child.name}',
              ),
            Padding(
              padding: const EdgeInsets.fromLTRB(20, 0, 20, 12),
              child: Row(
                children: [
                  _filterBtn(label: 'הכל', selected: _filter == null, onTap: () => setState(() => _filter = null)),
                  const SizedBox(width: 8),
                  _filterBtn(
                      label: subjectMeta[Subject.math]!.heLabel,
                      selected: _filter == Subject.math,
                      onTap: () => setState(() => _filter = Subject.math)),
                  const SizedBox(width: 8),
                  _filterBtn(
                      label: subjectMeta[Subject.english]!.heLabel,
                      selected: _filter == Subject.english,
                      onTap: () => setState(() => _filter = Subject.english)),
                ],
              ),
            ),
            Expanded(
              child: filtered.isEmpty
                  ? _empty(context)
                  : ListView.separated(
                      padding: const EdgeInsets.fromLTRB(20, 4, 20, 16),
                      itemCount: filtered.length,
                      separatorBuilder: (context, _) => const SizedBox(height: 12),
                      itemBuilder: (_, i) => _SessionTile(session: filtered[i]),
                    ),
            ),
          ],
        );
    if (widget.embedded) return content;
    return Scaffold(
      body: SafeArea(bottom: false, child: content),
    );
  }

  Widget _filterBtn({
    required String label,
    required bool selected,
    required VoidCallback onTap,
  }) {
    return InkWell(
      onTap: onTap,
      borderRadius: BorderRadius.circular(AppRadii.pill),
      child: AnimatedContainer(
        duration: const Duration(milliseconds: 140),
        padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
        decoration: BoxDecoration(
          color: selected ? AppColors.sky : Colors.white,
          borderRadius: BorderRadius.circular(AppRadii.pill),
          border: Border.all(
            color: selected ? AppColors.sky : AppColors.skySoft,
            width: 2,
          ),
        ),
        child: Text(
          label,
          style: TextStyle(
            fontWeight: FontWeight.w700,
            fontSize: 13.5,
            color: selected ? Colors.white : AppColors.inkSoft,
          ),
        ),
      ),
    );
  }

  Widget _empty(BuildContext context) {
    return Center(
      child: Padding(
        padding: const EdgeInsets.all(24),
        child: Text(
          'עדיין אין מפגשים. ההתקן ישלח דיווח אוטומטית בסיום המפגש הראשון.',
          textAlign: TextAlign.center,
          style: AppTextStyles.hint(context),
        ),
      ),
    );
  }
}

class _SessionTile extends StatelessWidget {
  const _SessionTile({required this.session});
  final Session session;

  @override
  Widget build(BuildContext context) {
    final meta = subjectMeta[session.subject]!;
    final accColor = session.accuracyPct >= 80
        ? AppColors.mint
        : session.accuracyPct >= 70
            ? AppColors.sun
            : AppColors.coral;
    final dateTime = DateFormat('HH:mm', 'he').format(session.startedAt);
    return PCard(
      onTap: () => Navigator.of(context).push(
        MaterialPageRoute(
          builder: (_) => SessionDetailScreen(session: session),
        ),
      ),
      padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 14),
      child: Row(
        children: [
          Container(
            width: 48,
            height: 48,
            decoration: BoxDecoration(
              color: meta.tint,
              borderRadius: BorderRadius.circular(14),
            ),
            alignment: Alignment.center,
            child: Text(meta.emoji, style: const TextStyle(fontSize: 24)),
          ),
          const SizedBox(width: 12),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              mainAxisSize: MainAxisSize.min,
              children: [
                Text(meta.heLabel,
                    style: AppTextStyles.title(context).copyWith(fontSize: 15.5)),
                const SizedBox(height: 2),
                Text(
                  '${_heDay(session.startedAt)} · $dateTime · ${session.durationMinutes} דק׳',
                  style: AppTextStyles.hint(context).copyWith(fontSize: 12.5),
                ),
                const SizedBox(height: 7),
                Wrap(
                  spacing: 7,
                  children: [
                    Pill(
                      label: '${session.starsEarned}',
                      color: const Color(0xFFC98A12),
                      background: AppColors.sunSoft,
                      icon: const Icon(Icons.star,
                          color: Color(0xFFE6A91E), size: 12),
                    ),
                    Pill(
                      label: MoodScale.heLabel[session.moodSummary] ?? '',
                      icon: MoodDot(
                          color: MoodScale.color[session.moodSummary]!),
                    ),
                  ],
                ),
              ],
            ),
          ),
          Column(
            children: [
              Text(
                '${session.accuracyPct}%',
                style: AppTextStyles.display(context)
                    .copyWith(fontSize: 21, color: accColor),
              ),
              Text(
                '${session.correctCount}/${session.questionsAsked}',
                style: AppTextStyles.hint(context).copyWith(fontSize: 11),
              ),
            ],
          ),
        ],
      ),
    );
  }

  static String _heDay(DateTime d) {
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
