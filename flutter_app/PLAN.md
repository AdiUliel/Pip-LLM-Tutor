# PLAN.md — Emotional Tutor (Parent App)

> Crash-safe progress tracker. Updated after **every** build step. Future sessions can `/sync` from this file to resume.

**Last updated:** 2026-06-07 — All 9 steps complete · ready for user verification & UI polish
**Current step:** ✅ Done — Step 8 pending one user action (`flutterfire configure`)
**Mode:** Auto / Opus 4.7

---

## What we're building

Flutter + Firebase **parent companion** app for an ESP32-based emotional tutor for kids (7–11). The app does NO tutoring — that runs on the device. The app does: child config, material upload, session reports, mood/accuracy trends, device monitor, settings, admin view.

Mock mode is **ON by default** so the entire app demos with no Firebase and no hardware. Real Firebase is wired from day one behind a toggle.

User confirmed: project name `emotional_tutor`, minimal mock data (1 child + 3–5 sessions), topic focus = dropdown + "Other → free text", Hebrew RTL. Visual design supplied via Claude Design at `~/Downloads/ioT/` — translated into `theme.dart` + future widgets.

Full plan: `/Users/lotandahan/.claude/plans/build-prompt-piped-cocke.md`

---

## Build checklist

- [x] **Step 1 — Scaffold + Firebase wiring + PLAN.md**
      `flutter create` + pubspec, `theme.dart` with the kid-friendly Claude Design tokens, `constants.dart` with all tunables + Hebrew labels + mood scale, RTL locale, auth-gate routing skeleton, stub `firebase_options.dart`. Mock-mode default ON. `dart analyze` clean, smoke test passes.
- [x] **Step 2 — Models + service interfaces + mocks**
      6 models (ParentUser, Child + ChildSettings, Session, QuestionLog, MaterialDoc + QAPair, DeviceState). FirebaseService + DeviceSyncService abstract interfaces with real Firestore impls and seeded mocks. 3 new providers (Child, Device, Stats). Main.dart now wires services per Mock-mode flag. Analyzer clean, smoke test passes.
- [x] **Step 3 — Auth + First-Time Setup wizard**
      AuthProvider with real Firebase Auth + mock paths (signIn/signUp/signOut, Hebrew error translations). Full Hebrew RTL Login/Signup screen with segmented tabs. 6-step Setup Wizard (welcome → connect → profile → level → subjects → done) writing `children/{childId}` on completion. AuthGate now routes login → wizard (no child) → dashboard. **RobotFace** widget (CustomPainter, 9 emotions, bob/blink/talk/pulse/ring animations) + 6 primitives (DevChip, ScreenHeader, WizardProgress, PCard, POpt, PStepper). Analyzer clean, smoke test passes.
- [x] **Step 4 — Dashboard shell**
      `ShellScreen` with `IndexedStack` + 5-tab BottomNav (Home / Reports / Trends / Device / Settings). Home tab: greeting header, device hero card with RobotFace + DevChip, today-ring, last-session card with accuracy + duration + streak, 2×2 quick-actions grid (config, materials, reports, trends). Stubs for 4 other tabs + child-config + material-upload. AuthGate now routes child-loaded users into ShellScreen.
- [x] **Step 5 — Child Config + Material Upload**
      `ChildConfigScreen`: age stepper, per-subject toggles, topic chips (3-4 predefined + "אחר" free-text), 3-bucket level segments (מתחילים/בינוני/מתקדמים). `MaterialUploadScreen` with 3 sub-tabs: list (existing materials + dashed-border upload tile), manual (Q/A pair editor + add/remove rows), file (file_picker with 5 MB cap + MIME check + Storage upload via FirebaseService).
