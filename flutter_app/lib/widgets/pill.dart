// Pill — small rounded chip used for stars / mood indicators on the
// dashboard. Matches `.dev-chip`-like small pill from the design.

import 'package:flutter/material.dart';

import '../theme.dart';

class Pill extends StatelessWidget {
  const Pill({
    super.key,
    required this.label,
    this.icon,
    this.color = AppColors.sky,
    this.background = AppColors.skySoft,
  });

  final String label;
  final Widget? icon;
  final Color color;
  final Color background;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 11, vertical: 5),
      decoration: BoxDecoration(
        color: background,
        borderRadius: BorderRadius.circular(AppRadii.pill),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          if (icon != null) ...[
            icon!,
            const SizedBox(width: 6),
          ],
          Text(
            label,
            style: TextStyle(
              fontWeight: FontWeight.w700,
              fontSize: 12.5,
              color: color,
            ),
          ),
        ],
      ),
    );
  }
}

/// MoodDot — solid circle in the mood's color, useful inside [Pill].
class MoodDot extends StatelessWidget {
  const MoodDot({super.key, required this.color, this.size = 11});
  final Color color;
  final double size;

  @override
  Widget build(BuildContext context) {
    return Container(
      width: size,
      height: size,
      decoration: BoxDecoration(color: color, shape: BoxShape.circle),
    );
  }
}
