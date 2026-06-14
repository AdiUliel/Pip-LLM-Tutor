// ChildConfigScreen — edit the active child's profile: age, enabled subjects,
// per-subject topic focus + starting level. Writes back to Firestore via
// ChildProvider on save. Layout from ~/Downloads/ioT/screen-config.jsx.

import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../constants.dart';
import '../models/child.dart';
import '../providers/auth_provider.dart';
import '../providers/child_provider.dart';
import '../providers/device_provider.dart';
import '../providers/stats_provider.dart';
import '../services/firebase_service.dart';
import '../theme.dart';
import '../widgets/gender_picker.dart';
import '../widgets/p_card.dart';
import '../widgets/p_stepper.dart';
import '../widgets/screen_header.dart';

class ChildConfigScreen extends StatefulWidget {
  const ChildConfigScreen({super.key});

  @override
  State<ChildConfigScreen> createState() => _ChildConfigScreenState();
}

class _ChildConfigScreenState extends State<ChildConfigScreen> {
  late int _age;
  late Gender _gender;
  late Map<Subject, bool> _enabled;
  late Map<Subject, Set<String>> _topics; // selected predefined topic keys (multi-select, may be empty)
  late Map<Subject, List<String>> _customTopics; // free-text additions per subject
  late Map<Subject, int> _levelBucket; // 1=easy, 2=medium, 3=hard
  bool _initialized = false;
  bool _saving = false;
  bool _justSaved = false;

  @override
  Widget build(BuildContext context) {
    final child = context.watch<ChildProvider>().child;
    if (child == null) {
      return const Scaffold(body: Center(child: CircularProgressIndicator()));
    }
    if (!_initialized) {
      _initialized = true;
      _age = child.age;
      _gender = child.gender;
      _enabled = {
        for (final s in Subject.values)
          s: child.subjectsEnabled.contains(s),
      };
      _topics = {for (final s in Subject.values) s: <String>{}};
      _customTopics = {for (final s in Subject.values) s: <String>[]};
      for (final s in Subject.values) {
        for (final t in child.topicFocus[s] ?? const <String>[]) {
          if (_isPredefined(s, t)) {
            _topics[s]!.add(t);
          } else {
            _customTopics[s]!.add(t);
          }
        }
      }
      _levelBucket = {
        for (final s in Subject.values)
          s: _bucketForLevel(child.level[s] ?? 1),
      };
    }

    final canSave = _enabled.values.any((v) => v);

    return Scaffold(
      body: SafeArea(
        child: Column(
          children: [
            ScreenHeader(
              title: 'מה לומדים',
              subtitle: 'הפרופיל של ${child.name}',
              onBack: () => Navigator.of(context).maybePop(),
            ),
            Expanded(
              child: SingleChildScrollView(
                padding: const EdgeInsets.fromLTRB(20, 4, 20, 0),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    PCard(
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Text('גיל', style: AppTextStyles.label(context)),
                          const SizedBox(height: 8),
                          PStepper(
                            value: _age,
                            min: AppConstants.minChildAge + 1,
                            max: AppConstants.maxChildAge - 1,
                            hint:
                                'גילאי ${AppConstants.minChildAge + 1}–${AppConstants.maxChildAge - 1}',
                            onChanged: (v) => setState(() => _age = v),
                          ),
                        ],
                      ),
                    ),
                    const SizedBox(height: 14),
                    PCard(
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Text('בן או בת?', style: AppTextStyles.label(context)),
                          const SizedBox(height: 10),
                          GenderPicker(
                            value: _gender,
                            onChanged: (g) => setState(() => _gender = g),
                          ),
                        ],
                      ),
                    ),
                    const SizedBox(height: 20),
                    Padding(
                      padding: const EdgeInsets.only(right: 2, bottom: 10),
                      child: Text(
                        'מקצועות',
                        style:
                            AppTextStyles.title(context).copyWith(fontSize: 16),
                      ),
                    ),
                    for (final s in Subject.values) ...[
                      _SubjectBlock(
                        subject: s,
                        enabled: _enabled[s]!,
                        topics: _topics[s]!,
                        customTopics: _customTopics[s]!,
                        levelBucket: _levelBucket[s]!,
                        onToggle: (v) => setState(() => _enabled[s] = v),
                        onToggleTopic: (t) => setState(() {
                          if (_topics[s]!.contains(t)) {
                            _topics[s]!.remove(t);
                          } else {
                            _topics[s]!.add(t);
                          }
                        }),
                        onAddCustom: (t) {
                          final trimmed = t.trim();
                          if (trimmed.isEmpty) return;
                          setState(() {
                            if (!_customTopics[s]!.contains(trimmed)) {
                              _customTopics[s]!.add(trimmed);
                            }
                          });
                        },
                        onRemoveCustom: (t) =>
                            setState(() => _customTopics[s]!.remove(t)),
                        onLevel: (b) => setState(() => _levelBucket[s] = b),
                      ),
                      const SizedBox(height: 12),
                    ],
                    const SizedBox(height: 4),
                    Text(
                      'ההתקן יתעדכן אוטומטית עם השינויים',
                      textAlign: TextAlign.center,
                      style: AppTextStyles.hint(context),
                    ),
                    const SizedBox(height: 18),
                    _DeleteChildButton(child: child),
                    const SizedBox(height: 16),
                  ],
                ),
              ),
            ),
            Padding(
              padding: const EdgeInsets.fromLTRB(20, 8, 20, 22),
              child: ElevatedButton(
                onPressed: (!canSave || _saving) ? null : () => _save(child),
                child: Text(_justSaved
                    ? '✓ נשמר'
                    : (_saving ? 'שומר…' : 'שמירת שינויים')),
              ),
            ),
          ],
        ),
      ),
    );
  }

  Future<void> _save(Child cur) async {
    setState(() {
      _saving = true;
      _justSaved = false;
    });
    final enabled = <Subject>[
      for (final s in Subject.values)
        if (_enabled[s]!) s,
    ];
    final topicFocus = <Subject, List<String>>{
      for (final s in enabled)
        s: [..._topics[s]!, ..._customTopics[s]!],
    };
    final level = <Subject, int>{
      for (final s in enabled) s: _levelForBucket(_levelBucket[s]!),
    };
    final updated = cur.copyWith(
      age: _age,
      gender: _gender,
      subjectsEnabled: enabled,
      topicFocus: topicFocus,
      level: level,
    );
    await context.read<ChildProvider>().save(updated);
    if (!mounted) return;
    setState(() {
      _saving = false;
      _justSaved = true;
    });
    Future.delayed(const Duration(milliseconds: 900), () {
      if (mounted) setState(() => _justSaved = false);
    });
  }

  static const _predefined = {
    Subject.math: ['addition', 'subtraction', 'multiplication', 'division'],
    Subject.english: ['vocabulary', 'spelling', 'reading'],
  };
  static bool _isPredefined(Subject s, String key) =>
      _predefined[s]!.contains(key);

  static int _bucketForLevel(int level) {
    if (level <= 2) return 1;
    if (level <= 5) return 2;
    return 3;
  }

  static int _levelForBucket(int bucket) =>
      bucket == 1 ? 1 : (bucket == 2 ? 3 : 5);
}