- [x] **Step 6 — Reports + Trends charts**
      `ReportsScreen` with filter chips (הכל/חשבון/אנגלית) + session cards (subject icon, day/time/duration, stars + mood pills, big accuracy with mint/sun/coral coloring). `SessionDetailScreen` with 3-stat card (accuracy/stars/streak), mood card with RobotFace, per-question StreamBuilder<QuestionLog> list. `TrendsScreen` with 4 cards: smooth fl_chart line for accuracy (mint), mood (grape) with נהנה↔מתוסכל floor labels, fl_chart BarChart for time by subject, custom progress bars for subject distribution.
- [x] **Step 7 — Device Monitor + Settings + Admin**
      `DeviceMonitorScreen` with live RobotFace + DevChip, 2-up status grid (last heartbeat / active subject), current-question card, remote Start/Stop buttons (Start green, Stop ghost) with offline guard + SnackBar toast. `SettingsScreen` with 3 themed Sliders (session/break/daily limit) + Provider-backed Save button + rows for child config / admin / Mock toggle / logout / version footer. `AdminViewScreen` with raw `deviceState/{deviceId}` KV listing (LTR monospace), 5-row LTR log stream, Mock toggle + reset-to-defaults.
- [~] **Step 8 — Flip Mock → Real and verify**
      Code-side complete: main.dart wraps `Firebase.initializeApp` in a guarded try/catch that auto-reverts to Mock + shows a Hebrew SnackBar if `firebase_options.dart` is still the stub. A `GlobalKey<NavigatorState>` plumbs SnackBars before the auth gate mounts. **User action required:** run `flutterfire configure` against your project, then flip the Mock toggle in Admin to verify end-to-end live writes to Firestore + Storage.
- [x] **Step 9 — Edge cases sweep + polish**
      `utils/validators.dart` (email, password, notEmpty, childAge, fileSize, fileExtension, subjectsAtLeastOne → all Hebrew messages). `utils/offline_queue.dart` (shared_preferences-backed FIFO with `enqueueSaveChild` + `flush(FirebaseService)`; max 100 ops, drops oldest). `widgets/offline_banner.dart` (warm-orange banner above Shell, tap to retry, hidden when queue empty). `ChildProvider.save` wraps FirebaseException → queue. `main.dart` instantiates queue + auto-flushes on startup.

---

## Open blockers / decisions in flight

- **`flutterfire configure` is the only remaining user action.** Stub `firebase_options.dart` is in place; Mock mode is default ON. To flip to real Firebase: run `flutterfire configure`, then toggle Mock-off in Admin/Settings → restart. If user flips early (stub still in place), main.dart auto-reverts to Mock + shows a Hebrew SnackBar.
- `flutter analyze` crashes due to a Dart LSP JSON bug on the Hebrew folder path (`אינטרנט של הדברים/פרויקט/`). **Workaround: use `dart analyze` instead.** Tracks fine, just a different CLI.
- Robot mascot is a single `CustomPainter` translating the React SVG (9 emotions, 4 controllers for bob/blink/talk/pulse).
- Offline file-upload queue intentionally not implemented (Storage `putData` needs connectivity; only typed-Q&A material writes would queue cleanly, and they go through the same path as `saveChild`).

---

## Architecture quick-ref

```
lib/
  main.dart, app.dart, theme.dart, constants.dart, firebase_options.dart (stub)
  providers/   auth_provider.dart, config_provider.dart
  screens/     login_screen.dart, dashboard_screen.dart  (placeholders so far)
  (models/ services/ widgets/ utils/ added in Step 2+)
```

**Rule:** screens never touch Firestore — only providers, and providers only call services. Every external IO sits behind a service interface with **real + mock** implementations.

**Design system** (translated from `~/Downloads/ioT/theme.css`):
- palette: sky `#4F86F7` (primary) · sky-soft, sun `#FFC93C`, coral `#FF7E7E`, mint `#36C9A0`, grape `#9B7BE6`, ink `#2A3550`, paper `#F4F8FF`
- radii: lg 36 / md 24 / sm 16 / pill 999
- font: Heebo (Google Fonts) — Hebrew-friendly across all weights
- RTL: `Directionality(textDirection: TextDirection.rtl)` wrapped at the MaterialApp `builder`

