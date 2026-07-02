// GenderPicker — two equally-weighted card buttons (בן / בת). Hebrew grammar
// makes this material: the device addresses the child in masculine vs.
// feminine form depending on this choice.

import 'package:flutter/material.dart';

import '../constants.dart';
import '../theme.dart';

class GenderPicker extends StatelessWidget {
  const GenderPicker({
    super.key,
    required this.value,
    required this.onChanged,
  });

  final Gender value;
  final ValueChanged<Gender> onChanged;

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        for (var i = 0; i < Gender.values.length; i++) ...[
          Expanded(
            child: _GenderOption(
              gender: Gender.values[i],
              selected: value == Gender.values[i],
              onTap: () => onChanged(Gender.values[i]),
            ),
          ),
          if (i < Gender.values.length - 1) const SizedBox(width: 10),
        ],
      ],
    );
  }
}

class _GenderOption extends StatelessWidget {
  const _GenderOption({
    required this.gender,
    required this.selected,
    required this.onTap,
  });
  final Gender gender;
  final bool selected;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return InkWell(
      onTap: onTap,
      borderRadius: BorderRadius.circular(AppRadii.md),
      child: AnimatedContainer(
        duration: const Duration(milliseconds: 140),
        padding: const EdgeInsets.symmetric(vertical: 14),
        alignment: Alignment.center,
        decoration: BoxDecoration(
          color: selected ? AppColors.skySoft : Colors.white,
          borderRadius: BorderRadius.circular(AppRadii.md),
          border: Border.all(
            color: selected ? AppColors.sky : AppColors.skySoft,
            width: selected ? 2.5 : 2,
          ),
        ),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Text(
              genderEmoji[gender]!,
              style: const TextStyle(fontSize: 28),
            ),
            const SizedBox(height: 4),
            Text(
              genderHeLabel[gender]!,
              style: TextStyle(
                fontWeight: FontWeight.w800,
                fontSize: 15,
                color: selected ? AppColors.sky : AppColors.inkSoft,
              ),
            ),
          ],
        ),
      ),
    );
  }
}
