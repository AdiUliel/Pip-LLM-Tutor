// PStepper — number stepper with circular +/- buttons and a centered value.
// Used for the child's age in the wizard.

import 'package:flutter/material.dart';

import '../theme.dart';

class PStepper extends StatelessWidget {
  const PStepper({
    super.key,
    required this.value,
    required this.min,
    required this.max,
    required this.onChanged,
    this.hint,
  });

  final int value;
  final int min;
  final int max;
  final ValueChanged<int> onChanged;
  final String? hint;

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        _RoundBtn(
          glyph: '−',
          onTap: value > min ? () => onChanged(value - 1) : null,
        ),
        const SizedBox(width: 16),
        SizedBox(
          width: 54,
          child: Text(
            '$value',
            textAlign: TextAlign.center,
            style: AppTextStyles.display(context).copyWith(fontSize: 34),
          ),
        ),
        const SizedBox(width: 16),
        _RoundBtn(
          glyph: '+',
          onTap: value < max ? () => onChanged(value + 1) : null,
        ),
        if (hint != null) ...[
          const Spacer(),
          Text(hint!, style: AppTextStyles.hint(context)),
        ],
      ],
    );
  }
}

class _RoundBtn extends StatelessWidget {
  const _RoundBtn({required this.glyph, required this.onTap});
  final String glyph;
  final VoidCallback? onTap;

  @override
  Widget build(BuildContext context) {
    final disabled = onTap == null;
    return InkResponse(
      onTap: onTap,
      radius: 30,
      child: AnimatedContainer(
        duration: const Duration(milliseconds: 120),
        width: 52,
        height: 52,
        decoration: BoxDecoration(
          shape: BoxShape.circle,
          color: Colors.white,
          border: Border.all(
            color: disabled ? AppColors.skySoft : AppColors.sky,
            width: 2,
          ),
        ),
        alignment: Alignment.center,
        child: Text(
          glyph,
          style: AppTextStyles.display(context).copyWith(
            fontSize: 26,
            color: disabled ? AppColors.inkSoft : AppColors.sky,
            height: 1,
          ),
        ),
      ),
    );
  }
}