**Firestore collections (shared with the ESP32 device):**
- `parents/`, `children/`, `materials/` — app writes
- `deviceState/{deviceId}` — device writes (status, heartbeat); app may write `command`
- `sessions/{sessionId}/questions/` — device writes; app reads for reports

---

## Step-completion log

### Step 1 — 2026-06-07

- `flutter create . --project-name emotional_tutor --org com.emotionaltutor --platforms=android,ios`
- `pubspec.yaml`: added provider, firebase_core/auth/firestore/storage/messaging, fl_chart, file_picker, http, intl, shared_preferences, google_fonts, flutter_localizations (SDK).
- `lib/constants.dart`: tunables (heartbeat 30s, mock tick 2s, offline queue cap 100, age 6–12, session defaults 15/7/30 min, upload 5 MB cap), `Subject`/`DeviceStatus`/`RobotEmotion` enums, `MoodScale` (1..5 with Hebrew labels + colors + robot emotion), `SubjectMeta` + `LevelOption` lists, Hebrew topic labels.
- `lib/theme.dart`: full Claude-Design color/radii/shadow tokens (`AppColors`, `AppRadii`, `AppShadow`), `buildAppTheme()` with Heebo text theme, pill-shaped elevated/outlined buttons (54 px min height), soft-blue input borders, `AppTextStyles` helpers.
- `lib/providers/auth_provider.dart`: `AuthStatus` enum + stub sign-in/sign-up/sign-out (real Firebase Auth in Step 3).
- `lib/providers/config_provider.dart`: shared_preferences-backed Mock toggle + session/break/daily-limit settings + `resetToDefaults()`.
- `lib/firebase_options.dart`: stub that throws — `flutterfire configure` replaces it later.
- `lib/main.dart`: WidgetsFlutterBinding.ensureInitialized → SharedPreferences → conditional `Firebase.initializeApp` (only if `!mockMode`) → MultiProvider → runApp.
- `lib/app.dart`: `MaterialApp` with `locale: he`, `supportedLocales: [he, en]`, three GlobalLocalizations delegates, `Directionality.rtl` builder, `AuthGate` Consumer routing to LoginScreen or DashboardScreen.
- `lib/screens/login_screen.dart` + `dashboard_screen.dart`: minimal placeholders so the auth gate works end-to-end.
- `test/widget_test.dart`: smoke test — pumps `EmotionalTutorApp` with mocked SharedPreferences, asserts the login screen renders.
- README rewritten with quick-start, FlutterFire setup, Firestore schema, app↔device write split.
- Verification: `dart analyze` → no issues. `flutter test` → 1 passed.

**Next session:** start Step 2 — models + service interfaces + mock implementations (no UI yet).

### Step 2 — 2026-06-07

- Models: `parent_user.dart`, `child.dart` (+ `ChildSettings.defaults()`, `.copyWith`), `session.dart` (computed `accuracyPct` + `durationMinutes`), `question_log.dart`, `material_doc.dart` (+ `QAPair`), `device_state.dart` (derived `online` from `lastHeartbeat` + `heartbeatTimeoutSec`).
- Models serialize via `fromMap(id, Map<String,dynamic>)` + `toMap()` using `DateTime`; Firestore service converts `Timestamp`↔`DateTime` in a `_hydrate()` helper.
- `services/firebase_service.dart` — abstract API (parents, children, materials, sessions, question logs) used by every provider.
- `services/firebase_service_mock.dart` — seeded with the design's sample data (נועה age 8, deviceId TUTOR-3F9A, math+english enabled, 5 sample sessions across the last 5 days with varied accuracy/mood, 6 per-question logs for the newest session, 1 multiplication material).
- `services/firebase_service_real.dart` — full Firestore + Storage impl (snapshots(), collection queries with `orderBy`/`limit`, `putData` + `getDownloadURL` for uploaded materials).
- `services/device_sync_service*.dart` — abstract + real (Firestore stream) + mock (`Timer.periodic` cycles idle→asking→listening→feedback→idle→break every 2 s, bumps heartbeat each tick so `online` stays true).
- Providers: `ChildProvider` (current child, subscribed via `watchChild`), `DeviceProvider` (live state + `isOnline`), `StatsProvider` (minutesToday, averageAccuracy, accuracySeries, moodSeries for charts).
- `main.dart` updated to choose Mock vs Real services at startup based on `config.mockMode`, register all 7 providers via `MultiProvider`.
- Import fix: `firebase_core` exports a `FirebaseService` symbol → hidden via `hide FirebaseService` on the import so our service class doesn't clash.
- Verification: `dart analyze` → no issues. `flutter test` → 1 passed.

