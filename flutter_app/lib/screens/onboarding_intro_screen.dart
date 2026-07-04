// OnboardingIntroScreen — a first-use slide deck that welcomes the parent and
// walks through how to use the app (add materials, watch reports, connect the
// device, adjust settings). Shown once after login (gated by
// ConfigProvider.hasSeenIntro in AuthGate) and re-openable from Settings.
// Reuses the RobotFace mascot, the real bottom-nav icons/labels, and the theme.

import 'package:flutter/material.dart';

import '../constants.dart';
import '../theme.dart';
import '../widgets/robot_face.dart';

class _IntroSlide {
  const _IntroSlide({
    required this.title,
    required this.body,
    this.note,
    this.emotion,
    this.icon,
    this.tint,
  });

  final String title;
  final String body;

  /// Optional smaller caption shown under the body (e.g. the skip hint on the
  /// first slide).
  final String? note;

  /// Welcome / done slides show the mascot with this emotion.
  final RobotEmotion? emotion;

  /// Feature slides show this bottom-nav icon (mirrors what the user will tap)
  /// in a [tint]-colored badge instead of the mascot.
  final IconData? icon;
  final Color? tint;
}

// Icons/labels below mirror lib/widgets/bottom_nav.dart so the walkthrough
// matches exactly what the parent sees in the bottom navigation bar.
const List<_IntroSlide> _slides = [
  _IntroSlide(
    emotion: RobotEmotion.happy,
    title: 'ברוכים הבאים ל‑Pip 👋',
    body: 'Pip הוא עוזר לימוד חכם לילד — והאפליקציה הזו נותנת לכם לנהל אותו, '
        'לעקוב ולשלוט. הנה סיור קצר.',
    note: 'אפשר לדלג על הסיור בכל רגע — תמיד תוכלו לצפות בו שוב '
        'דרך «הגדרות».',
  ),
  _IntroSlide(
    icon: Icons.menu_book_rounded,
    tint: AppColors.sun,
    title: 'הוספת חומרי לימוד',
    body: 'בלשונית «חומר לימוד» העלו קובץ (PDF, תמונה או טקסט) או הקלידו שאלות '
        'ותשובות משלכם — ו‑Pip ילמד מהם וישאל את הילד.',
  ),
  _IntroSlide(
    icon: Icons.description_outlined,
    tint: AppColors.mint,
    title: 'מעקב ודוחות',
    body: 'בלשונית «דוחות» תראו את היסטוריית המפגשים, ובעמוד «מגמות» את '
        'ההתקדמות של הילד לאורך זמן.',
  ),
  _IntroSlide(
    icon: Icons.devices_other_rounded,
    tint: AppColors.sky,
    title: 'חיבור ההתקן',
    body: 'הדליקו את ההתקן וודאו שהוא מחובר ל‑Wi‑Fi — על מסכו יופיע קוד בן 6 '
        'ספרות. עברו ללשונית «התקן», הקלידו את הקוד, וההתקן יתחבר לילד. שם גם '
        'תראו אם הוא מחובר ותעקבו אחרי מפגש בזמן אמת.',
    note: 'אין התקן עדיין? אפשר להמשיך בלעדיו ולחבר אותו מאוחר יותר '
        'מלשונית «התקן».',
  ),
  _IntroSlide(
    icon: Icons.settings_rounded,
    tint: AppColors.grape,
    title: 'שליטה והגדרות',
    body: 'בלשונית «הגדרות» קבעו אורך מפגש והפסקות, הפעילו התראות וערכו את '
        'פרופיל הילד. בעמוד «בית» תמיד יש סיכום מהיר.',
  ),
  _IntroSlide(
    emotion: RobotEmotion.celebrating,
    title: 'מוכנים להתחיל! 🎉',
    body: 'נתחיל בהגדרה קצרה — חיבור ההתקן ויצירת פרופיל לילד.',
  ),
];

class OnboardingIntroScreen extends StatefulWidget {
  const OnboardingIntroScreen({super.key, required this.onDone});

  /// Called when the user finishes or skips the intro. When used as the
  /// auth-gate step this sets the "seen" flag; when pushed from Settings it
  /// simply pops the route.
  final VoidCallback onDone;

  @override
  State<OnboardingIntroScreen> createState() => _OnboardingIntroScreenState();
}

class _OnboardingIntroScreenState extends State<OnboardingIntroScreen> {
  final _controller = PageController();
  int _page = 0;

