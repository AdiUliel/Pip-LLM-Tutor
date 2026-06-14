// RobotFace — the animated tutor mascot.
// Translated from the Claude Design SVG (~/Downloads/ioT/robot.jsx).
// 9 emotions × per-emotion eye/mouth shapes, with bobbing, blink, talk, pulse,
// and twinkle animations driven by AnimationControllers.

import 'dart:math' as math;

import 'package:flutter/material.dart';

import '../constants.dart';
import '../theme.dart';

class RobotFace extends StatefulWidget {
  const RobotFace({
    super.key,
    this.emotion = RobotEmotion.neutral,
    this.color = AppColors.sky,
    this.size = 220,
    this.speed = 1.0,
  });

  final RobotEmotion emotion;
  final Color color;
  final double size;
  final double speed;

  @override
  State<RobotFace> createState() => _RobotFaceState();
}

class _RobotFaceState extends State<RobotFace> with TickerProviderStateMixin {
  late final AnimationController _bob;
  late final AnimationController _blink;
  late final AnimationController _talk;
  late final AnimationController _pulse;

  @override
  void initState() {
    super.initState();
    _bob = AnimationController(
      vsync: this,
      duration: Duration(milliseconds: (3400 / widget.speed).round()),
    )..repeat(reverse: true);
    _blink = AnimationController(
      vsync: this,
      duration: Duration(milliseconds: (4000 / widget.speed).round()),
    )..repeat();
    _talk = AnimationController(
      vsync: this,
      duration: Duration(milliseconds: (320 / widget.speed).round()),
    )..repeat(reverse: true);
    _pulse = AnimationController(
      vsync: this,
      duration: Duration(milliseconds: (1100 / widget.speed).round()),
    )..repeat(reverse: true);
  }

  @override
  void dispose() {
    _bob.dispose();
    _blink.dispose();
    _talk.dispose();
    _pulse.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      width: widget.size,
      height: widget.size * 380 / 360,
      child: AnimatedBuilder(
        animation: Listenable.merge([_bob, _blink, _talk, _pulse]),
        builder: (context, _) => CustomPaint(
          painter: _RobotPainter(
            emotion: widget.emotion,
            color: widget.color,
            bob: _bobValue,
            blink: _blinkValue,
            talk: _talk.value,
            pulse: _pulse.value,
          ),
        ),
      ),
    );
  }

  /// Bobbing offset in viewport units (-9..9). For "celebrating" we bounce
  /// higher; for "sleepy" we do a gentle scale-breathe (encoded as bob<0.5).
  double get _bobValue {
    final t = math.sin(_bob.value * math.pi);
    if (widget.emotion == RobotEmotion.celebrating) return t * 16;
    if (widget.emotion == RobotEmotion.sleepy) return t * 3;
    return t * 9;
  }

  /// 1 = eyes open, ~0.1 = closed. Blink window is brief.
  double get _blinkValue {
    final t = _blink.value;
    if (t > 0.92 && t < 0.96) return 0.1;
    return 1;
  }
}

class _RobotPainter extends CustomPainter {
  _RobotPainter({
    required this.emotion,
    required this.color,
    required this.bob,
    required this.blink,
    required this.talk,
    required this.pulse,
  });

  final RobotEmotion emotion;
  final Color color;
  final double bob;
  final double blink;
  final double talk;
  final double pulse;

  // Glow / screen colors (constant across emotions).
  static const _glow = Color(0xFF5EE7E7);
  static const _screen = Color(0xFF15233B);

  @override
  void paint(Canvas canvas, Size size) {
    // Map the original 360×380 viewBox to whatever size the widget got.
    final scaleX = size.width / 360;
    final scaleY = size.height / 380;
    canvas.scale(scaleX, scaleY);

    // Bob translation applies to body+face only (not the listening rings).
    canvas.save();
    canvas.translate(0, bob);

    _drawAntenna(canvas);
    _drawEars(canvas);
    _drawBody(canvas);
    _drawScreen(canvas);
    _drawCheeks(canvas);
    _drawEyes(canvas);
    _drawMouth(canvas);
    _drawOverlay(canvas);

    canvas.restore();

    if (emotion == RobotEmotion.listening) {
      _drawListenRings(canvas);
    }
  }

  // ── pieces ─────────────────────────────────────────────────────────────

  Color get _bulbColor {
    switch (emotion) {
      case RobotEmotion.concerned:
        return const Color(0xFFF6A5C0);
      case RobotEmotion.celebrating:
      case RobotEmotion.proud:
        return const Color(0xFFFFD166);
      default:
        return _glow;
    }
  }

  void _drawAntenna(Canvas c) {
    final stem = Paint()
      ..color = color
      ..strokeWidth = 8
      ..strokeCap = StrokeCap.round
      ..style = PaintingStyle.stroke;
    c.drawLine(const Offset(180, 78), const Offset(180, 44), stem);
    final bulb = Paint()..color = _bulbColor;
    c.drawCircle(const Offset(180, 36), 13, bulb);
  }

