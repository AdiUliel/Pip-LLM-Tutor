// SettingsScreen — sliders for session length / break / daily limit, plus a
// rows-stack for profile / admin / logout.
// Layout from ~/Downloads/ioT/screen-device.jsx (Settings).

import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../models/child.dart';
import '../providers/auth_provider.dart';
import '../providers/child_provider.dart';
import '../providers/config_provider.dart';
import '../theme.dart';
import '../widgets/p_card.dart';
import '../widgets/screen_header.dart';
import 'admin_view_screen.dart';
import 'child_config_screen.dart';

class SettingsScreen extends StatefulWidget {
  const SettingsScreen({super.key});

  @override
  State<SettingsScreen> createState() => _SettingsScreenState();
}

class _SettingsScreenState extends State<SettingsScreen> {
  late int _sessionMin;
  late int _breakMin;
  bool _initialized = false;
  bool _dirty = false;
  bool _saving = false;

  @override
  Widget build(BuildContext context) {
    final child = context.watch<ChildProvider>().child;
    if (child == null) {
      return const Scaffold(body: Center(child: CircularProgressIndicator()));
    }
    if (!_initialized) {
      _initialized = true;
      _sessionMin = child.settings.sessionMinutes;
      _breakMin = child.settings.breakEveryMinutes;
    }

    return Scaffold(
      body: SafeArea(
        bottom: false,
        child: Column(
          children: [
            ScreenHeader(
              title: 'הגדרות',
              subtitle: '${child.name} · גיל ${child.age}',
              right: _dirty
                  ? TextButton(
                      onPressed: _saving ? null : () => _save(child),
                      style: TextButton.styleFrom(
                        foregroundColor: AppColors.sky,
                      ),
                      child: Text(_saving ? 'שומר…' : 'שמירה'),
                    )
                  : null,
            ),
            Expanded(
              child: ListView(
                padding: const EdgeInsets.fromLTRB(20, 4, 20, 16),
                children: [
                  _section('זמני לימוד'),
                  _Slider(
                    label: 'אורך מפגש',
                    value: _sessionMin,
                    min: 5,
                    max: 30,
                    step: 1,
                    unit: 'דק׳',
                    onChanged: (v) {
                      setState(() {
                        _sessionMin = v;
                        _dirty = true;
                      });
                    },
                  ),
                  _Slider(
                    label: 'הפסקה כל',
                    value: _breakMin,
                    min: 3,
                    max: 15,
                    step: 1,
                    unit: 'דק׳',
                    onChanged: (v) {
                      setState(() {
                        _breakMin = v;
                        _dirty = true;
                      });
                    },
                  ),
                  const SizedBox(height: 8),
                  _section('כללי'),
                  PCard(
                    padding: EdgeInsets.zero,
                    child: Column(
                      children: [
                        _Row(
                          icon: Icons.face_outlined,
                          label: 'פרופיל הילד',
                          sub: 'גיל, מקצועות ורמה',
                          onTap: () => Navigator.of(context).push(
                            MaterialPageRoute(
                              builder: (_) => const ChildConfigScreen(),
                            ),
                          ),
                        ),
                        const _Divider(),
                        _Row(
                          icon: Icons.admin_panel_settings_outlined,
                          label: 'מצב טכנאי',
                          sub: 'לוגים גולמיים ואבחון',
                          onTap: () => Navigator.of(context).push(
                            MaterialPageRoute(
                              builder: (_) => const AdminViewScreen(),
                            ),
                          ),
                        ),
                      ],
                    ),
                  ),
                  const SizedBox(height: 16),
                  PCard(
                    padding: EdgeInsets.zero,
                    child: _Row(
                      icon: Icons.logout_outlined,
                      label: 'התנתקות',
                      danger: true,
                      onTap: () => context.read<AuthProvider>().signOut(),
                    ),
                  ),
                  const SizedBox(height: 6),
                  Text(
                    'Emotional Tutor · גרסה 0.9 (MVP)',
                    textAlign: TextAlign.center,
                    style: AppTextStyles.hint(context).copyWith(fontSize: 12),
                  ),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }

  Future<void> _save(Child cur) async {
    setState(() => _saving = true);
    final updated = cur.copyWith(
      settings: cur.settings.copyWith(
        sessionMinutes: _sessionMin,
        breakEveryMinutes: _breakMin,
      ),
    );
    // Capture providers BEFORE the first await so we don't read across the
    // async gap.
    final childProv = context.read<ChildProvider>();
    final config = context.read<ConfigProvider>();
    await childProv.save(updated);
    await config.setSessionMinutes(_sessionMin);
    await config.setBreakEveryMinutes(_breakMin);
    if (!mounted) return;
    setState(() {
      _saving = false;
      _dirty = false;
    });
  }

  Widget _section(String t) => Padding(
        padding: const EdgeInsets.only(right: 2, top: 10, bottom: 8),
        child: Text(t,
            style:
                AppTextStyles.title(context).copyWith(fontSize: 16)),
      );
}

class _Slider extends StatelessWidget {
  const _Slider({
    required this.label,
    required this.value,
    required this.min,
    required this.max,
    required this.step,
    required this.unit,
    required this.onChanged,
  });
  final String label;
  final int value;
  final int min;
  final int max;
  final int step;
  final String unit;
  final ValueChanged<int> onChanged;

  @override
  Widget build(BuildContext context) {
    final divisions = ((max - min) / step).round();
    return PCard(
      child: Column(
        children: [
          Row(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              Text(label, style: AppTextStyles.label(context)),
              Text(
                '$value $unit',
                style: AppTextStyles.display(context)
                    .copyWith(fontSize: 17, color: AppColors.sky),
              ),
            ],
          ),
          const SizedBox(height: 6),
          SliderTheme(
            data: SliderTheme.of(context).copyWith(
              activeTrackColor: AppColors.sky,
              inactiveTrackColor: AppColors.skySoft,
              thumbColor: AppColors.sky,
              overlayColor: AppColors.sky.withValues(alpha: 0.12),
              trackHeight: 6,
              thumbShape:
                  const RoundSliderThumbShape(enabledThumbRadius: 12),
            ),
            child: Slider(
              min: min.toDouble(),
              max: max.toDouble(),
              divisions: divisions,
              value: value.toDouble(),
              onChanged: (v) => onChanged(v.round()),
            ),
          ),
        ],
      ),
    );
  }
}

class _Row extends StatelessWidget {
  const _Row({
    required this.icon,
    required this.label,
    this.sub,
    this.onTap,
    this.danger = false,
  });
  final IconData icon;
  final String label;
  final String? sub;
  final VoidCallback? onTap;
  final bool danger;

  @override
  Widget build(BuildContext context) {
    return InkWell(
      onTap: onTap,
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 18, vertical: 14),
        child: Row(
          children: [
            Container(
              width: 42,
              height: 42,
              decoration: BoxDecoration(
                color: danger ? AppColors.coralSoft : AppColors.skySoft,
                borderRadius: BorderRadius.circular(12),
              ),
              alignment: Alignment.center,
              child: Icon(icon,
                  color: danger ? AppColors.coral : AppColors.sky, size: 22),
            ),
            const SizedBox(width: 14),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                mainAxisSize: MainAxisSize.min,
                children: [
                  Text(
                    label,
                    style: AppTextStyles.title(context).copyWith(
                      fontSize: 15.5,
                      color: danger ? AppColors.coral : AppColors.ink,
                    ),
                  ),
                  if (sub != null) ...[
                    const SizedBox(height: 2),
                    Text(sub!,
                        style: AppTextStyles.hint(context).copyWith(fontSize: 12.5)),
                  ],
                ],
              ),
            ),
            if (onTap != null)
              const Icon(Icons.chevron_left, color: AppColors.inkSoft),
          ],
        ),
      ),
    );
  }
}

class _Divider extends StatelessWidget {
  const _Divider();
  @override
  Widget build(BuildContext context) => Container(
        margin: const EdgeInsetsDirectional.only(start: 18, end: 18),
        height: 1.5,
        color: const Color(0xFFF0F4FA),
      );
}