  bool get _isLast => _page == _slides.length - 1;

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }

  void _next() {
    if (_isLast) {
      widget.onDone();
      return;
    }
    _controller.nextPage(
      duration: const Duration(milliseconds: 260),
      curve: Curves.easeOut,
    );
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: SafeArea(
        child: Column(
          children: [
            // Skip lives on the leading (right, in RTL) edge across all slides.
            Align(
              alignment: AlignmentDirectional.centerStart,
              child: Padding(
                padding: const EdgeInsets.fromLTRB(8, 6, 8, 0),
                child: TextButton(
                  style: TextButton.styleFrom(
                    foregroundColor: AppColors.sky,
                    backgroundColor: Colors.white,
                    padding:
                        const EdgeInsets.symmetric(horizontal: 18, vertical: 10),
                    textStyle: const TextStyle(
                      fontSize: 17,
                      fontWeight: FontWeight.w800,
                    ),
                    shape: RoundedRectangleBorder(
                      borderRadius: BorderRadius.circular(AppRadii.pill),
                      side: const BorderSide(color: Colors.white, width: 2),
                    ),
                  ),
                  onPressed: widget.onDone,
                  child: const Text('דלג'),
                ),
              ),
            ),
            Expanded(
              child: PageView.builder(
                controller: _controller,
                itemCount: _slides.length,
                onPageChanged: (i) => setState(() => _page = i),
                itemBuilder: (_, i) => _SlideView(slide: _slides[i]),
              ),
            ),
            _dots(),
            const SizedBox(height: 18),
            Padding(
              padding: const EdgeInsets.fromLTRB(20, 0, 20, 22),
              child: SizedBox(
                width: double.infinity,
                child: ElevatedButton(
                  onPressed: _next,
                  child: Text(_isLast ? 'בואו נתחיל' : 'הבא'),
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget _dots() {
    return Row(
      mainAxisAlignment: MainAxisAlignment.center,
      children: [
        for (var i = 0; i < _slides.length; i++)
          AnimatedContainer(
            duration: const Duration(milliseconds: 220),
            margin: const EdgeInsets.symmetric(horizontal: 4),
            width: i == _page ? 22 : 8,
            height: 8,
            decoration: BoxDecoration(
              color: i == _page ? AppColors.sky : AppColors.skySoft,
              borderRadius: BorderRadius.circular(AppRadii.pill),
            ),
          ),
      ],
    );
  }
}

class _SlideView extends StatelessWidget {
  const _SlideView({required this.slide});
  final _IntroSlide slide;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.fromLTRB(24, 12, 24, 0),
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          const Spacer(),
          _visual(),
          const SizedBox(height: 22),
          Text(
            slide.title,
            textAlign: TextAlign.center,
            style: AppTextStyles.display(context).copyWith(fontSize: 26),
          ),
          const SizedBox(height: 10),
          SizedBox(
            width: 320,
            child: Text(
              slide.body,
              textAlign: TextAlign.center,
              style: AppTextStyles.hint(context).copyWith(fontSize: 16),
            ),
          ),
          if (slide.note != null) ...[
            const SizedBox(height: 16),
            Container(
              padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
              decoration: BoxDecoration(
                color: AppColors.skySoft.withValues(alpha: 0.5),
                borderRadius: BorderRadius.circular(AppRadii.sm),
              ),
              child: Row(
                mainAxisSize: MainAxisSize.min,
                children: [
                  const Icon(Icons.info_outline, size: 18, color: AppColors.sky),
                  const SizedBox(width: 8),
                  Flexible(
                    child: Text(
                      slide.note!,
                      textAlign: TextAlign.center,
                      style: AppTextStyles.hint(context).copyWith(fontSize: 13.5),
                    ),
                  ),
                ],
              ),
            ),
          ],
          const Spacer(),
        ],
      ),
    );
  }

  Widget _visual() {
    // Feature slides show the real bottom-nav icon in a tinted badge; the
    // welcome/done slides show the animated mascot.
    if (slide.icon != null) {
      final tint = slide.tint ?? AppColors.sky;
      return Container(
        width: 132,
        height: 132,
        decoration: BoxDecoration(
          color: tint.withValues(alpha: 0.14),
          borderRadius: BorderRadius.circular(AppRadii.lg),
        ),
        alignment: Alignment.center,
        child: Icon(slide.icon, size: 62, color: tint),
      );
    }
    return RobotFace(emotion: slide.emotion ?? RobotEmotion.happy, size: 180);
  }
}
