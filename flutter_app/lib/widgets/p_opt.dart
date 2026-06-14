// POpt — selectable option card with leading icon + title + subtitle and an
// indicator on the right (radio or toggle, per [type]).

import 'package:flutter/material.dart';

import '../theme.dart';

enum POptIndicator { radio, toggle }

class POpt extends StatelessWidget {
  const POpt({
    super.key,
    required this.title,
    this.subtitle,
    required this.selected,
    required this.onTap,
    this.leading,
    this.indicator = POptIndicator.radio,
    this.leadingTint,
  });

  final String title;
  final String? subtitle;
  final bool selected;
  final VoidCallback onTap;
  final Widget? leading;
  final POptIndicator indicator;
  final Color? leadingTint;

  @override
  Widget build(BuildContext context) {
    return InkWell(
      onTap: onTap,
      borderRadius: BorderRadius.circular(AppRadii.md),
      child: AnimatedContainer(
        duration: const Duration(milliseconds: 140),
        padding: const EdgeInsets.all(18),
        decoration: BoxDecoration(
          color: selected ? AppColors.skySoft : Colors.white,
          borderRadius: BorderRadius.circular(AppRadii.md),
          border: Border.all(
            color: selected ? AppColors.sky : AppColors.skySoft,
            width: 2.5,
          ),
        ),
        child: Row(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            if (leading != null) ...[
              Container(
                width: 54,
                height: 54,
                decoration: BoxDecoration(
                  color: leadingTint ?? AppColors.skySoft,
                  borderRadius: BorderRadius.circular(16),
                ),
                alignment: Alignment.center,
                child: leading,
              ),
              const SizedBox(width: 14),
            ],
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    title,
                    style: AppTextStyles.title(context).copyWith(fontSize: 18),
                  ),
                  if (subtitle != null) ...[
                    const SizedBox(height: 4),
                    Text(subtitle!, style: AppTextStyles.hint(context)),
                  ],
                ],
              ),
            ),
            const SizedBox(width: 10),
            indicator == POptIndicator.radio
                ? _Radio(selected: selected)
                : _Toggle(selected: selected),
          ],
        ),
      ),
    );
  }
}

class _Radio extends StatelessWidget {
  const _Radio({required this.selected});
  final bool selected;

  @override
  Widget build(BuildContext context) {
    return Container(
      width: 24,
      height: 24,
      decoration: BoxDecoration(
        shape: BoxShape.circle,
        border: Border.all(color: AppColors.sky, width: 2.5),
      ),
      alignment: Alignment.center,
      child: selected
          ? Container(
              width: 13,
              height: 13,
              decoration: const BoxDecoration(
                color: AppColors.sky,
                shape: BoxShape.circle,
              ),
            )
          : null,
    );
  }
}

class _Toggle extends StatelessWidget {
  const _Toggle({required this.selected});
  final bool selected;

  @override
  Widget build(BuildContext context) {
    return AnimatedContainer(
      duration: const Duration(milliseconds: 180),
      width: 46,
      height: 28,
      padding: const EdgeInsets.all(3),
      decoration: BoxDecoration(
        color: selected ? AppColors.sky : const Color(0xFFD5DEEE),
        borderRadius: BorderRadius.circular(AppRadii.pill),
      ),
      child: Align(
        alignment: selected ? Alignment.centerLeft : Alignment.centerRight,
        // ↑ in RTL, "centerLeft" → visually right side, where the knob should
        //   end up when ON (the design slides knob from left to right in LTR,
        //   which mirrors to right-to-left in RTL).
        child: Container(
          width: 22,
          height: 22,
          decoration: const BoxDecoration(
            color: Colors.white,
            shape: BoxShape.circle,
          ),
        ),
      ),
    );
  }
}