  void _drawEars(Canvas c) {
    final p = Paint()..color = color;
    c.drawRRect(
      RRect.fromRectAndRadius(
        const Rect.fromLTWH(34, 150, 26, 64),
        const Radius.circular(13),
      ),
      p,
    );
    c.drawRRect(
      RRect.fromRectAndRadius(
        const Rect.fromLTWH(300, 150, 26, 64),
        const Radius.circular(13),
      ),
      p,
    );
    final bolt = Paint()..color = _lighten(color, 30);
    c.drawCircle(const Offset(47, 182), 7, bolt);
    c.drawCircle(const Offset(313, 182), 7, bolt);
  }

  void _drawBody(Canvas c) {
    final bodyRect = const Rect.fromLTWH(60, 84, 240, 232);
    final gradient = LinearGradient(
      begin: Alignment.topCenter,
      end: Alignment.bottomCenter,
      colors: [_lighten(color, 18), color],
    );
    final fill = Paint()..shader = gradient.createShader(bodyRect);
    c.drawRRect(
      RRect.fromRectAndRadius(bodyRect, const Radius.circular(52)),
      fill,
    );
    final stroke = Paint()
      ..color = _lighten(color, 26).withValues(alpha: 0.6)
      ..style = PaintingStyle.stroke
      ..strokeWidth = 3;
    c.drawRRect(
      RRect.fromRectAndRadius(bodyRect, const Radius.circular(52)),
      stroke,
    );
  }

  void _drawScreen(Canvas c) {
    final r = const Rect.fromLTWH(92, 112, 176, 148);
    c.drawRRect(
      RRect.fromRectAndRadius(r, const Radius.circular(38)),
      Paint()..color = _screen,
    );
    c.drawRRect(
      RRect.fromRectAndRadius(r, const Radius.circular(38)),
      Paint()
        ..color = const Color(0xFF0C1626)
        ..style = PaintingStyle.stroke
        ..strokeWidth = 4,
    );
  }

  void _drawCheeks(Canvas c) {
    if (![
      RobotEmotion.happy,
      RobotEmotion.proud,
      RobotEmotion.encouraging,
      RobotEmotion.celebrating,
    ].contains(emotion)) {
      return;
    }
    final p = Paint()..color = const Color(0x8CFF8FB1); // 55% alpha
    c.drawCircle(const Offset(104, 190), 13, p);
    c.drawCircle(const Offset(246, 190), 13, p);
  }

  // ── eyes ───────────────────────────────────────────────────────────────

  void _drawEyes(Canvas c) {
    final glow = Paint()..color = _glow;
    final glowStroke = Paint()
      ..color = _glow
      ..style = PaintingStyle.stroke
      ..strokeWidth = 9
      ..strokeCap = StrokeCap.round;

    switch (emotion) {
      case RobotEmotion.happy:
      case RobotEmotion.encouraging:
        _smileArc(c, glowStroke, const Offset(136, 150));
        _smileArc(c, glowStroke, const Offset(214, 150));
        break;
      case RobotEmotion.proud:
        _smileArc(c, glowStroke, const Offset(136, 152));
        _smileArc(c, glowStroke, const Offset(214, 152));
        break;
      case RobotEmotion.celebrating:
        _star(c, const Offset(136, 150), 20, glow);
        _star(c, const Offset(214, 150), 20, glow);
        break;
      case RobotEmotion.concerned:
        c.drawCircle(const Offset(136, 156), 13, glow);
        c.drawCircle(const Offset(214, 156), 13, glow);
        // brows
        final brow = Paint()
          ..color = _glow.withValues(alpha: 0.85)
          ..style = PaintingStyle.stroke
          ..strokeWidth = 7
          ..strokeCap = StrokeCap.round;
        c.drawLine(const Offset(120, 134), const Offset(148, 142), brow);
        c.drawLine(const Offset(230, 134), const Offset(202, 142), brow);
        break;
      case RobotEmotion.sleepy:
        c.drawLine(const Offset(118, 152), const Offset(154, 152), glowStroke);
        c.drawLine(const Offset(196, 152), const Offset(232, 152), glowStroke);
        break;
      case RobotEmotion.listening:
        final r = 17 + pulse * 4;
        final alpha = 1 - pulse * 0.4;
        final eyeP = Paint()..color = _glow.withValues(alpha: alpha);
        c.drawCircle(const Offset(136, 150), r, eyeP);
        c.drawCircle(const Offset(214, 150), r, eyeP);
        break;
      case RobotEmotion.speaking:
      case RobotEmotion.neutral:
        _blinkEye(c, const Rect.fromLTWH(123, 134, 26, 34));
        _blinkEye(c, const Rect.fromLTWH(201, 134, 26, 34));
        break;
    }
  }

  void _blinkEye(Canvas c, Rect rect) {
    final h = rect.height * blink;
    final newRect = Rect.fromCenter(
      center: rect.center,
      width: rect.width,
      height: h,
    );
    c.drawRRect(
      RRect.fromRectAndRadius(newRect, const Radius.circular(13)),
      Paint()..color = _glow,
    );
  }

