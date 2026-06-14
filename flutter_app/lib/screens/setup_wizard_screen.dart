// SetupWizardScreen — 6 steps: welcome, connect, profile, level, subjects, done.
// Layout translated from ~/Downloads/ioT/wizard.jsx. Writes Child to Firestore
// via ChildProvider when the parent finishes the flow.

import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../constants.dart';
import '../models/child.dart';
import '../providers/auth_provider.dart';
import '../providers/child_provider.dart';
import '../theme.dart';
import '../widgets/dev_chip.dart';
import '../widgets/gender_picker.dart';
import '../widgets/p_card.dart';
import '../widgets/p_opt.dart';
import '../widgets/p_stepper.dart';
import '../widgets/robot_face.dart';
import '../widgets/wizard_progress.dart';

class SetupWizardScreen extends StatefulWidget {
  const SetupWizardScreen({super.key});

  @override
  State<SetupWizardScreen> createState() => _SetupWizardScreenState();
}

class _SetupWizardScreenState extends State<SetupWizardScreen> {
  int _step = 0;
  bool _connected = false;

  final _nameCtrl = TextEditingController();
  int _age = 8;
  Gender _gender = AppConstants.defaultGender;
  String _level = 'easy';
  final _subjects = <Subject, bool>{
    Subject.math: true,
    Subject.english: true,
  };

  late final String _deviceId;
  bool _saving = false;

  @override
  void initState() {
    super.initState();
    // Simulated auto-pair value (real device emits its own id over BLE/Wi-Fi).
    _deviceId =
        'TUTOR-${DateTime.now().millisecondsSinceEpoch.toRadixString(16).substring(6, 10).toUpperCase()}';
  }

  @override
  void dispose() {
    _nameCtrl.dispose();
    super.dispose();
  }

  bool get _canFinishSubjects => _subjects.values.any((v) => v);

