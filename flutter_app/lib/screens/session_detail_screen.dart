// SessionDetailScreen — stats card + mood card + per-question breakdown.

import 'package:flutter/material.dart';
import 'package:intl/intl.dart' hide TextDirection;
import 'package:provider/provider.dart';

import '../constants.dart';
import '../models/question_log.dart';
import '../models/session.dart';
import '../services/firebase_service.dart';
import '../theme.dart';
import '../widgets/p_card.dart';
import '../widgets/pill.dart';
import '../widgets/robot_face.dart';
import '../widgets/screen_header.dart';

class SessionDetailScreen extends StatelessWidget {
  const SessionDetailScreen({super.key, required this.session});

  final Session session;

  @override
  Widget build(BuildContext context) {
    final meta = subjectMeta[session.subject]!;
    final fb = context.read<FirebaseService>();
    final accColor = session.accuracyPct >= 80
        ? AppColors.mint
        : session.accuracyPct >= 70
            ? AppColors.sun
            : AppColors.coral;
    final timeStr = DateFormat('HH:mm', 'he').format(session.startedAt);
    return Scaffold(
      body: SafeArea(
        child: Column(
          children: [
            ScreenHeader(
              title: '${meta.heLabel} · ${_heDay(session.startedAt)}',
              subtitle: '$timeStr · ${session.durationMinutes} דק׳',
              onBack: () => Navigator.of(context).maybePop(),
            ),
            Expanded(
              child: SingleChildScrollView(
                padding: const EdgeInsets.fromLTRB(20, 4, 20, 24),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    PCard(
                      padding: const EdgeInsets.symmetric(
                          horizontal: 12, vertical: 18),
                      child: Row(
                        mainAxisAlignment: MainAxisAlignment.spaceAround,
                        children: [
                          _stat(context, '${session.accuracyPct}%', 'דיוק',
                              accColor),
                          _vdivider(),
                          _stat(context, '${session.starsEarned}', 'כוכבים',
                              const Color(0xFFE6A91E)),
                          _vdivider(),
                          _stat(context, '${session.longestStreak}', 'רצף שיא',
                              AppColors.sky),
                        ],
                      ),
                    ),
                    const SizedBox(height: 14),
                    PCard(
                      child: Row(
                        children: [
                          RobotFace(
                            emotion: MoodScale.emotion[session.moodSummary] ??
                                RobotEmotion.neutral,
                            size: 66,
                          ),
                          const SizedBox(width: 14),
                          Expanded(
                            child: Column(
                              crossAxisAlignment: CrossAxisAlignment.start,
                              children: [
                                Text(
                                  'מצב רוח כללי: ${MoodScale.heLabel[session.moodSummary] ?? ''}',
                                  style: AppTextStyles.title(context)
                                      .copyWith(fontSize: 15.5),
                                ),
                                const SizedBox(height: 3),
                                Text(
                                  'זוהה על ידי ההתקן במהלך המפגש',
                                  style: AppTextStyles.hint(context)
                                      .copyWith(fontSize: 13),
                                ),
                                if (session.endReasonLabel != null) ...[
                                  const SizedBox(height: 3),
                                  Text(
                                    'סיום: ${session.endReasonLabel}',
                                    style: AppTextStyles.hint(context)
                                        .copyWith(fontSize: 13),
                                  ),
                                ],
                              ],
                            ),
                          ),
                        ],
                      ),
                    ),
                    const SizedBox(height: 22),
                    Padding(
                      padding: const EdgeInsets.only(right: 2, bottom: 10),
                      child: Text(
                        'פירוט השאלות',
                        style: AppTextStyles.title(context).copyWith(fontSize: 16),
                      ),
                    ),
                    StreamBuilder<List<QuestionLog>>(
                      stream: fb.watchSessionQuestions(session.id),
                      builder: (context, snap) {
                        final qs = snap.data ?? const <QuestionLog>[];
                        if (snap.connectionState == ConnectionState.waiting) {
                          return const Padding(
                            padding: EdgeInsets.all(24),
                            child: Center(child: CircularProgressIndicator()),
                          );
                        }
                        if (qs.isEmpty) {
                          return PCard(
                            child: Text(
                              'אין פירוט שאלות זמין למפגש זה.',
                              style: AppTextStyles.hint(context),
                            ),
                          );
                        }
                        return Column(
                          children: [
                            for (final q in qs) ...[
                              _QuestionTile(q: q),
                              const SizedBox(height: 10),
                            ],
                          ],
                        );
                      },
                    ),
                  ],
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget _stat(BuildContext c, String v, String l, Color color) {
    return Expanded(
      child: Column(
        children: [
          Text(v,
              style: AppTextStyles.display(c)
                  .copyWith(fontSize: 23, color: color)),
          const SizedBox(height: 2),
          Text(l,
              style: AppTextStyles.hint(c).copyWith(fontSize: 11.5)),
        ],
      ),
    );
  }

  Widget _vdivider() => Container(
        width: 1.5,
        height: 38,
        color: AppColors.divider,
      );

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

class _QuestionTile extends StatelessWidget {
  const _QuestionTile({required this.q});
  final QuestionLog q;

  @override
  Widget build(BuildContext context) {
    return PCard(
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Row(
            children: [
              Container(
                width: 32,
                height: 32,
                decoration: BoxDecoration(
                  color: q.correct
                      ? AppColors.mintSoft
                      : AppColors.coralSoft,
                  shape: BoxShape.circle,
                ),
                child: Icon(
                  q.correct ? Icons.check : Icons.close,
                  size: 17,
                  color: q.correct
                      ? const Color(0xFF1E9C7E)
                      : const Color(0xFFC2425A),
                ),
              ),
              const SizedBox(width: 12),
              Expanded(
                child: Directionality(
                  textDirection: TextDirection.ltr,
                  child: Text(
                    q.prompt,
                    textAlign: TextAlign.right,
                    style: AppTextStyles.title(context).copyWith(fontSize: 16),
                  ),
                ),
              ),
              MoodDot(color: MoodScale.color[q.mood]!, size: 13),
            ],
          ),
          const SizedBox(height: 10),
          Padding(
            padding: const EdgeInsetsDirectional.only(start: 44),
            child: Wrap(
              spacing: 18,
              runSpacing: 4,
              children: [
                _kv(context, 'תשובת הילד', q.childAnswerTranscript,
                    color: q.correct
                        ? const Color(0xFF1E9C7E)
                        : AppColors.coral),
                if (!q.correct)
                  _kv(context, 'נכון', q.expectedAnswer,
                      color: AppColors.ink),
              ],
            ),
          ),
        ],
      ),
    );
  }

  Widget _kv(BuildContext c, String k, String v, {required Color color}) {
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        Text('$k: ',
            style: AppTextStyles.hint(c).copyWith(fontSize: 13)),
        Directionality(
          textDirection: TextDirection.ltr,
          child: Text(
            v,
            style: TextStyle(
              fontWeight: FontWeight.w800,
              fontSize: 13,
              color: color,
            ),
          ),
        ),
      ],
    );
  }
}
