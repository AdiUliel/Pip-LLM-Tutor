// PCard — the soft white card primitive matching `.p-card` in the design.

import 'package:flutter/material.dart';

import '../theme.dart';

class PCard extends StatelessWidget {
  const PCard({
    super.key,
    required this.child,
    this.padding = const EdgeInsets.all(20),
    this.onTap,
    this.borderColor,
    this.background,
  });

  final Widget child;
  final EdgeInsets padding;
  final VoidCallback? onTap;
  final Color? borderColor;
  final Color? background;

  @override
  Widget build(BuildContext context) {
    final box = Container(
      padding: padding,
      decoration: BoxDecoration(
        color: background ?? AppColors.card,
        borderRadius: BorderRadius.circular(AppRadii.md),
        boxShadow: AppShadow.soft,
        border: borderColor == null
            ? null
            : Border.all(color: borderColor!, width: 2),
      ),
      child: child,
    );
    if (onTap == null) return box;
    return InkWell(
      onTap: onTap,
      borderRadius: BorderRadius.circular(AppRadii.md),
      child: box,
    );
  }
}