  Future<void> _finish() async {
    final auth = context.read<AuthProvider>();
    final childProv = context.read<ChildProvider>();
    final parentId = auth.user?.uid ?? 'mock-parent';

    final levelOpt = levelOptions.firstWhere((l) => l.id == _level);
    final enabled = <Subject>[
      for (final e in _subjects.entries)
        if (e.value) e.key,
    ];
    final defaultLevel = levelOpt.startingLevel;

    final child = Child(
      id: '',
      parentId: parentId,
      name: _nameCtrl.text.trim(),
      age: _age,
      gender: _gender,
      subjectsEnabled: enabled,
      // No preset focus; parent picks specific topics later in the config screen.
      topicFocus: {for (final s in enabled) s: const <String>[]},
      level: {for (final s in enabled) s: defaultLevel},
      settings: ChildSettings.defaults(),
      deviceId: _deviceId,
      createdAt: DateTime.now(),
    );

    setState(() => _saving = true);
    final id = await childProv.save(child);
    await childProv.setActive(id);
    if (!mounted) return;
    setState(() => _saving = false);
    // If the wizard was pushed manually (Add child from the dashboard),
    // return to the previous screen. Otherwise AuthGate hands off to the
    // dashboard once the children list updates.
    final nav = Navigator.of(context);
    if (nav.canPop()) nav.pop();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: SafeArea(
        child: Column(
          children: [
            WizardProgress(current: _step.clamp(1, 4)),
            Expanded(
              child: AnimatedSwitcher(
                duration: const Duration(milliseconds: 220),
                child: KeyedSubtree(
                  key: ValueKey(_step),
                  child: _stepView(),
                ),
              ),
            ),
            _footer(),
          ],
        ),
      ),
    );
  }

  // ─────────────────────────── per-step content ───────────────────────────

  Widget _stepView() {
    switch (_step) {
      case 0:
        return _welcome();
      case 1:
        return _connect();
      case 2:
        return _profile();
      case 3:
        return _level3();
      case 4:
        return _subjectsStep();
      default:
        return _done();
    }
  }

  Widget _welcome() {
    return Padding(
      padding: const EdgeInsets.fromLTRB(20, 12, 20, 0),
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          const Spacer(),
          const RobotFace(emotion: RobotEmotion.happy, size: 190),
          const SizedBox(height: 14),
          Text(
            'ברוכים הבאים! 👋',
            textAlign: TextAlign.center,
            style: AppTextStyles.display(context).copyWith(fontSize: 27),
          ),
          const SizedBox(height: 8),
          SizedBox(
            width: 300,
            child: Text(
              'בכמה צעדים קצרים נחבר את ההתקן ונגדיר פרופיל לימוד אישי לילד שלכם.',
              textAlign: TextAlign.center,
              style: AppTextStyles.hint(context).copyWith(fontSize: 16),
            ),
          ),
          const SizedBox(height: 18),
          const DevChip(label: 'התקנה', state: DevChipState.searching),
          const Spacer(),
        ],
      ),
    );
  }

  Widget _connect() {
    // Simulate pairing 2.6 seconds after entering this step.
    if (!_connected) {
      Future.delayed(const Duration(milliseconds: 2600), () {
        if (mounted && _step == 1 && !_connected) {
          setState(() => _connected = true);
        }
      });
    }
    return Padding(
      padding: const EdgeInsets.fromLTRB(20, 4, 20, 0),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          _header('חיבור ההתקן', 'צעד 1 מתוך 4'),
          Center(
            child: RobotFace(
              emotion: _connected ? RobotEmotion.happy : RobotEmotion.listening,
              size: 180,
            ),
          ),
          const SizedBox(height: 16),
          Center(
            child: DevChip(
              label: _connected ? 'ההתקן מחובר ומוכן!' : 'מחפש את ההתקן...',
              state:
                  _connected ? DevChipState.online : DevChipState.searching,
            ),
          ),
          const SizedBox(height: 18),
          PCard(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  children: [
                    const Icon(Icons.wifi, color: AppColors.sky, size: 20),
                    const SizedBox(width: 8),
                    Text(
                      'חיבור ל-Wi-Fi',
                      style: AppTextStyles.label(context),
                    ),
                  ],
                ),
                const SizedBox(height: 10),
                Text(
                  'ודאו שההתקן דולק ומחובר לאותה רשת Wi-Fi כמו הטלפון. הנורית על ההתקן תהבהב בכחול עד שיתחבר.',
                  style: AppTextStyles.hint(context),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }

  Widget _profile() {
    return Padding(
      padding: const EdgeInsets.fromLTRB(20, 4, 20, 0),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          _header('פרטי הילד', 'צעד 2 מתוך 4'),
          PCard(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text('איך קוראים לילד/ה?',
                    style: AppTextStyles.label(context)),
                const SizedBox(height: 8),
                TextField(
                  controller: _nameCtrl,
                  maxLength: 20,
                  autofocus: true,
                  decoration: const InputDecoration(
                    hintText: 'לדוגמה: נועה',
                    counterText: '',
                  ),
                  onChanged: (_) => setState(() {}),
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
          const SizedBox(height: 14),
          PCard(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text('בן/בת כמה?', style: AppTextStyles.label(context)),
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
        ],
      ),
    );
  }

  Widget _level3() {
    return Padding(
      padding: const EdgeInsets.fromLTRB(20, 4, 20, 0),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          _header('רמת התחלה', 'תמיד אפשר לשנות אחר כך'),
          for (final L in levelOptions) ...[
            POpt(
              title: L.heTitle,
              subtitle: L.heSub,
              selected: _level == L.id,
              onTap: () => setState(() => _level = L.id),
              leading: RobotFace(emotion: L.emotion, size: 46),
              leadingTint: AppColors.skySoft,
            ),
            const SizedBox(height: 12),
          ],
        ],
      ),
    );
  }

  Widget _subjectsStep() {
    return Padding(
      padding: const EdgeInsets.fromLTRB(20, 4, 20, 0),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          _header('מה נלמד?', 'בחרו מקצוע אחד או יותר'),
          for (final s in Subject.values) ...[
            POpt(
              title: subjectMeta[s]!.heLabel,
              subtitle: s == Subject.math
                  ? 'חיבור, חיסור, כפל ועוד'
                  : 'אוצר מילים ואיות בסיסי',
              selected: _subjects[s]!,
              indicator: POptIndicator.toggle,
              onTap: () => setState(
                () => _subjects[s] = !_subjects[s]!,
              ),
              leading: Text(
                subjectMeta[s]!.emoji,
                style: const TextStyle(fontSize: 28),
              ),
              leadingTint: subjectMeta[s]!.tint,
            ),
            const SizedBox(height: 12),
          ],
          if (!_canFinishSubjects) ...[
            const SizedBox(height: 4),
            Text(
              'בחרו לפחות מקצוע אחד',
              textAlign: TextAlign.center,
              style: AppTextStyles.hint(context).copyWith(color: AppColors.coral),
            ),
          ],
        ],
      ),
    );
  }

  Widget _done() {
    final lvl = levelOptions.firstWhere((l) => l.id == _level);
    final subjLabels = [
      for (final e in _subjects.entries)
        if (e.value) subjectMeta[e.key]!.heLabel,
    ].join(' · ');
    return Padding(
      padding: const EdgeInsets.fromLTRB(20, 8, 20, 0),
      child: Column(
        children: [
          const Spacer(),
          const RobotFace(emotion: RobotEmotion.celebrating, size: 200),
          const SizedBox(height: 10),
          Text(
            'הכל מוכן! 🎉',
            textAlign: TextAlign.center,
            style: AppTextStyles.display(context).copyWith(fontSize: 26),
          ),
          const SizedBox(height: 8),
          SizedBox(
            width: 280,
            child: RichText(
              textAlign: TextAlign.center,
              text: TextSpan(
                style: AppTextStyles.hint(context).copyWith(fontSize: 16),
                children: [
                  const TextSpan(text: 'הפרופיל של '),
                  TextSpan(
                    text: _nameCtrl.text.trim().isEmpty
                        ? 'הילד'
                        : _nameCtrl.text.trim(),
                    style: const TextStyle(
                      color: AppColors.ink,
                      fontWeight: FontWeight.w800,
                    ),
                  ),
                  const TextSpan(text: ' נוצר וההתקן מחובר. אפשר להתחיל ללמוד!'),
                ],
              ),
            ),
          ),
          const SizedBox(height: 16),
          PCard(
            child: Column(
              children: [
                _row('שם',
                    _nameCtrl.text.trim().isEmpty ? '—' : _nameCtrl.text.trim()),
                _row('מגדר', '${genderEmoji[_gender]} ${genderHeLabel[_gender]}'),
                _row('גיל', '$_age'),
                _row('רמה', lvl.heTitle),
                _row('מקצועות', subjLabels.isEmpty ? '—' : subjLabels, last: true),
              ],
            ),
          ),
          const Spacer(),
        ],
      ),
    );
  }

  Widget _header(String title, String? sub) {
    return Align(
      alignment: AlignmentDirectional.centerEnd,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.end,
        children: [
          const SizedBox(height: 14),
          Text(title,
              textAlign: TextAlign.start,
              style: AppTextStyles.display(context).copyWith(fontSize: 24)),
          if (sub != null) ...[
            const SizedBox(height: 2),
            Text(sub, style: AppTextStyles.hint(context).copyWith(fontSize: 14)),
          ],
          const SizedBox(height: 16),
        ],
      ),
    );
  }

  Widget _row(String k, String v, {bool last = false}) {
    return Container(
      padding: const EdgeInsets.symmetric(vertical: 11),
      decoration: BoxDecoration(
        border: last
            ? null
            : const Border(bottom: BorderSide(color: AppColors.divider, width: 1.5)),
      ),
      child: Row(
        mainAxisAlignment: MainAxisAlignment.spaceBetween,
        children: [
          Text(v,
              style: const TextStyle(
                fontWeight: FontWeight.w700,
                fontSize: 15.5,
                color: AppColors.ink,
              )),
          Text(k, style: AppTextStyles.hint(context).copyWith(fontSize: 15)),
        ],
      ),
    );
  }

  // ─────────────────────────── footer (buttons) ───────────────────────────

  Widget _footer() {
    final buttons = _buttons();
    return Padding(
      padding: const EdgeInsets.fromLTRB(20, 8, 20, 22),
      child: Row(children: buttons),
    );
  }

  List<Widget> _buttons() {
    switch (_step) {
      case 0:
        return [
          Expanded(
            child: ElevatedButton(
              onPressed: () => setState(() => _step = 1),
              child: const Text('בואו נתחיל'),
            ),
          ),
        ];
      case 1:
        return [
          Expanded(
            child: ElevatedButton(
              onPressed: _connected
                  ? () => setState(() => _step = 2)
                  : null,
              child: Text(_connected ? 'המשך' : 'מחבר...'),
            ),
          ),
        ];
      case 2:
        return [
          SizedBox(
            width: 130,
            child: OutlinedButton(
              onPressed: () => setState(() => _step = 1),
              child: const Text('חזרה'),
            ),
          ),
          const SizedBox(width: 10),
          Expanded(
            child: ElevatedButton(
              onPressed: _nameCtrl.text.trim().isEmpty
                  ? null
                  : () => setState(() => _step = 3),
              child: const Text('המשך'),
            ),
          ),
        ];
      case 3:
        return [
          SizedBox(
            width: 130,
            child: OutlinedButton(
              onPressed: () => setState(() => _step = 2),
              child: const Text('חזרה'),
            ),
          ),
          const SizedBox(width: 10),
          Expanded(
            child: ElevatedButton(
              onPressed: () => setState(() => _step = 4),
              child: const Text('המשך'),
            ),
          ),
        ];
      case 4:
        return [
          SizedBox(
            width: 130,
            child: OutlinedButton(
              onPressed: () => setState(() => _step = 3),
              child: const Text('חזרה'),
            ),
          ),
          const SizedBox(width: 10),
          Expanded(
            child: ElevatedButton(
              onPressed: _canFinishSubjects
                  ? () => setState(() => _step = 5)
                  : null,
              child: const Text('סיום'),
            ),
          ),
        ];
      default:
        return [
          Expanded(
            child: ElevatedButton(
              onPressed: _saving ? null : _finish,
              child: Text(_saving ? 'שומר…' : 'כניסה ללוח הבקרה'),
            ),
          ),
        ];
    }
  }
}
