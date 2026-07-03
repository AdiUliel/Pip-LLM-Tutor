// DeviceMonitorScreen — live device hero, status grid, optional remote
// start/stop. Reads from DeviceProvider's live stream.

import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../constants.dart';
import '../providers/child_provider.dart';
import '../providers/device_provider.dart';
import '../theme.dart';
import '../widgets/dev_chip.dart';
import '../widgets/p_card.dart';
import '../widgets/pairing_sheet.dart';
import '../widgets/robot_face.dart';
import '../widgets/screen_header.dart';

class DeviceMonitorScreen extends StatefulWidget {
  const DeviceMonitorScreen({super.key});

  @override
  State<DeviceMonitorScreen> createState() => _DeviceMonitorScreenState();
}

class _DeviceMonitorScreenState extends State<DeviceMonitorScreen> {
  bool _starting = false;
  bool _stopping = false;
  bool _repairing = false;

  @override
  Widget build(BuildContext context) {
    final child = context.watch<ChildProvider>().child;
    final hasDevice = child?.deviceId.isNotEmpty ?? false;
    final device = context.watch<DeviceProvider>();
    final state = device.state;
    final online = device.isOnline;
    final status = state?.status ?? DeviceStatus.idle;
    final running = online && status != DeviceStatus.idle;

    final emo = !online
        ? RobotEmotion.neutral
        : status == DeviceStatus.listening
            ? RobotEmotion.listening
            : status == DeviceStatus.onBreak
                ? RobotEmotion.sleepy
                : RobotEmotion.happy;

    return Scaffold(
      body: SafeArea(
        bottom: false,
        child: Column(
          children: [
            ScreenHeader(
              title: 'ניטור ההתקן',
              subtitle: hasDevice ? child!.deviceId : 'טרם חובר התקן',
            ),
            Expanded(
              child: ListView(
                padding: const EdgeInsets.fromLTRB(20, 4, 20, 24),
                children: [
                  PCard(
                    background: online ? const Color(0xFFF7FAFF) : null,
                    borderColor: online ? AppColors.skySoft : null,
                    child: Column(
                      children: [
                        RobotFace(emotion: emo, size: 150),
                        const SizedBox(height: 14),
                        DevChip(
                          label: online
                              ? 'מחובר · ${deviceStatusHe[status] ?? ''}'
                              : 'לא מחובר',
                          state: online
                              ? DevChipState.online
                              : DevChipState.offline,
                        ),
                      ],
                    ),
                  ),
                  const SizedBox(height: 14),
                  Row(
                    children: [
                      Expanded(
                        child: _MiniCard(
                          title: 'פעימה אחרונה',
                          value: _heartbeatAge(state?.lastHeartbeat, online),
                          accent: online ? AppColors.ink : AppColors.coral,
                        ),
                      ),
                      const SizedBox(width: 12),
                      Expanded(
                        child: _MiniCard(
                          title: 'מקצוע פעיל',
                          value: running && state?.activeSubject != null
                              ? subjectMeta[state!.activeSubject!]!.heLabel
                              : '—',
                          accent: AppColors.ink,
                        ),
                      ),
                    ],
                  ),
                  if (running && (state?.currentQuestion?.isNotEmpty ?? false)) ...[
                    const SizedBox(height: 12),
                    PCard(
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Text(
                            'שאלה נוכחית',
                            style: AppTextStyles.hint(context)
                                .copyWith(fontSize: 12.5),
                          ),
                          const SizedBox(height: 4),
                          Directionality(
                            textDirection: TextDirection.ltr,
                            child: Text(
                              state!.currentQuestion!,
                              textAlign: TextAlign.right,
                              style: AppTextStyles.title(context)
                                  .copyWith(fontSize: 18),
                            ),
                          ),
                        ],
                      ),
                    ),
                  ],
                  const SizedBox(height: 22),
                  Padding(
                    padding: const EdgeInsets.only(right: 2, bottom: 10),
                    child: Text(
                      'שליטה מרחוק',
                      style:
                          AppTextStyles.title(context).copyWith(fontSize: 16),
                    ),
                  ),
                  PCard(
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.stretch,
                      children: [
                        Text(
                          'בדרך כלל הילד מפעיל את ההתקן בעצמו עם הכפתור הפיזי. ניתן גם להפעיל מכאן.',
                          style: AppTextStyles.hint(context),
                        ),
                        const SizedBox(height: 14),
                        Row(
                          children: [
                            Expanded(
                              child: ElevatedButton(
                                style: ElevatedButton.styleFrom(
                                  backgroundColor: AppColors.mint,
                                ),
                                onPressed: !online || running || _starting
                                    ? null
                                    : () => _send(action: 'start'),
                                child: Text(
                                    _starting ? 'שולח…' : '▶ התחלה'),
                              ),
                            ),
                            const SizedBox(width: 10),
                            Expanded(
                              child: OutlinedButton(
                                onPressed: !online || !running || _stopping
                                    ? null
                                    : () => _send(action: 'stop'),
                                child:
                                    Text(_stopping ? 'שולח…' : '■ עצירה'),
                              ),
                            ),
                          ],
                        ),
                        if (!online) ...[
                          const SizedBox(height: 12),
                          Text(
                            '⚠️ ההתקן לא מחובר כרגע',
                            textAlign: TextAlign.center,
                            style: AppTextStyles.hint(context)
                                .copyWith(color: AppColors.coral),
                          ),
                        ],
                      ],
                    ),
                  ),
                  const SizedBox(height: 22),
                  Padding(
                    padding: const EdgeInsets.only(right: 2, bottom: 10),
                    child: Text(
                      'חיבור התקן',
                      style:
                          AppTextStyles.title(context).copyWith(fontSize: 16),
                    ),
                  ),
                  PCard(
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.stretch,
                      children: [
                        Text(
                          hasDevice
                              ? 'אם החלפתם את ההתקן הפיזי, או שהמסך מראה קוד '
                                  'שאינו תואם — הקלידו כאן את הקוד החדש.'
                              : 'ודאו שההתקן דולק ומחובר ל-Wi-Fi, והקלידו כאן '
                                  'את הקוד המופיע על מסכו כדי לחבר אותו.',
                          style: AppTextStyles.hint(context),
                        ),
                        const SizedBox(height: 14),
                        OutlinedButton(
                          onPressed: (child == null || _repairing)
                              ? null
                              : _repair,
                          child: Text(_repairing
                              ? 'שומר…'
                              : (hasDevice ? 'החלפת התקן' : 'חיבור התקן')),
                        ),
                      ],
                    ),
                  ),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }

  Future<void> _repair() async {
    final messenger = ScaffoldMessenger.of(context);
    final childProv = context.read<ChildProvider>();
    final cur = childProv.child;
    if (cur == null) return;

    final wasEmpty = cur.deviceId.isEmpty;
    final newId = await showPairingSheet(context);
    if (!mounted || newId == null || newId == cur.deviceId) return;

    setState(() => _repairing = true);
    try {
      await childProv.save(cur.copyWith(deviceId: newId));
      if (!mounted) return;
      messenger.showSnackBar(
        SnackBar(
          content: Text(wasEmpty ? 'ההתקן חובר בהצלחה' : 'ההתקן הוחלף בהצלחה'),
        ),
      );
    } catch (e) {
      if (!mounted) return;
      messenger.showSnackBar(
        SnackBar(content: Text('שמירת ההתקן נכשלה: $e')),
      );
    } finally {
      if (mounted) setState(() => _repairing = false);
    }
  }

  Future<void> _send({required String action}) async {
    final dev = context.read<DeviceProvider>();
    if (!dev.isOnline) {
      _showToast('ההתקן לא מחובר — לא ניתן לשלוח פקודה');
      return;
    }
    setState(() {
      if (action == 'start') _starting = true;
      if (action == 'stop') _stopping = true;
    });
    if (action == 'start') await dev.sendStart();
    if (action == 'stop') await dev.sendStop();
    await Future.delayed(const Duration(milliseconds: 1100));
    if (!mounted) return;
    setState(() {
      _starting = false;
      _stopping = false;
    });
  }

  void _showToast(String message) {
    ScaffoldMessenger.of(context).showSnackBar(
      SnackBar(content: Text(message)),
    );
  }

  static String _heartbeatAge(DateTime? at, bool online) {
    if (at == null) return 'אין נתון';
    final secs = DateTime.now().difference(at).inSeconds;
    if (secs < 60) return 'לפני $secs שניות';
    final mins = secs ~/ 60;
    if (mins < 60) return 'לפני $mins דק׳';
    final hours = mins ~/ 60;
    return 'לפני $hours שעות';
  }
}

class _MiniCard extends StatelessWidget {
  const _MiniCard({
    required this.title,
    required this.value,
    required this.accent,
  });
  final String title;
  final String value;
  final Color accent;

  @override
  Widget build(BuildContext context) {
    return PCard(
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        mainAxisSize: MainAxisSize.min,
        children: [
          Text(title,
              style: AppTextStyles.hint(context).copyWith(fontSize: 12.5)),
          const SizedBox(height: 4),
          Text(
            value,
            style:
                AppTextStyles.title(context).copyWith(fontSize: 16, color: accent),
            maxLines: 1,
            overflow: TextOverflow.ellipsis,
          ),
        ],
      ),
    );
  }
}