  void _smileArc(Canvas c, Paint p, Offset center) {
    // upward arc ^
    final path = Path()
      ..moveTo(center.dx - 18, center.dy)
      ..quadraticBezierTo(center.dx, center.dy - 26, center.dx + 18, center.dy);
    c.drawPath(path, p);
  }

  void _star(Canvas c, Offset center, double r, Paint paint) {
    final path = Path();
    for (var i = 0; i < 10; i++) {
      final ang = math.pi / 5 * i - math.pi / 2;
      final rad = i.isEven ? r : r * 0.45;
      final x = center.dx + math.cos(ang) * rad;
      final y = center.dy + math.sin(ang) * rad;
      if (i == 0) {
        path.moveTo(x, y);
      } else {
        path.lineTo(x, y);
      }
    }
    path.close();
    c.drawPath(path, paint);
  }

  // ── mouth ──────────────────────────────────────────────────────────────

  void _drawMouth(Canvas c) {
    final stroke = Paint()
      ..color = _glow
      ..style = PaintingStyle.stroke
      ..strokeWidth = 9
      ..strokeCap = StrokeCap.round;
    final fill = Paint()..color = _glow;

    switch (emotion) {
      case RobotEmotion.happy:
      case RobotEmotion.proud:
      case RobotEmotion.encouraging:
        final path = Path()
          ..moveTo(138, 198)
          ..quadraticBezierTo(175, 232, 212, 198);
        c.drawPath(path, stroke);
        c.drawPath(path, Paint()..color = _glow.withValues(alpha: 0.18));
        break;
      case RobotEmotion.celebrating:
        final path = Path()
          ..moveTo(134, 196)
          ..quadraticBezierTo(175, 248, 216, 196)
          ..quadraticBezierTo(175, 210, 134, 196)
          ..close();
        c.drawPath(path, fill);
        break;
      case RobotEmotion.speaking:
        final scale = 0.6 + talk * 0.7; // 0.6..1.3
        c.drawOval(
          Rect.fromCenter(
            center: const Offset(175, 202),
            width: 40,
            height: 14 * 2 * scale,
          ),
          fill,
        );
        break;
      case RobotEmotion.listening:
        c.drawCircle(const Offset(175, 202), 11, fill);
        break;
      case RobotEmotion.concerned:
        // sad: arc curving down
        final path = Path()
          ..moveTo(140, 212)
          ..quadraticBezierTo(175, 182, 210, 212);
        c.drawPath(path, stroke);
        break;
      case RobotEmotion.sleepy:
        final path = Path()
          ..moveTo(158, 204)
          ..quadraticBezierTo(175, 218, 192, 204);
        c.drawPath(path, stroke);
        break;
      case RobotEmotion.neutral:
        c.drawLine(const Offset(148, 204), const Offset(202, 204), stroke);
        break;
    }
  }

  // ── overlays ───────────────────────────────────────────────────────────

  void _drawOverlay(Canvas c) {
    if (emotion == RobotEmotion.sleepy) {
      final tp = TextPainter(
        text: const TextSpan(
          text: 'Z',
          style: TextStyle(
            color: _glow,
            fontWeight: FontWeight.w700,
            fontSize: 26,
          ),
        ),
        textDirection: TextDirection.ltr,
      )..layout();
      tp.paint(c, const Offset(270, 70));
      tp.paint(c, const Offset(290, 50));
    }
    if (emotion == RobotEmotion.proud ||
        emotion == RobotEmotion.celebrating) {
      final p = Paint()..color = _bulbColor.withValues(alpha: 0.85);
      _star(c, const Offset(64, 120), 10, p);
      _star(c, const Offset(300, 140), 8, p);
      _star(c, const Offset(286, 70), 7, p);
    }
  }

  void _drawListenRings(Canvas c) {
    // Two expanding rings around the body for the "listening" state.
    final base = Offset(180, 200);
    for (var i = 0; i < 2; i++) {
      final t = (pulse + i * 0.5) % 1.0;
      final r = 60 + t * 100;
      final alpha = (1 - t) * 0.55;
      final p = Paint()
        ..color = _glow.withValues(alpha: alpha)
        ..style = PaintingStyle.stroke
        ..strokeWidth = 4;
      c.drawCircle(base, r, p);
    }
  }

  @override
  bool shouldRepaint(_RobotPainter o) =>
      o.emotion != emotion ||
      o.bob != bob ||
      o.blink != blink ||
      o.talk != talk ||
      o.pulse != pulse ||
      o.color != color;
}

/// Lighten a color by [amount] (-255..255) on each RGB channel.
Color _lighten(Color c, int amount) {
  int clamp(int v) => v < 0 ? 0 : (v > 255 ? 255 : v);
  return Color.fromARGB(
    (c.a * 255.0).round() & 0xff,
    clamp(((c.r * 255.0).round() & 0xff) + amount),
    clamp(((c.g * 255.0).round() & 0xff) + amount),
    clamp(((c.b * 255.0).round() & 0xff) + amount),
  );
}
