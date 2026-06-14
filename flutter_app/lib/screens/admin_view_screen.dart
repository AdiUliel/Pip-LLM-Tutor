// AdminViewScreen — technician view: raw deviceState document, recent logs,
// reset-to-defaults. Layout from ~/Downloads/ioT/screen-device.jsx (AdminView).

import 'package:flutter/material.dart';
import 'package:intl/intl.dart' hide TextDirection;
import 'package:provider/provider.dart';

import '../constants.dart';
import '../providers/child_provider.dart';
import '../providers/config_provider.dart';
import '../providers/device_provider.dart';
import '../theme.dart';
import '../widgets/p_card.dart';
import '../widgets/screen_header.dart';

class AdminViewScreen extends StatefulWidget {
  const AdminViewScreen({super.key});

  @override
  State<AdminViewScreen> createState() => _AdminViewScreenState();
}

class _AdminViewScreenState extends State<AdminViewScreen> {
  bool _didReset = false;

  @override
  Widget build(BuildContext context) {
    final child = context.watch<ChildProvider>().child;
    final device = context.watch<DeviceProvider>();
    final config = context.read<ConfigProvider>();
    final state = device.state;
    final online = device.isOnline;
    final status = state?.status ?? DeviceStatus.idle;
    final heartbeatLabel = state?.lastHeartbeat == null
        ? '—'
        : DateFormat('HH:mm:ss', 'he').format(state!.lastHeartbeat!);

    return Scaffold(
      body: SafeArea(
        child: Column(
          children: [
            ScreenHeader(
              title: 'מצב טכנאי',
              subtitle: 'deviceState גולמי + לוגים',
              onBack: () => Navigator.of(context).maybePop(),
            ),
            Expanded(
              child: ListView(
                padding: const EdgeInsets.fromLTRB(20, 4, 20, 24),
                children: [
                  PCard(
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(
                          'deviceState/${child?.deviceId ?? '—'}',
                          style: TextStyle(
                            fontWeight: FontWeight.w800,
                            fontSize: 14,
                            color: AppColors.inkSoft,
                          ),
                        ),
                        const SizedBox(height: 6),
                        _kv('online', '$online',
                            valueColor: online
                                ? const Color(0xFF1E9C7E)
                                : AppColors.coral),
                        _kv('status', deviceStatusId[status] ?? 'idle'),
                        _kv('lastHeartbeat', heartbeatLabel,
                            valueColor:
                                online ? AppColors.ink : AppColors.coral),
                        _kv('activeSubject',
                            state?.activeSubject == null
                                ? '—'
                                : subjectMeta[state!.activeSubject!]!.id),
                        _kv(
                          'command',
                          state?.command ?? 'none',
                          last: true,
                        ),
                      ],
                    ),
                  ),
                  const SizedBox(height: 22),
                  Padding(
                    padding: const EdgeInsets.only(right: 2, bottom: 10),
                    child: Text(
                      'לוגים אחרונים',
                      style:
                          AppTextStyles.title(context).copyWith(fontSize: 16),
                    ),
                  ),
                  PCard(
                    padding: const EdgeInsets.all(14),
                    child: Directionality(
                      textDirection: TextDirection.ltr,
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.stretch,
                        children: [
                          for (var i = 0; i < _logs(online, status).length; i++)
                            _LogRow(
                              entry: _logs(online, status)[i],
                              isLast: i == _logs(online, status).length - 1,
                            ),
                        ],
                      ),
                    ),
                  ),
                  const SizedBox(height: 22),
                  Padding(
                    padding: const EdgeInsets.only(right: 2, bottom: 10),
                    child: Text(
                      'כלים',
                      style:
                          AppTextStyles.title(context).copyWith(fontSize: 16),
                    ),
                  ),
                  OutlinedButton.icon(
                    style: OutlinedButton.styleFrom(
                      foregroundColor: AppColors.coral,
                      side: const BorderSide(
                          color: AppColors.coralSoft, width: 2),
                    ),
                    icon: const Icon(Icons.restart_alt_rounded,
                        color: AppColors.coral),
                    label: Text(_didReset
                        ? '✓ אופס לברירת מחדל'
                        : 'איפוס לברירות מחדל'),
                    onPressed: () async {
                      await config.resetToDefaults();
                      if (!mounted) return;
                      setState(() => _didReset = true);
                      Future.delayed(const Duration(seconds: 2), () {
                        if (mounted) setState(() => _didReset = false);
                      });
                    },
                  ),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget _kv(String k, String v, {Color? valueColor, bool last = false}) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 11),
      child: Container(
        decoration: BoxDecoration(
          border: last
              ? null
              : const Border(
                  bottom: BorderSide(color: AppColors.divider, width: 1.5),
                ),
        ),
        padding: EdgeInsets.only(bottom: last ? 0 : 11),
        child: Row(
          mainAxisAlignment: MainAxisAlignment.spaceBetween,
          children: [
            Text(k,
                style: const TextStyle(
                  color: AppColors.inkSoft,
                  fontSize: 13.5,
                )),
            Directionality(
              textDirection: TextDirection.ltr,
              child: Text(
                v,
                style: TextStyle(
                  fontFamily: 'monospace',
                  fontWeight: FontWeight.w700,
                  fontSize: 13.5,
                  color: valueColor ?? AppColors.ink,
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }

  List<_LogEntry> _logs(bool online, DeviceStatus status) {
    final now = DateTime.now();
    String t(int sAgo) =>
        DateFormat('HH:mm:ss', 'he').format(now.subtract(Duration(seconds: sAgo)));
    return [
      _LogEntry(t(2), 'heartbeat ok · rssi -52dBm', ok: online),
      _LogEntry(t(5), 'status → ${deviceStatusId[status]}', ok: true),
      _LogEntry(t(22), 'session.write s6 · acc=83', ok: true),
      _LogEntry(t(40), 'mood.detect → happy (0.91)', ok: true),
      _LogEntry(
        t(120),
        online
            ? 'fetch materials/m1 · 18 items'
            : 'WARN: heartbeat timeout',
        ok: online,
      ),
    ];
  }
}

class _LogEntry {
  final String time;
  final String message;
  final bool ok;
  _LogEntry(this.time, this.message, {required this.ok});
}

class _LogRow extends StatelessWidget {
  const _LogRow({required this.entry, required this.isLast});
  final _LogEntry entry;
  final bool isLast;

  @override
  Widget build(BuildContext context) {
    return Container(
      decoration: BoxDecoration(
        border: isLast
            ? null
            : const Border(
                bottom: BorderSide(color: Color(0xFFF2F5FA), width: 1)),
      ),
      padding: const EdgeInsets.symmetric(vertical: 5),
      child: Row(
        children: [
          Text(
            entry.time,
            style: const TextStyle(
              color: Color(0xFFA9B4CC),
              fontSize: 12,
              fontFamily: 'monospace',
            ),
          ),
          const SizedBox(width: 10),
          Expanded(
            child: Text(
              entry.message,
              style: TextStyle(
                fontFamily: 'monospace',
                fontSize: 12,
                color: entry.ok ? AppColors.inkSoft : AppColors.coral,
                fontWeight: entry.ok ? FontWeight.w400 : FontWeight.w700,
              ),
            ),
          ),
        ],
      ),
    );
  }
}
