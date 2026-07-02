// TodayRing — circular progress ring showing minutes used today vs. the
// daily limit. The center prints the minutes count.

import 'dart:math' as math;

import 'package:flutter/material.dart';

import '../theme.dart';

class TodayRing extends StatelessWidget {
  const TodayRing({
    super.key,
    required this.usedMinutes,
    required this.limitMinutes,
    this.size = 78,
  });

  final int usedMinutes;
  final int limitMinutes;
  final double size;

  @override
  Widget build(BuildContext context) {
    final pct = limitMinutes == 0
        ? 0.0
        : (usedMinutes / limitMinutes).clamp(0.0, 1.0);
    return SizedBox(
      width: size,
      height: size,
      child: CustomPaint(
        painter: _RingPainter(pct: pct),
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Text(
              '$usedMinutes',
              style: AppTextStyles.display(context).copyWith(fontSize: 19),
            ),
            const SizedBox(height: 1),
            Text(
              'דק׳ היום',
              style: AppTextStyles.hint(context).copyWith(fontSize: 10),
            ),
          ],
        ),
      ),
    );
  }
}

class _RingPainter extends CustomPainter {
  _RingPainter({required this.pct});
  final double pct;

  @override
  void paint(Canvas canvas, Size size) {
    final c = size.center(Offset.zero);
    final r = math.min(size.width, size.height) / 2 - 4;
    final track = Paint()
      ..color = AppColors.skySoft
      ..style = PaintingStyle.stroke
      ..strokeWidth = 8;
    final fill = Paint()
      ..color = AppColors.sky
      ..style = PaintingStyle.stroke
      ..strokeWidth = 8
      ..strokeCap = StrokeCap.round;
    canvas.drawCircle(c, r, track);
    if (pct <= 0) return;
    canvas.drawArc(
      Rect.fromCircle(center: c, radius: r),
      -math.pi / 2,
      2 * math.pi * pct,
      false,
      fill,
    );
  }

  @override
  bool shouldRepaint(_RingPainter o) => o.pct != pct;
}