class _SubjectBlock extends StatefulWidget {
  const _SubjectBlock({
    required this.subject,
    required this.enabled,
    required this.topics,
    required this.customTopics,
    required this.levelBucket,
    required this.onToggle,
    required this.onToggleTopic,
    required this.onAddCustom,
    required this.onRemoveCustom,
    required this.onLevel,
  });

  final Subject subject;
  final bool enabled;
  final Set<String> topics;
  final List<String> customTopics;
  final int levelBucket;
  final ValueChanged<bool> onToggle;
  final ValueChanged<String> onToggleTopic;
  final ValueChanged<String> onAddCustom;
  final ValueChanged<String> onRemoveCustom;
  final ValueChanged<int> onLevel;

  @override
  State<_SubjectBlock> createState() => _SubjectBlockState();
}

class _SubjectBlockState extends State<_SubjectBlock> {
  final _customCtrl = TextEditingController();

  @override
  void dispose() {
    _customCtrl.dispose();
    super.dispose();
  }

  void _submitCustom() {
    final t = _customCtrl.text.trim();
    if (t.isEmpty) return;
    widget.onAddCustom(t);
    _customCtrl.clear();
  }

  @override
  Widget build(BuildContext context) {
    final meta = subjectMeta[widget.subject]!;
    final predefined =
        _ChildConfigScreenState._predefined[widget.subject]!;
    return Opacity(
      opacity: widget.enabled ? 1 : 0.6,
      child: PCard(
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Container(
                  width: 46,
                  height: 46,
                  decoration: BoxDecoration(
                    color: meta.tint,
                    borderRadius: BorderRadius.circular(14),
                  ),
                  alignment: Alignment.center,
                  child: Text(meta.emoji, style: const TextStyle(fontSize: 24)),
                ),
                const SizedBox(width: 12),
                Expanded(
                  child: Text(
                    meta.heLabel,
                    style: AppTextStyles.title(context),
                  ),
                ),
                Switch(
                  value: widget.enabled,
                  onChanged: widget.onToggle,
                  activeThumbColor: Colors.white,
                  activeTrackColor: AppColors.sky,
                ),
              ],
            ),
            if (widget.enabled) ...[
              const SizedBox(height: 16),
              Text('נושאים להתמקדות (אפשר כמה או אף אחד)',
                  style: AppTextStyles.label(context).copyWith(fontSize: 14)),
              const SizedBox(height: 8),
              Wrap(
                spacing: 8,
                runSpacing: 8,
                children: [
                  for (final t in predefined)
                    _Chip(
                      label: AppConstants.topicHeLabels[t] ?? t,
                      selected: widget.topics.contains(t),
                      onTap: () => widget.onToggleTopic(t),
                    ),
                  for (final t in widget.customTopics)
                    _Chip(
                      label: '$t  ✕',
                      selected: true,
                      onTap: () => widget.onRemoveCustom(t),
                    ),
                ],
              ),
              const SizedBox(height: 10),
              Row(
                children: [
                  Expanded(
                    child: TextField(
                      controller: _customCtrl,
                      textInputAction: TextInputAction.done,
                      onSubmitted: (_) => _submitCustom(),
                      decoration: const InputDecoration(
                        hintText: 'נושא מותאם — הקלידו ולחצו +',
                      ),
                    ),
                  ),
                  const SizedBox(width: 8),
                  IconButton.filled(
                    onPressed: _submitCustom,
                    icon: const Icon(Icons.add),
                  ),
                ],
              ),
              const SizedBox(height: 14),
              Text('רמת התחלה',
                  style: AppTextStyles.label(context).copyWith(fontSize: 14)),
              const SizedBox(height: 8),
              Row(
                children: [
                  for (var n = 1; n <= 3; n++) ...[
                    Expanded(
                      child: _LevelSeg(
                        label: ['מתחילים', 'בינוני', 'מתקדמים'][n - 1],
                        selected: widget.levelBucket == n,
                        onTap: () => widget.onLevel(n),
                      ),
                    ),
                    if (n < 3) const SizedBox(width: 8),
                  ],
                ],
              ),
            ],
          ],
        ),
      ),
    );
  }
}

