// ScreenHeader — title + optional subtitle + optional back chevron (RTL aware)
// + optional right-side slot (e.g. a notifications bell). Spec layout.

import 'package:flutter/material.dart';

import '../theme.dart';

class ScreenHeader extends StatelessWidget {
  const ScreenHeader({
    super.key,
    required this.title,
    this.subtitle,
    this.subtitleWidget,
    this.onBack,
    this.right,
  });

  final String title;
  final String? subtitle;
  /// If provided, renders this instead of [subtitle]. Use for interactive
  /// subtitles (e.g. a child-switcher chip).
  final Widget? subtitleWidget;
  final VoidCallback? onBack;
  final Widget? right;

  @override
  Widget build(BuildContext context) {
    final hasSubtitle = subtitleWidget != null || subtitle != null;
    return Padding(
      padding: EdgeInsets.fromLTRB(20, 18, 20, hasSubtitle ? 16 : 14),
      child: Row(
        children: [
          if (onBack != null) ...[
            _RoundIconButton(icon: Icons.arrow_forward, onTap: onBack!),
            const SizedBox(width: 12),
          ],
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              mainAxisSize: MainAxisSize.min,
              children: [
                Text(title, style: AppTextStyles.display(context).copyWith(fontSize: 24)),
                if (subtitleWidget != null) ...[
                  const SizedBox(height: 2),
                  subtitleWidget!,
                ] else if (subtitle != null) ...[
                  const SizedBox(height: 2),
                  Text(subtitle!, style: AppTextStyles.hint(context).copyWith(fontSize: 14)),
                ],
              ],
            ),
          ),
          ?right,
        ],
      ),
    );
  }
}

class _RoundIconButton extends StatelessWidget {
  const _RoundIconButton({required this.icon, required this.onTap});
  final IconData icon;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return InkWell(
      onTap: onTap,
      borderRadius: BorderRadius.circular(AppRadii.pill),
      child: Container(
        width: 42,
        height: 42,
        decoration: const BoxDecoration(
          color: Colors.white,
          shape: BoxShape.circle,
          boxShadow: AppShadow.soft,
        ),
        child: Icon(icon, size: 22, color: AppColors.ink),
      ),
    );
  }
}
