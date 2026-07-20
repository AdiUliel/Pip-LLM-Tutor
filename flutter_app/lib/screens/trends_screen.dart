// TrendsScreen — accuracy / mood / time-spent / subject-distribution.
// fl_chart line charts and a custom bar layout, fed from StatsProvider.

import 'package:fl_chart/fl_chart.dart';
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

class TrendsScreen extends StatelessWidget {
  const TrendsScreen({super.key, this.embedded = false});

  /// When true the screen is hosted inside [ReportsHubScreen]'s TabBarView, so
  /// it renders without its own header/Scaffold (the hub supplies them).
  final bool embedded;

  @override
  Widget build(BuildContext context) {
    final child = context.watch<ChildProvider>().child;
    final stats = context.watch<StatsProvider>();
    // Most recent 14 sessions, oldest→newest, so the time-series charts stay
    // readable.
    final sessions = stats.sessions.take(14).toList().reversed.toList();
    final content = Column(
          children: [
            if (!embedded)
              ScreenHeader(
                title: 'מגמות',
                subtitle: child == null
                    ? null
                    : 'התקדמות ומצב רוח · ${child.name}',
              ),
            Expanded(
              child: sessions.isEmpty
                  ? Center(
                      child: Padding(
                        padding: const EdgeInsets.all(24),
                        child: Text(
                          'נדרש לפחות מפגש אחד כדי להציג מגמות.',
                          textAlign: TextAlign.center,
                          style: AppTextStyles.hint(context),
                        ),
                      ),
                    )
                  : ListView(
                      padding: const EdgeInsets.fromLTRB(20, 4, 20, 24),
                      children: [
                        _summaryCard(context, stats),
                        const SizedBox(height: 14),
                        _accuracyCard(context, sessions, stats),
                        const SizedBox(height: 14),
                        _moodCard(context, sessions),
                        const SizedBox(height: 14),
                        _timeCard(context, sessions),
                        const SizedBox(height: 14),
                        _subjectsCard(context, sessions),
                      ],
                    ),
            ),
          ],
        );
    if (embedded) return content;
    return Scaffold(
      body: SafeArea(bottom: false, child: content),
    );
  }

  // ── cards ──────────────────────────────────────────────────────────────

  /// Headline totals computed across all loaded sessions (not just the 14
  /// charted). Pure aggregation from StatsProvider — no extra Firestore reads.
  Widget _summaryCard(BuildContext c, StatsProvider stats) {
    final tiles = <Widget>[
      _StatTile(value: '${stats.totalSessions}', label: 'מפגשים', color: AppColors.mint),
      _StatTile(value: '${stats.totalQuestions}', label: 'שאלות', color: AppColors.grape),
      _StatTile(value: '${stats.totalStars}', label: 'כוכבים', icon: '⭐', color: AppColors.sun),
      _StatTile(value: '${stats.activeDayStreak}', label: 'ימים ברצף', icon: '🔥', color: AppColors.sun),
      _StatTile(value: '${stats.sessionsThisWeek}', label: 'השבוע', color: AppColors.mint),
      _StatTile(value: '${stats.avgSessionMinutes}', label: 'דק׳ למפגש', color: AppColors.grape),
    ];
    return _ChartCard(
      title: 'סיכום כללי',
      hint: 'לאורך כל השימוש',
      child: Wrap(
        spacing: 10,
        runSpacing: 10,
        children: [for (final t in tiles) t],
      ),
    );
  }

  Widget _accuracyCard(
      BuildContext c, List<Session> oldestFirst, StatsProvider stats) {
    // Average over the shown sessions so the pill matches the chart.
    final avg = oldestFirst.isEmpty
        ? 0
        : (oldestFirst.fold<int>(0, (a, s) => a + s.accuracyPct) /
                oldestFirst.length)
            .round();
    return _ChartCard(
      title: 'דיוק לאורך זמן',
      hint: 'אחוז התשובות הנכונות בכל מפגש',
      right: Pill(
        label: 'ממוצע $avg%',
        color: AppColors.mint,
        background: AppColors.mintSoft,
      ),
      child: SizedBox(
        height: 150,
        child: _SmoothLineChart(
          values: [for (final s in oldestFirst) s.accuracyPct.toDouble()],
          labels: [for (final s in oldestFirst) _shortDay(s.startedAt)],
          minY: 0,
          maxY: 100,
          lineColor: AppColors.mint,
        ),
      ),
    );
  }

