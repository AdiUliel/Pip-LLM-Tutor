// PairingSheet — 6-digit code entry that resolves to a deviceId.
//
// The ESP32 displays a 6-digit code derived from its MAC (stable across boots)
// and publishes `pairingCodes/{deviceIdPrefix}{code} = { firebaseUid }`. The
// parent types the code here; we resolve it to the device's Firebase UID
// (which is what `children.deviceId` must hold so the cloud's identify-child
// flow matches), then verify `deviceState/{firebaseUid}.lastHeartbeat` is
// within `pairingMaxHeartbeatAgeSec` — i.e. the device is alive right now.
//
// Returns the resolved deviceId (= the Firebase UID) via [showPairingSheet]
// (or null on cancel).

import 'package:cloud_firestore/cloud_firestore.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:provider/provider.dart';

import '../constants.dart';
import '../services/device_sync_service.dart';
import '../theme.dart';
import 'dev_chip.dart';
import 'p_card.dart';
import 'robot_face.dart';

/// Shows the pairing sheet and returns the resolved deviceId on success,
/// or null if the user dismissed it.
Future<String?> showPairingSheet(BuildContext context) {
  return showModalBottomSheet<String>(
    context: context,
    isScrollControlled: true,
    backgroundColor: Colors.transparent,
    builder: (_) => const _PairingSheet(),
  );
}

class _PairingSheet extends StatefulWidget {
  const _PairingSheet();

  @override
  State<_PairingSheet> createState() => _PairingSheetState();
}

class _PairingSheetState extends State<_PairingSheet> {
  final _ctrl = TextEditingController();
  final _focus = FocusNode();
  bool _checking = false;
  String? _error;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) => _focus.requestFocus());
  }

  @override
  void dispose() {
    _ctrl.dispose();
    _focus.dispose();
    super.dispose();
  }

  bool get _isComplete => _ctrl.text.length == AppConstants.pairingCodeLength;

  Future<void> _submit() async {
    if (!_isComplete || _checking) return;
    final code = _ctrl.text;
    final pairingDocId = '${AppConstants.deviceIdPrefix}$code';
    setState(() {
      _checking = true;
      _error = null;
    });

    try {
      // Resolve the user-visible code to the device's Firebase UID via the
      // pairingCodes mapping the device publishes on every boot.
      final pairingSnap = await FirebaseFirestore.instance
          .collection('pairingCodes')
          .doc(pairingDocId)
          .get();

      if (!mounted) return;

      if (!pairingSnap.exists) {
        setState(() {
          _error = 'לא נמצא התקן עם הקוד הזה. ודאו שהמכשיר דולק ומחובר ל-Wi-Fi.';
          _checking = false;
        });
        return;
      }
      final firebaseUid = pairingSnap.data()?['firebaseUid'] as String?;
      if (firebaseUid == null || firebaseUid.isEmpty) {
        setState(() {
          _error = 'הקוד תקין אך ההתקן עוד לא הזדהה. נסו שוב בעוד רגע.';
          _checking = false;
        });
        return;
      }

      // Verify the device is actually alive right now by checking heartbeat
      // freshness on its deviceState doc (keyed by the Firebase UID).
      final svc = context.read<DeviceSyncService>();
      final state = await svc.tryFetch(firebaseUid);

      if (!mounted) return;

      if (state == null || state.lastHeartbeat == null) {
        setState(() {
          _error = 'ההתקן נמצא, אבל לא דיווח עדכני. ודאו שהוא דולק ונסו שוב בעוד רגע.';
          _checking = false;
        });
        return;
      }
      final age = DateTime.now().difference(state.lastHeartbeat!).inSeconds;
      if (age > AppConstants.pairingMaxHeartbeatAgeSec) {
        setState(() {
          _error =
              'ההתקן נמצא, אבל לא דיווח עדכני. ודאו שהוא דולק ונסו שוב בעוד רגע.';
          _checking = false;
        });
        return;
      }

      Navigator.of(context).pop(firebaseUid);
    } catch (_) {
      if (!mounted) return;
      setState(() {
        _error = 'שגיאה בחיבור לשרת. בדקו את האינטרנט ונסו שוב.';
        _checking = false;
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    final bottomInset = MediaQuery.of(context).viewInsets.bottom;
    return Padding(
      padding: EdgeInsets.only(bottom: bottomInset),
      child: Container(
        decoration: const BoxDecoration(
          color: AppColors.paper,
          borderRadius: BorderRadius.vertical(top: Radius.circular(AppRadii.lg)),
        ),
        padding: const EdgeInsets.fromLTRB(20, 14, 20, 24),
        child: SafeArea(
          top: false,
          child: SingleChildScrollView(
            child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              Container(
                width: 44,
                height: 5,
                margin: const EdgeInsets.only(bottom: 18),
                decoration: BoxDecoration(
                  color: AppColors.divider,
                  borderRadius: BorderRadius.circular(99),
                ),
              ),
              const RobotFace(emotion: RobotEmotion.listening, size: 110),
              const SizedBox(height: 14),
              Text(
                'חיבור התקן',
                style: AppTextStyles.display(context).copyWith(fontSize: 22),
              ),
              const SizedBox(height: 6),
              Text(
                'הקלידו את הקוד בן ${AppConstants.pairingCodeLength} הספרות '
                'שמופיע על מסך ההתקן.',
                textAlign: TextAlign.center,
                style: AppTextStyles.hint(context).copyWith(fontSize: 14),
              ),
              const SizedBox(height: 18),
              const _InstructionsCard(),
              const SizedBox(height: 14),
              PCard(
                padding: const EdgeInsets.fromLTRB(16, 14, 16, 16),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    Directionality(
                      textDirection: TextDirection.ltr,
                      child: TextField(
                        controller: _ctrl,
                        focusNode: _focus,
                        autofocus: true,
                        keyboardType: TextInputType.number,
                        textAlign: TextAlign.center,
                        maxLength: AppConstants.pairingCodeLength,
                        inputFormatters: [
                          FilteringTextInputFormatter.digitsOnly,
                          LengthLimitingTextInputFormatter(
                              AppConstants.pairingCodeLength),
                        ],
                        style: AppTextStyles.display(context).copyWith(
                          fontSize: 32,
                          letterSpacing: 10,
                        ),
                        decoration: const InputDecoration(
                          hintText: '------',
                          counterText: '',
                        ),
                        onChanged: (_) => setState(() => _error = null),
                        onSubmitted: (_) => _submit(),
                      ),
                    ),
                    if (_error != null) ...[
                      const SizedBox(height: 12),
                      Text(
                        _error!,
                        textAlign: TextAlign.center,
                        style: AppTextStyles.hint(context).copyWith(
                          color: AppColors.coral,
                        ),
                      ),
                    ],
                    if (_checking) ...[
                      const SizedBox(height: 12),
                      const Center(
                        child: DevChip(
                          label: 'מחפש התקן...',
                          state: DevChipState.searching,
                        ),
                      ),
                    ],
                  ],
                ),
              ),
              const SizedBox(height: 18),
              ElevatedButton(
                onPressed: _isComplete && !_checking ? _submit : null,
                child: Text(_checking ? 'מתחבר…' : 'אישור'),
              ),
              const SizedBox(height: 8),
              TextButton(
                onPressed: _checking ? null : () => Navigator.of(context).pop(),
                child: const Text('ביטול'),
              ),
            ],
          ),
          ),
        ),
      ),
    );
  }
}