class _Chip extends StatelessWidget {
  const _Chip({
    required this.label,
    required this.selected,
    required this.onTap,
  });
  final String label;
  final bool selected;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return InkWell(
      onTap: onTap,
      borderRadius: BorderRadius.circular(AppRadii.pill),
      child: AnimatedContainer(
        duration: const Duration(milliseconds: 140),
        padding: const EdgeInsets.symmetric(horizontal: 15, vertical: 8),
        decoration: BoxDecoration(
          color: selected ? AppColors.skySoft : Colors.white,
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
            color: selected ? AppColors.sky : AppColors.inkSoft,
          ),
        ),
      ),
    );
  }
}

class _DeleteChildButton extends StatelessWidget {
  const _DeleteChildButton({required this.child});
  final Child child;

  @override
  Widget build(BuildContext context) {
    final auth = context.watch<AuthProvider>();
    final fb = context.read<FirebaseService>();
    final parentId = auth.user?.uid;
    if (parentId == null) return const SizedBox.shrink();

    return StreamBuilder<List<Child>>(
      stream: fb.watchChildrenOfParent(parentId),
      builder: (ctx, snap) {
        final all = snap.data ?? const <Child>[];
        // Per spec: only allow delete when at least one other child remains.
        if (all.length < 2) return const SizedBox.shrink();
        return OutlinedButton.icon(
          onPressed: () => _confirm(ctx, all),
          style: OutlinedButton.styleFrom(
            foregroundColor: AppColors.coral,
            side: const BorderSide(color: AppColors.coralSoft, width: 2),
            minimumSize: const Size.fromHeight(48),
          ),
          icon: const Icon(Icons.delete_outline_rounded,
              color: AppColors.coral),
          label: Text('מחק את ${child.name}'),
        );
      },
    );
  }

  Future<void> _confirm(BuildContext ctx, List<Child> all) async {
    final messenger = ScaffoldMessenger.of(ctx);
    final fb = ctx.read<FirebaseService>();
    final childProv = ctx.read<ChildProvider>();
    final deviceProv = ctx.read<DeviceProvider>();
    final statsProv = ctx.read<StatsProvider>();
    final navigator = Navigator.of(ctx);

    final ok = await showDialog<bool>(
      context: ctx,
      builder: (dCtx) => AlertDialog(
        title: const Text('מחיקת ילד'),
        content: Text('למחוק את ${child.name}? פעולה זו אינה הפיכה.'),
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
      // Hand off active to a sibling before deleting so the dashboard
      // doesn't try to read a doc that's about to disappear.
      final next = all.firstWhere((c) => c.id != child.id);
      await childProv.setActive(next.id);
      deviceProv.watch(next.deviceId);
      statsProv.load(next.id);
      await fb.deleteChild(child.id);
      if (navigator.canPop()) navigator.pop();
    } catch (e) {
      messenger.showSnackBar(
        SnackBar(content: Text('המחיקה נכשלה: $e')),
      );
    }
  }
}

class _LevelSeg extends StatelessWidget {
  const _LevelSeg({
    required this.label,
    required this.selected,
    required this.onTap,
  });
  final String label;
  final bool selected;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return InkWell(
      onTap: onTap,
      borderRadius: BorderRadius.circular(AppRadii.sm),
      child: AnimatedContainer(
        duration: const Duration(milliseconds: 140),
        padding: const EdgeInsets.symmetric(vertical: 10),
        alignment: Alignment.center,
        decoration: BoxDecoration(
          color: selected ? AppColors.skySoft : Colors.white,
          borderRadius: BorderRadius.circular(AppRadii.sm),
          border: Border.all(
            color: selected ? AppColors.sky : AppColors.skySoft,
            width: selected ? 2.5 : 2,
          ),
        ),
        child: Text(
          label,
          style: TextStyle(
            fontWeight: FontWeight.w800,
            fontSize: 14,
            color: selected ? AppColors.sky : AppColors.inkSoft,
          ),
        ),
      ),
    );
  }
}