  Widget _moodCard(BuildContext c, List<Session> oldestFirst) {
    final avg = (oldestFirst.fold<int>(0, (a, s) => a + s.moodSummary) /
            oldestFirst.length)
        .round()
        .clamp(MoodScale.min, MoodScale.max);
    return _ChartCard(
      title: 'מצב רוח לאורך זמן',
      hint: 'זוהה על ידי ההתקן בכל מפגש',
      right: Pill(
        label: MoodScale.heLabel[avg] ?? '',
        icon: MoodDot(color: MoodScale.color[avg]!),
      ),
      child: SizedBox(
        height: 150,
        child: _SmoothLineChart(
          values: [for (final s in oldestFirst) s.moodSummary.toDouble()],
          labels: [for (final s in oldestFirst) _shortDay(s.startedAt)],
          minY: 1,
          maxY: 5,
          lineColor: AppColors.grape,
          yMaxLabel: 'נהנה',
          yMinLabel: 'מתוסכל',
        ),
      ),
    );
  }

  Widget _timeCard(BuildContext c, List<Session> oldestFirst) {
    // Scale the Y axis to the longest session (+15% headroom) so a long session's
    // bar never overflows the card. Floor at 22 min so short sessions still read
    // on a sensible scale. (fl_chart does NOT clip a rod taller than maxY.)
    final maxDur = oldestFirst.fold<int>(
        0, (m, s) => s.durationMinutes > m ? s.durationMinutes : m);
    final chartMaxY = maxDur <= 20 ? 22.0 : (maxDur * 1.15).ceilToDouble();
    return _ChartCard(
      title: 'זמן לימוד',
      hint: 'דקות לכל מפגש (לפי מקצוע)',
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          SizedBox(
            height: 160,
            child: BarChart(
              BarChartData(
                gridData: const FlGridData(show: false),
                borderData: FlBorderData(show: false),
                titlesData: FlTitlesData(
                  topTitles: const AxisTitles(
                      sideTitles: SideTitles(showTitles: false)),
                  rightTitles: const AxisTitles(
                      sideTitles: SideTitles(showTitles: false)),
                  leftTitles: const AxisTitles(
                      sideTitles: SideTitles(showTitles: false)),
                  bottomTitles: AxisTitles(
                    sideTitles: SideTitles(
                      showTitles: true,
                      reservedSize: 24,
                      getTitlesWidget: (v, _) {
                        final i = v.toInt();
                        if (i < 0 || i >= oldestFirst.length) {
                          return const SizedBox.shrink();
                        }
                        return Padding(
                          padding: const EdgeInsets.only(top: 6),
                          child: Text(
                            _shortDay(oldestFirst[i].startedAt),
                            style: AppTextStyles.hint(c).copyWith(fontSize: 10.5),
                          ),
                        );
                      },
                    ),
                  ),
                ),
                barGroups: [
                  for (var i = 0; i < oldestFirst.length; i++)
                    BarChartGroupData(
                      x: i,
                      barRods: [
                        BarChartRodData(
                          toY: oldestFirst[i].durationMinutes.toDouble(),
                          width: 16,
                          borderRadius: const BorderRadius.vertical(
                              top: Radius.circular(7),
                              bottom: Radius.circular(2)),
                          color: subjectMeta[oldestFirst[i].subject]!.ink,
                        ),
                      ],
                    ),
                ],
                maxY: chartMaxY,
              ),
            ),
          ),
          const SizedBox(height: 10),
          Row(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              _legend(c, AppColors.sun, subjectMeta[Subject.math]!.heLabel,
                  const Color(0xFFC98A12)),
              const SizedBox(width: 16),
              _legend(c, AppColors.mint,
                  subjectMeta[Subject.english]!.heLabel,
                  const Color(0xFF1E9C7E)),
            ],
          ),
        ],
      ),
    );
  }

  Widget _legend(BuildContext c, Color color, String label, Color sw) {
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        Container(
          width: 11,
          height: 11,
          decoration: BoxDecoration(
            color: sw,
            borderRadius: BorderRadius.circular(3),
          ),
        ),
        const SizedBox(width: 6),
        Text(label,
            style: AppTextStyles.hint(c).copyWith(
              fontSize: 12.5,
              fontWeight: FontWeight.w700,
            )),
      ],
    );
  }

  Widget _subjectsCard(BuildContext c, List<Session> all) {
    final math = all.where((s) => s.subject == Subject.math).length;
    final eng = all.where((s) => s.subject == Subject.english).length;
    final total = math + eng;
    if (total == 0) return const SizedBox.shrink();
    return _ChartCard(
      title: 'מקצועות שתורגלו',
      hint: '$total המפגשים האחרונים',
      child: Column(
        children: [
          _SubjectBar(
            subject: Subject.math,
            count: math,
            total: total,
          ),
          const SizedBox(height: 12),
          _SubjectBar(
            subject: Subject.english,
            count: eng,
            total: total,
          ),
        ],
      ),
    );
  }

  static String _shortDay(DateTime d) {
    final now = DateTime.now();
    final today = DateTime(now.year, now.month, now.day);
    final that = DateTime(d.year, d.month, d.day);
    final ago = today.difference(that).inDays;
    if (ago == 0) return 'היום';
    if (ago == 1) return 'אתמ׳';
    // Actual date — d/M (e.g. 14/6) so multiple sessions on the same
    // weekday are distinguishable.
    return DateFormat('d/M').format(d);
  }
}