/// Short "how to get the code" steps, shown above the code field so the parent
/// knows how to make the 6-digit code appear on the device screen.
class _InstructionsCard extends StatelessWidget {
  const _InstructionsCard();

  @override
  Widget build(BuildContext context) {
    return PCard(
      background: AppColors.skySoft.withValues(alpha: 0.45),
      borderColor: AppColors.skySoft,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              const Icon(Icons.help_outline, size: 18, color: AppColors.sky),
              const SizedBox(width: 6),
              Text(
                'איך מוצאים את הקוד?',
                style: AppTextStyles.label(context),
              ),
            ],
          ),
          const SizedBox(height: 10),
          const _Step('1', 'ודאו שהראוטר וה‑Wi‑Fi בבית דלוקים.'),
          const _Step('2', 'הדליקו את Pip והמתינו כ‑10–20 שניות שיתחבר לרשת.'),
          const _Step(
            '3',
            'כשההתקן מחובר, קוד בן 6 ספרות יופיע על צג ההתקן. אם לא הופיע — '
                'כבו והדליקו את Pip מחדש.',
          ),
          const _Step('4', 'הקלידו כאן את הקוד שמופיע על הצג.', last: true),
        ],
      ),
    );
  }
}

class _Step extends StatelessWidget {
  const _Step(this.number, this.text, {this.last = false});
  final String number;
  final String text;
  final bool last;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: EdgeInsets.only(bottom: last ? 0 : 8),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Container(
            width: 22,
            height: 22,
            decoration: const BoxDecoration(
              color: AppColors.sky,
              shape: BoxShape.circle,
            ),
            alignment: Alignment.center,
            child: Text(
              number,
              style: const TextStyle(
                color: Colors.white,
                fontWeight: FontWeight.w800,
                fontSize: 12.5,
              ),
            ),
          ),
          const SizedBox(width: 10),
          Expanded(
            child: Padding(
              padding: const EdgeInsets.only(top: 2),
              child: Text(
                text,
                style: AppTextStyles.hint(context).copyWith(fontSize: 13.5),
              ),
            ),
          ),
        ],
      ),
    );
  }
}
