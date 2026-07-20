// LoginScreen — Hebrew RTL, kid-friendly. Segmented login/signup tabs.

import 'package:flutter/material.dart';
import 'package:flutter_svg/flutter_svg.dart';
import 'package:provider/provider.dart';

import '../constants.dart';
import '../providers/auth_provider.dart';
import '../theme.dart';
import '../widgets/p_card.dart';
import '../widgets/robot_face.dart';

class LoginScreen extends StatefulWidget {
  const LoginScreen({super.key});

  @override
  State<LoginScreen> createState() => _LoginScreenState();
}

class _LoginScreenState extends State<LoginScreen> {
  bool _signup = false;
  final _email = TextEditingController(text: '');
  final _pass = TextEditingController(text: '');
  final _name = TextEditingController(text: '');

  @override
  void dispose() {
    _email.dispose();
    _pass.dispose();
    _name.dispose();
    super.dispose();
  }

  bool get _emailOk =>
      RegExp(r'^\S+@\S+\.\S+$').hasMatch(_email.text.trim());

  bool get _valid =>
      _emailOk &&
      _pass.text.length >= 4 &&
      (!_signup || _name.text.trim().isNotEmpty);

  void _submit() {
    final auth = context.read<AuthProvider>();
    if (_signup) {
      auth.signUp(
        name: _name.text.trim(),
        email: _email.text.trim(),
        password: _pass.text,
      );
    } else {
      auth.signIn(email: _email.text.trim(), password: _pass.text);
    }
  }

  Future<void> _showResetDialog(BuildContext context) async {
    final ctrl = TextEditingController(text: _email.text.trim());
    final auth = context.read<AuthProvider>();
    final messenger = ScaffoldMessenger.of(context);
    final result = await showDialog<String?>(
      context: context,
      builder: (dialogCtx) {
        String? localErr;
        bool sending = false;
        return StatefulBuilder(
          builder: (ctx, setS) => AlertDialog(
            title: const Text('איפוס סיסמה'),
            content: Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                const Text(
                  'נשלח לך מייל עם קישור לאיפוס.',
                  textAlign: TextAlign.right,
                ),
                const SizedBox(height: 12),
                TextField(
                  controller: ctrl,
                  keyboardType: TextInputType.emailAddress,
                  textDirection: TextDirection.ltr,
                  decoration: const InputDecoration(hintText: 'name@mail.com'),
                ),
                if (localErr != null) ...[
                  const SizedBox(height: 10),
                  Text(
                    localErr!,
                    style: const TextStyle(
                      color: Color(0xFFC2425A),
                      fontWeight: FontWeight.w700,
                      fontSize: 13.5,
                    ),
                  ),
                ],
              ],
            ),
            actions: [
              TextButton(
                onPressed: sending ? null : () => Navigator.of(ctx).pop(null),
                child: const Text('ביטול'),
              ),
              ElevatedButton(
                onPressed: sending
                    ? null
                    : () async {
                        final email = ctrl.text.trim();
                        if (!RegExp(r'^\S+@\S+\.\S+$').hasMatch(email)) {
                          setS(() => localErr = 'כתובת אימייל לא תקינה');
                          return;
                        }
                        setS(() {
                          sending = true;
                          localErr = null;
                        });
                        final err = await auth.resetPassword(email);
                        if (!ctx.mounted) return;
                        if (err == null) {
                          Navigator.of(ctx).pop(email);
                        } else {
                          setS(() {
                            sending = false;
                            localErr = err;
                          });
                        }
                      },
                child: Text(sending ? 'שולח…' : 'שליחה'),
              ),
            ],
          ),
        );
      },
    );
    ctrl.dispose();
    if (result != null) {
      messenger.showSnackBar(
        SnackBar(content: Text('נשלח מייל איפוס אל $result')),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    final auth = context.watch<AuthProvider>();
    final busy = auth.status == AuthStatus.authenticating;
    final err = auth.errorMessage;

    return Scaffold(
      body: SafeArea(
        child: LayoutBuilder(
          builder: (context, c) {
            return SingleChildScrollView(
              padding: const EdgeInsets.fromLTRB(20, 8, 20, 22),
              child: ConstrainedBox(
                constraints: BoxConstraints(minHeight: c.maxHeight - 30),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    const SizedBox(height: 6),
                    const Center(
                      child: RobotFace(
                        emotion: RobotEmotion.happy,
                        size: 132,
                      ),
                    ),
                    const SizedBox(height: 6),
                    Text(
                      'Pip - Personal Tutor for Kids',
                      textAlign: TextAlign.center,
                      style: AppTextStyles.display(context).copyWith(fontSize: 22),
                    ),
                    const SizedBox(height: 2),
                    Text(
                      'אזור ההורים · מעקב והגדרות',
                      textAlign: TextAlign.center,
                      style: AppTextStyles.hint(context),
                    ),
                    const SizedBox(height: 22),
                    _Segmented(
                      signup: _signup,
                      onChange: (v) => setState(() {
                        _signup = v;
                      }),
                    ),
                    const SizedBox(height: 18),
                    PCard(
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.stretch,
                        children: [
                          if (_signup) ...[
                            _Field(
                              label: 'שם ההורה',
                              controller: _name,
                              placeholder: 'לדוגמה: אורלי',
                              onChange: () => setState(() {}),
                            ),
                            const SizedBox(height: 14),
                          ],
                          _Field(
                            label: 'אימייל',
                            controller: _email,
                            placeholder: 'name@mail.com',
                            keyboardType: TextInputType.emailAddress,
                            ltr: true,
                            onChange: () => setState(() {}),
                          ),
                          const SizedBox(height: 14),
                          _Field(
                            label: 'סיסמה',
                            controller: _pass,
                            placeholder: '••••••',
                            obscure: true,
                            ltr: true,
                            onChange: () => setState(() {}),
                          ),
                          if (err != null) ...[
                            const SizedBox(height: 14),
                            _ErrorBox(message: err),
                          ],
                        ],
                      ),
                    ),
                    if (!_signup) ...[
                      const SizedBox(height: 12),
                      Align(
                        alignment: AlignmentDirectional.centerStart,
                        child: TextButton(
                          style: TextButton.styleFrom(
                            foregroundColor: AppColors.sky,
                            padding: EdgeInsets.zero,
                          ),
                          onPressed: () => _showResetDialog(context),
                          child: const Text(
                            'שכחתי סיסמה',
                            style: TextStyle(fontWeight: FontWeight.w700),
                          ),
                        ),
                      ),
                    ],
                    const SizedBox(height: 22),
                    ElevatedButton(
                      onPressed: (!_valid || busy) ? null : _submit,
                      child: Text(
                        busy
                            ? 'מתחבר…'
                            : (_signup ? 'יצירת חשבון' : 'כניסה'),
                      ),
                    ),
                    const SizedBox(height: 16),
                    const _OrDivider(),
                    const SizedBox(height: 16),
                    OutlinedButton.icon(
                      onPressed: busy
                          ? null
                          : () => context.read<AuthProvider>().signInWithGoogle(),
                      style: OutlinedButton.styleFrom(
                        // Standard "Sign in with Google" light button styling.
                        backgroundColor: Colors.white,
                        foregroundColor: const Color(0xFF3C4043),
                        minimumSize: const Size.fromHeight(54),
                        side: const BorderSide(color: Color(0xFFDADCE0)),
                        shape: RoundedRectangleBorder(
                          borderRadius: BorderRadius.circular(AppRadii.pill),
                        ),
                        textStyle: const TextStyle(
                          fontWeight: FontWeight.w700,
                          fontSize: 16,
                        ),
                      ),
                      icon: SvgPicture.asset(
                        'assets/google_g.svg',
                        width: 20,
                        height: 20,
                      ),
                      label: const Text('המשך עם Google'),
                    ),
                  ],
                ),
              ),
            );
          },
        ),
      ),
    );
  }
}