class _ChartCard extends StatelessWidget {
  const _ChartCard({
    required this.title,
    this.hint,
    this.right,
    required this.child,
  });
  final String title;
  final String? hint;
  final Widget? right;
  final Widget child;

  @override
  Widget build(BuildContext context) {
    return PCard(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Row(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Expanded(
                child: Text(
                  title,
                  style:
                      AppTextStyles.title(context).copyWith(fontSize: 15.5),
                ),
              ),
              ?right,
            ],
          ),
          if (hint != null) ...[
            const SizedBox(height: 2),
            Text(hint!,
                style: AppTextStyles.hint(context).copyWith(fontSize: 12.5)),
            const SizedBox(height: 10),
          ] else
            const SizedBox(height: 10),
          child,
        ],
      ),
    );
  }
}

class _StatTile extends StatelessWidget {
  const _StatTile({
    required this.value,
    required this.label,
    required this.color,
    this.icon,
  });
  final String value;
  final String label;
  final Color color;
  final String? icon;

  @override
  Widget build(BuildContext context) {
    return Container(
      width: 88,
      padding: const EdgeInsets.symmetric(vertical: 12, horizontal: 8),
      decoration: BoxDecoration(
        color: color.withValues(alpha: 0.10),
        borderRadius: BorderRadius.circular(AppRadii.sm),
      ),
      child: Column(
        children: [
          Text(
            icon == null ? value : '$icon $value',
            style: AppTextStyles.title(context)
                .copyWith(fontSize: 20, color: color, fontWeight: FontWeight.w800),
          ),
          const SizedBox(height: 2),
          Text(label,
              textAlign: TextAlign.center,
              style: AppTextStyles.hint(context).copyWith(fontSize: 11.5)),
        ],
      ),
    );
  }
}

class _SubjectBar extends StatelessWidget {
  const _SubjectBar({
    required this.subject,
    required this.count,
    required this.total,
  });
  final Subject subject;
  final int count;
  final int total;

  @override
  Widget build(BuildContext context) {
    final meta = subjectMeta[subject]!;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Row(
          mainAxisAlignment: MainAxisAlignment.spaceBetween,
          children: [
            Container(
              padding:
                  const EdgeInsets.symmetric(horizontal: 11, vertical: 5),
              decoration: BoxDecoration(
                color: meta.tint,
                borderRadius: BorderRadius.circular(AppRadii.pill),
              ),
              child: Row(
                mainAxisSize: MainAxisSize.min,
                children: [
                  Text(meta.emoji, style: const TextStyle(fontSize: 13)),
                  const SizedBox(width: 4),
                  Text(meta.heLabel,
                      style: TextStyle(
                        fontWeight: FontWeight.w700,
                        fontSize: 12.5,
                        color: meta.ink,
                      )),
                ],
              ),
            ),
            Text('$count מפגשים',
                style: AppTextStyles.title(context).copyWith(fontSize: 14)),
          ],
        ),
        const SizedBox(height: 6),
        Container(
          height: 10,
          decoration: BoxDecoration(
            color: AppColors.divider,
            borderRadius: BorderRadius.circular(99),
          ),
          alignment: AlignmentDirectional.centerStart,
          child: FractionallySizedBox(
            widthFactor: total == 0 ? 0 : count / total,
            child: Container(
              decoration: BoxDecoration(
                color: meta.ink,
                borderRadius: BorderRadius.circular(99),
              ),
            ),
          ),
        ),
      ],
    );
  }
}

