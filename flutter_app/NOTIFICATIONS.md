# Push notifications setup

The parent app delivers three push notifications via Firebase Cloud Messaging (FCM):

| Trigger | When |
|---|---|
| **`<name> התחיל/ה שיעור`** | Cloud Function fires on `sessions/{id}` create |
| **`<name> סיים/ה את השיעור`** | Scheduled function marks the session ended after 5 min of no exchange, then notifies |
| **`ההתקן של <name> מנותק`** | Scheduled function detects `deviceState/{id}.lastHeartbeat` older than 60 s, dedups per offline period |

Logic lives in `firebase/functions/notifications.js`. Hebrew verb forms use the child's `gender` field (boy/girl).

---

## One-time setup (do these once per project)

### 1. Generate a Web Push VAPID key

1. Open **[Firebase Console → Project Settings → Cloud Messaging](https://console.firebase.google.com/project/llm-tutor-d721e/settings/cloudmessaging)**
2. Scroll to **Web configuration → Web Push certificates → Generate key pair**
3. Copy the **public key** (looks like `B...`, ~88 chars)
4. Paste it into `lib/constants.dart`:
   ```dart
   static const String fcmVapidKey = 'B...'; // your public key here
   ```
5. Rebuild + redeploy the web app: `flutter build web --release --pwa-strategy=none && firebase deploy --only hosting`

Without this, **Android push still works** but **web push silently fails** (the `getToken` call returns null on web).

### 2. Enable Cloud Scheduler API

The `monitorTutor` function runs on a schedule (every minute). Cloud Scheduler must be enabled in the GCP project:

1. Open **[GCP Console → APIs → Cloud Scheduler API](https://console.cloud.google.com/apis/library/cloudscheduler.googleapis.com?project=llm-tutor-d721e)**
2. Click **Enable**

First-time deploy will fail with a clear "Enable Cloud Scheduler" error if you skip this step.

### 3. Deploy the Cloud Functions

```bash
cd firebase/functions
firebase deploy --only functions
```

This adds `notifyOnSessionStarted` and `monitorTutor` alongside the partner's existing tutor functions.

---

## Schema additions

```
parents/{uid}.fcmTokens: array<string>       # written by the Flutter app on attach
deviceState/{deviceId}.notifiedOfflineAt: timestamp   # written by monitorTutor to dedupe
sessions/{id}.endedAt: timestamp              # written by monitorTutor when ending by inactivity
sessions/{id}.status: "ended"                 # same — partner's tutor sets "active", we transition to "ended"
```

Firestore rules already allow the parent to write their own `parents/{uid}` doc (`request.auth.uid == parentId`), so the token registration goes through with no rule change.

---

## How it works end-to-end

1. Parent signs into the app. `FcmService.attach(uid)` is called from `main.dart`'s auth listener.
2. The service asks the OS for notification permission, fetches an FCM token, and writes it via `addParentFcmToken` (array union, idempotent).
3. The token rotates occasionally → `onTokenRefresh` keeps Firestore in sync.
4. Parent signs out → `detach()` removes the token from the array so notifications stop reaching this device.
5. The partner's ESP32 creates a session → `notifyOnSessionStarted` fires → reads the parent's tokens → sends FCM to each → OS shows the notification (or the foreground SnackBar via `onForegroundMessage` if the app is open).
6. Same flow for the scheduled triggers: every 60 s, check active sessions for inactivity (5 min no exchange) and devices for stale heartbeats.

Dead tokens (uninstall, cleared cookies) are pruned automatically — the send function catches `messaging/registration-token-not-registered` and removes the token from the array.

---

## Files added / changed

| Path | Purpose |
|---|---|
| `firebase/functions/notifications.js` | The three triggers + the send helper |
| `firebase/functions/index.js` | Re-exports the two new functions |
| `lib/constants.dart` | `fcmVapidKey` placeholder |
| `lib/services/firebase_service.dart` (+ `_real.dart`) | `addParentFcmToken` / `removeParentFcmToken` |
| `lib/services/fcm_service.dart` | Permission, token, refresh, foreground callback |
| `lib/main.dart` | Wires FCM to the auth lifecycle |
| `web/firebase-messaging-sw.js` | Background service worker for browser push |