class _Segmented extends StatelessWidget {
  const _Segmented({required this.signup, required this.onChange});
  final bool signup;
  final ValueChanged<bool> onChange;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(4),
      decoration: BoxDecoration(
        color: const Color(0xFFEDF2FB),
        borderRadius: BorderRadius.circular(AppRadii.pill),
      ),
      child: Row(
        children: [
          Expanded(child: _seg('כניסה', !signup, () => onChange(false))),
          Expanded(child: _seg('הרשמה', signup, () => onChange(true))),
        ],
      ),
    );
  }

  Widget _seg(String label, bool active, VoidCallback onTap) {
    return InkWell(
      onTap: onTap,
      borderRadius: BorderRadius.circular(AppRadii.pill),
      child: AnimatedContainer(
        duration: const Duration(milliseconds: 150),
        padding: const EdgeInsets.symmetric(vertical: 11),
        decoration: BoxDecoration(
          color: active ? Colors.white : Colors.transparent,
          borderRadius: BorderRadius.circular(AppRadii.pill),
          boxShadow: active ? AppShadow.soft : null,
        ),
        alignment: Alignment.center,
        child: Text(
          label,
          style: TextStyle(
            fontWeight: FontWeight.w800,
            fontSize: 15.5,
            color: active ? AppColors.sky : AppColors.inkSoft,
          ),
        ),
      ),
    );
  }
}

class _Field extends StatelessWidget {
  const _Field({
    required this.label,
    required this.controller,
    required this.placeholder,
    this.obscure = false,
    this.ltr = false,
    this.keyboardType,
    this.onChange,
  });

  final String label;
  final TextEditingController controller;
  final String placeholder;
  final bool obscure;
  final bool ltr;
  final TextInputType? keyboardType;
  final VoidCallback? onChange;

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Text(label, style: AppTextStyles.label(context)),
        const SizedBox(height: 8),
        TextField(
          controller: controller,
          obscureText: obscure,
          keyboardType: keyboardType,
          textDirection: ltr ? TextDirection.ltr : null,
          textAlign: ltr ? TextAlign.right : TextAlign.right,
          decoration: InputDecoration(hintText: placeholder),
          onChanged: (_) => onChange?.call(),
        ),
      ],
    );
  }
}

class _OrDivider extends StatelessWidget {
  const _OrDivider();

  @override
  Widget build(BuildContext context) {
    final line = Expanded(
      child: Divider(color: AppColors.inkSoft.withValues(alpha: 0.3), thickness: 1),
    );
    return Row(
      children: [
        line,
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 12),
          child: Text('או', style: AppTextStyles.hint(context)),
        ),
        line,
      ],
    );
  }
}

class _ErrorBox extends StatelessWidget {
  const _ErrorBox({required this.message});
  final String message;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 10),
      decoration: BoxDecoration(
        color: AppColors.coralSoft,
        borderRadius: BorderRadius.circular(AppRadii.sm),
      ),
      child: Row(
        children: [
          const Icon(Icons.warning_amber_rounded, color: Color(0xFFC2425A), size: 18),
          const SizedBox(width: 8),
          Expanded(
            child: Text(
              message,
              style: const TextStyle(
                color: Color(0xFFC2425A),
                fontWeight: FontWeight.w700,
                fontSize: 13.5,
              ),
            ),
          ),
        ],
      ),
    );
  }
}