/// Smooth line chart with gradient fill, dots, and X-axis labels.
/// Optionally renders a single label at Y=maxY and another at Y=minY on the
/// leading edge — used for the mood chart (נהנה ↔ מתוסכל).
class _SmoothLineChart extends StatelessWidget {
  const _SmoothLineChart({
    required this.values,
    required this.labels,
    required this.minY,
    required this.maxY,
    required this.lineColor,
    this.yMaxLabel,
    this.yMinLabel,
  });

  final List<double> values;
  final List<String> labels;
  final double minY;
  final double maxY;
  final Color lineColor;
  final String? yMaxLabel;
  final String? yMinLabel;

  @override
  Widget build(BuildContext context) {
    return LineChart(
      LineChartData(
        minX: 0,
        maxX: (values.length - 1).toDouble().clamp(0, double.infinity),
        minY: minY,
        maxY: maxY,
        gridData: FlGridData(
          show: true,
          drawVerticalLine: false,
          horizontalInterval: (maxY - minY) / 4,
          getDrawingHorizontalLine: (_) => FlLine(
            color: AppColors.divider,
            strokeWidth: 1.5,
          ),
        ),
        borderData: FlBorderData(show: false),
        titlesData: FlTitlesData(
          topTitles:
              const AxisTitles(sideTitles: SideTitles(showTitles: false)),
          leftTitles: AxisTitles(
            sideTitles: SideTitles(
              showTitles: yMaxLabel != null || yMinLabel != null,
              reservedSize: 42,
              interval: (maxY - minY),
              getTitlesWidget: (v, _) {
                if ((v - maxY).abs() < 0.01 && yMaxLabel != null) {
                  return Padding(
                    padding: const EdgeInsets.only(right: 4),
                    child: Text(
                      yMaxLabel!,
                      style: AppTextStyles.hint(context)
                          .copyWith(fontSize: 11),
                    ),
                  );
                }
                if ((v - minY).abs() < 0.01 && yMinLabel != null) {
                  return Padding(
                    padding: const EdgeInsets.only(right: 4),
                    child: Text(
                      yMinLabel!,
                      style: AppTextStyles.hint(context)
                          .copyWith(fontSize: 11),
                    ),
                  );
                }
                return const SizedBox.shrink();
              },
            ),
          ),
          rightTitles:
              const AxisTitles(sideTitles: SideTitles(showTitles: false)),
          bottomTitles: AxisTitles(
            sideTitles: SideTitles(
              showTitles: true,
              reservedSize: 22,
              getTitlesWidget: (v, _) {
                final i = v.toInt();
                if (i < 0 || i >= labels.length) return const SizedBox.shrink();
                return Padding(
                  padding: const EdgeInsets.only(top: 6),
                  child: Text(
                    labels[i],
                    style: AppTextStyles.hint(context).copyWith(fontSize: 10.5),
                  ),
                );
              },
            ),
          ),
        ),
        lineBarsData: [
          LineChartBarData(
            spots: [
              for (var i = 0; i < values.length; i++)
                FlSpot(i.toDouble(), values[i]),
            ],
            isCurved: true,
            curveSmoothness: 0.32,
            color: lineColor,
            barWidth: 3.5,
            isStrokeCapRound: true,
            dotData: FlDotData(
              show: true,
              getDotPainter: (spot, _, _, _) => FlDotCirclePainter(
                radius: 5,
                color: Colors.white,
                strokeWidth: 3,
                strokeColor: lineColor,
              ),
            ),
            belowBarData: BarAreaData(
              show: true,
              gradient: LinearGradient(
                begin: Alignment.topCenter,
                end: Alignment.bottomCenter,
                colors: [
                  lineColor.withValues(alpha: 0.22),
                  lineColor.withValues(alpha: 0),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }
}
