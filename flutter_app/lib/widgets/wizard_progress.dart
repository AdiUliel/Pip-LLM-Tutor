// WizardProgress — the 4-segment progress bar at the top of the setup wizard.

import 'package:flutter/material.dart';

import '../theme.dart';

class WizardProgress extends StatelessWidget {
  const WizardProgress({super.key, required this.current, this.total = 4});

  /// 1..total. 0 hides the bar (welcome screen).
  final int current;
  final int total;

  @override
  Widget build(BuildContext context) {
    if (current < 1 || current > total) return const SizedBox.shrink();
    return Padding(
      padding: const EdgeInsets.fromLTRB(20, 4, 20, 0),
      child: Row(
        children: [
          for (var i = 1; i <= total; i++) ...[
            Expanded(child: _segment(i)),
            if (i != total) const SizedBox(width: 7),
          ],
        ],
      ),
    );
  }

  Widget _segment(int i) {
    final done = i < current;
    final cur = i == current;
    return ClipRRect(
      borderRadius: BorderRadius.circular(99),
      child: Stack(
        children: [
          Container(height: 7, color: AppColors.skySoft),
          AnimatedFractionallySizedBox(
            duration: const Duration(milliseconds: 400),
            widthFactor: done ? 1.0 : (cur ? 0.5 : 0.0),
            child: Container(height: 7, color: AppColors.sky),
          ),
        ],
      ),
    );
  }
}
