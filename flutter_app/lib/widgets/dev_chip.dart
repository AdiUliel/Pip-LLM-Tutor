// DevChip — small pill that shows the device's online/searching/offline state.
// Translated from `.dev-chip` in ~/Downloads/ioT/theme.css.

import 'package:flutter/material.dart';

import '../theme.dart';

enum DevChipState { online, searching, offline }

class DevChip extends StatefulWidget {
  const DevChip({
    super.key,
    required this.label,
    required this.state,
  });

  final String label;
  final DevChipState state;

  @override
  State<DevChip> createState() => _DevChipState();
}

class _DevChipState extends State<DevChip>
    with SingleTickerProviderStateMixin {
  late final AnimationController _beat = AnimationController(
    vsync: this,
    duration: const Duration(milliseconds: 1600),
  )..repeat();

  @override
  void dispose() {
    _beat.dispose();
    super.dispose();
  }

  Color get _dotColor {
    switch (widget.state) {
      case DevChipState.online:
        return AppColors.mint;
      case DevChipState.searching:
        return AppColors.sun;
      case DevChipState.offline:
        return const Color(0xFFC9D0DE);
    }
  }

  Color get _borderColor {
    switch (widget.state) {
      case DevChipState.online:
        return AppColors.mintSoft;
      case DevChipState.searching:
        return AppColors.sunSoft;
      case DevChipState.offline:
        return const Color(0xFFF0D2D2);
    }
  }

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsetsDirectional.only(start: 12, end: 14, top: 7, bottom: 7),
      decoration: BoxDecoration(
        color: Colors.white,
        borderRadius: BorderRadius.circular(AppRadii.pill),
        border: Border.all(color: _borderColor, width: 2),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          AnimatedBuilder(
            animation: _beat,
            builder: (context, _) {
              final p = widget.state == DevChipState.offline ? 0.0 : _beat.value;
              return Container(
                width: 10 + p * 4,
                height: 10 + p * 4,
                decoration: BoxDecoration(
                  color: _dotColor,
                  shape: BoxShape.circle,
                  boxShadow: widget.state == DevChipState.offline
                      ? null
                      : [
                          BoxShadow(
                            color: _dotColor.withValues(alpha: 0.5 * (1 - p)),
                            blurRadius: p * 8,
                            spreadRadius: p * 4,
                          ),
                        ],
                ),
              );
            },
          ),
          const SizedBox(width: 8),
          Text(
            widget.label,
            style: const TextStyle(
              fontWeight: FontWeight.w700,
              fontSize: 13.5,
              color: AppColors.ink,
            ),
          ),
        ],
      ),
    );
  }
}
