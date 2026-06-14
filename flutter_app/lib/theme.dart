// Design system for the Emotional Tutor parent app.
// Translated from the Claude Design output (~/Downloads/ioT/theme.css).
// Kid-friendly palette: soft sky-blue primary with warm yellow/coral/mint accents.

import 'package:flutter/material.dart';
import 'package:google_fonts/google_fonts.dart';

class AppColors {
  static const Color sky        = Color(0xFF4F86F7);
  static const Color skySoft    = Color(0xFFDCE9FF);
  static const Color skyShadow  = Color(0x574F86F7); // rgba(79,134,247,0.34)
  static const Color sun        = Color(0xFFFFC93C);
  static const Color sunSoft    = Color(0xFFFFF0C9);
  static const Color coral      = Color(0xFFFF7E7E);
  static const Color coralSoft  = Color(0xFFFFE0E0);
  static const Color mint       = Color(0xFF36C9A0);
  static const Color mintSoft   = Color(0xFFD2F4EA);
  static const Color grape      = Color(0xFF9B7BE6);
  static const Color ink        = Color(0xFF2A3550);
  static const Color inkSoft    = Color(0xFF6B7794);
  static const Color paper      = Color(0xFFF4F8FF);
  static const Color card       = Color(0xFFFFFFFF);
  static const Color divider    = Color(0xFFEEF2FA);
  static const Color warn       = Color(0xFFFFF3E0);
  static const Color warnInk    = Color(0xFF9A5B00);
}

class AppRadii {
  static const double lg = 36;
  static const double md = 24;
  static const double sm = 16;
  static const double pill = 999;
}

class AppShadow {
  static const List<BoxShadow> soft = [
    BoxShadow(
      color: Color(0x1F2A3550), // rgba(42,53,80,0.12)
      offset: Offset(0, 10),
      blurRadius: 26,
    ),
  ];
  static const List<BoxShadow> large = [
    BoxShadow(
      color: Color(0x2E2A3550), // rgba(42,53,80,0.18)
      offset: Offset(0, 18),
      blurRadius: 44,
    ),
  ];
  static const List<BoxShadow> button = [
    BoxShadow(
      color: AppColors.skyShadow,
      offset: Offset(0, 8),
      blurRadius: 20,
    ),
  ];
}

ThemeData buildAppTheme() {
  // Hebrew-friendly fonts. Heebo for body (designed for Hebrew),
  // Heebo + bold weights for display. Google Fonts pulls them at first run.
  final base = ThemeData.light(useMaterial3: true);
  final textTheme = GoogleFonts.heeboTextTheme(base.textTheme).apply(
    bodyColor: AppColors.ink,
    displayColor: AppColors.ink,
  );

  return base.copyWith(
    scaffoldBackgroundColor: AppColors.paper,
    colorScheme: ColorScheme.fromSeed(
      seedColor: AppColors.sky,
      primary: AppColors.sky,
      onPrimary: Colors.white,
      surface: AppColors.card,
      onSurface: AppColors.ink,
    ),
    textTheme: textTheme,
    appBarTheme: AppBarTheme(
      backgroundColor: AppColors.paper,
      surfaceTintColor: Colors.transparent,
      foregroundColor: AppColors.ink,
      elevation: 0,
      centerTitle: false,
      titleTextStyle: textTheme.titleLarge?.copyWith(
        fontWeight: FontWeight.w800,
        fontSize: 24,
        color: AppColors.ink,
      ),
    ),
    cardTheme: CardThemeData(
      color: AppColors.card,
      elevation: 0,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(AppRadii.md),
      ),
      margin: EdgeInsets.zero,
    ),
    inputDecorationTheme: InputDecorationTheme(
      filled: true,
      fillColor: Colors.white,
      contentPadding: const EdgeInsets.symmetric(horizontal: 16, vertical: 14),
      hintStyle: textTheme.bodyMedium?.copyWith(color: const Color(0xFFA9B4CC)),
      border: OutlineInputBorder(
        borderRadius: BorderRadius.circular(AppRadii.sm),
        borderSide: const BorderSide(color: AppColors.skySoft, width: 2),
      ),
      enabledBorder: OutlineInputBorder(
        borderRadius: BorderRadius.circular(AppRadii.sm),
        borderSide: const BorderSide(color: AppColors.skySoft, width: 2),
      ),
      focusedBorder: OutlineInputBorder(
        borderRadius: BorderRadius.circular(AppRadii.sm),
        borderSide: const BorderSide(color: AppColors.sky, width: 2),
      ),
      errorBorder: OutlineInputBorder(
        borderRadius: BorderRadius.circular(AppRadii.sm),
        borderSide: const BorderSide(color: AppColors.coral, width: 2),
      ),
      labelStyle: textTheme.labelLarge?.copyWith(
        fontWeight: FontWeight.w700,
        color: AppColors.ink,
      ),
    ),
    elevatedButtonTheme: ElevatedButtonThemeData(
      style: ElevatedButton.styleFrom(
        backgroundColor: AppColors.sky,
        foregroundColor: Colors.white,
        elevation: 0,
        minimumSize: const Size.fromHeight(54),
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(AppRadii.pill),
        ),
        textStyle: textTheme.titleMedium?.copyWith(
          fontWeight: FontWeight.w800,
          fontSize: 18,
        ),
        disabledBackgroundColor: const Color(0xFFC9D4EA),
        disabledForegroundColor: Colors.white,
      ),
    ),
    outlinedButtonTheme: OutlinedButtonThemeData(
      style: OutlinedButton.styleFrom(
        foregroundColor: AppColors.sky,
        side: const BorderSide(color: AppColors.skySoft, width: 2),
        minimumSize: const Size.fromHeight(54),
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(AppRadii.pill),
        ),
        textStyle: textTheme.titleMedium?.copyWith(
          fontWeight: FontWeight.w800,
          fontSize: 18,
        ),
      ),
    ),
    dividerColor: AppColors.divider,
    snackBarTheme: SnackBarThemeData(
      backgroundColor: AppColors.ink,
      contentTextStyle: textTheme.bodyMedium?.copyWith(color: Colors.white),
      behavior: SnackBarBehavior.floating,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(AppRadii.sm),
      ),
    ),
  );
}

/// Convenience: standard text styles used by widgets. Kept here so the
/// Claude Design swap can adjust them in one place.
class AppTextStyles {
  static TextStyle display(BuildContext c) =>
      Theme.of(c).textTheme.headlineSmall!.copyWith(
            fontWeight: FontWeight.w800,
            color: AppColors.ink,
            height: 1.15,
          );

  static TextStyle title(BuildContext c) =>
      Theme.of(c).textTheme.titleMedium!.copyWith(
            fontWeight: FontWeight.w800,
            color: AppColors.ink,
            fontSize: 17,
          );

  static TextStyle label(BuildContext c) =>
      Theme.of(c).textTheme.titleSmall!.copyWith(
            fontWeight: FontWeight.w700,
            fontSize: 15,
            color: AppColors.ink,
          );

  static TextStyle hint(BuildContext c) =>
      Theme.of(c).textTheme.bodyMedium!.copyWith(
            color: AppColors.inkSoft,
            fontSize: 13.5,
            height: 1.5,
          );
}
