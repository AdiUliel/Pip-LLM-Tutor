// PairingSheet — 6-digit code entry that resolves to a deviceId.
//
// The ESP32 displays a 6-digit code on its screen (derived from its MAC, so
// stable across boots). The parent types it here; we look up
// `deviceState/{deviceIdPrefix}{code}` in Firestore and accept the pairing
// only if the doc exists *and* its `lastHeartbeat` is within
// `pairingMaxHeartbeatAgeSec` — i.e. the device is alive right now.
//
// Returns the resolved deviceId via [showPairingSheet] (or null on cancel).

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
    final deviceId = '${AppConstants.deviceIdPrefix}$code';
    setState(() {
      _checking = true;
      _error = null;
    });

    try {
      final svc = context.read<DeviceSyncService>();
      final state = await svc.tryFetch(deviceId);

      if (!mounted) return;

      if (state == null || state.lastHeartbeat == null) {
        setState(() {
          _error = 'לא נמצא התקן עם הקוד הזה. ודאו שהמכשיר דולק ומחובר ל-Wi-Fi.';
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

      Navigator.of(context).pop(deviceId);
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
    );
  }
}