**Next session:** Step 3 — login + sign-up screens, the 4-step wizard, and the `RobotFace` mascot (CustomPainter, 9 emotions, animated).

### Step 3 — 2026-06-07

- `widgets/robot_face.dart` — single `CustomPainter` translating the React SVG mascot to Flutter. 4 `AnimationController`s (bob, blink, talk, pulse) feed the painter. Per-emotion eye + mouth + cheek drawing logic for `neutral`/`speaking`/`listening`/`happy`/`proud`/`encouraging`/`concerned`/`celebrating`/`sleepy`. Listening state draws expanding sound rings; proud/celebrating get twinkling stars; sleepy gets floating Z's.
- 6 widget primitives translating the design tokens to Flutter:
  - `dev_chip.dart` — online/searching/offline pill with pulsing dot.
  - `screen_header.dart` — title + sub + back chevron (RTL aware) + right slot.
  - `wizard_progress.dart` — animated 4-segment progress bar.
  - `p_card.dart` — soft white card primitive (`AppShadow.soft`).
  - `p_opt.dart` — selectable option card with radio OR toggle indicator; tap-to-select with sky-soft selection bg.
  - `p_stepper.dart` — circular +/- buttons + centered value + hint slot.
- `providers/auth_provider.dart` — fully wired. Real path: `FirebaseAuth.signIn/createUser` + `updateDisplayName` + `saveParent`. Mock path: synthetic uid + delay. `authStateChanges()` listener keeps state in sync. Hebrew error message mapping (`invalid-email`, `wrong-password`, `email-already-in-use`, etc.).
- `screens/login_screen.dart` — RobotFace at top (happy), segmented `כניסה`/`הרשמה` tabs, form card with `שם ההורה` (signup only), `אימייל` (LTR), `סיסמה` (obscured LTR), inline coral error box, pill submit. `שכחתי סיסמה` link (stub).
- `screens/setup_wizard_screen.dart` — 6 steps: welcome (robot+chip+CTA) → connect (auto-pair 2.6s, status flips chip from `searching` to `online`) → profile (name TextField + age PStepper 7–11) → level (3 POpt cards with robot icons for each starting difficulty) → subjects (2 POpt toggles for math/english) → done (celebrating robot + summary card + writes child to Firestore via ChildProvider). Back/continue footer; final step button disabled while saving.
- `app.dart` — AuthGate now has a `_ChildLoader` that streams `watchChildrenOfParent` after auth: 0 children → wizard, ≥1 → dashboard. Active child auto-loaded into ChildProvider + DeviceProvider + StatsProvider via `addPostFrameCallback` (so providers aren't mutated during build).
- `main.dart` — AuthProvider now constructed with `isMockMode: config.mockMode, firebase: firebaseService`.
- `test/widget_test.dart` — updated to register the full provider tree with all 7 providers; asserts login title + subtitle + that `כניסה` appears (segment tab + button → looser matcher).
- Verification: `dart analyze` → no issues. `flutter test` → 1 passed.

**Next session:** Step 4 — full Dashboard hub (device hero card with RobotFace, today-ring (circular progress), last-session card, 2×2 quick-actions grid + bottom-nav).
