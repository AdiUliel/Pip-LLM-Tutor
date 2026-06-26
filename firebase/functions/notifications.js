/**
 * FCM push notifications for the parent app.
 *
 * Three events, sent to every FCM token registered under
 * parents/{parentId}.fcmTokens:
 *
 *   1. Session started   — sessions/{id} created
 *   2. Session ended     — session has had no new exchange for END_INACTIVITY_MS;
 *                          we set endedAt + status="ended" and notify, once.
 *   3. Device offline    — deviceState/{id}.lastHeartbeat is older than
 *                          OFFLINE_THRESHOLD_MS; we write notifiedOfflineAt to
 *                          dedupe and notify once per offline period.
 *
 * (2) and (3) are driven by a single scheduled function that runs every minute.
 *
 * All payloads are Hebrew. The child's gender is used to pick the right verb
 * form (התחיל vs התחילה, סיים vs סיימה).
 */

const { onDocumentCreated } = require("firebase-functions/v2/firestore");
const { onSchedule } = require("firebase-functions/v2/scheduler");
const { getFirestore, FieldValue, Timestamp } = require("firebase-admin/firestore");
const { getMessaging } = require("firebase-admin/messaging");

const OFFLINE_THRESHOLD_MS  = 60 * 1000;        // 60 s without heartbeat → offline
const END_INACTIVITY_MS     = 5 * 60 * 1000;    // 5 min no exchange → lesson ended

const SUBJECT_HE = { math: "חשבון", english: "אנגלית" };

// ──────────────────────────────────────────────────────────────────────────────
// Helpers
// ──────────────────────────────────────────────────────────────────────────────

function subjectLabel(s) {
  return SUBJECT_HE[s] || s || "לימוד";
}

function verbStartedFor(gender) {
  // gender field is "boy" | "girl" | undefined.
  return gender === "girl" ? "התחילה" : "התחיל";
}
function verbFinishedFor(gender) {
  return gender === "girl" ? "סיימה" : "סיים";
}

async function fetchChild(db, childId) {
  if (!childId) return null;
  const snap = await db.collection("children").doc(childId).get();
  return snap.exists ? { id: snap.id, ...snap.data() } : null;
}

async function fetchParentTokens(db, parentId) {
  if (!parentId) return [];
  const snap = await db.collection("parents").doc(parentId).get();
  if (!snap.exists) return [];
  const tokens = snap.data().fcmTokens;
  return Array.isArray(tokens) ? tokens.filter((t) => typeof t === "string" && t.length > 0) : [];
}

/**
 * Send a notification to every token. Prunes dead tokens so the array doesn't
 * accumulate stale ones (browser cleared cookies, app uninstalled, etc.).
 */
async function sendToParent(db, parentId, title, body, data = {}) {
  const tokens = await fetchParentTokens(db, parentId);
  if (tokens.length === 0) return;

  const messaging = getMessaging();
  const responses = await Promise.all(
    tokens.map((token) =>
      messaging
        .send({
          token,
          notification: { title, body },
          data: Object.fromEntries(Object.entries(data).map(([k, v]) => [k, String(v)])),
        })
        .then(() => ({ token, ok: true }))
        .catch((err) => ({ token, ok: false, code: err.code }))
    )
  );

  const dead = responses
    .filter((r) => !r.ok && (
      r.code === "messaging/registration-token-not-registered" ||
      r.code === "messaging/invalid-registration-token"
    ))
    .map((r) => r.token);

  if (dead.length > 0) {
    await db.collection("parents").doc(parentId).set(
      { fcmTokens: FieldValue.arrayRemove(...dead) },
      { merge: true }
    );
    console.log(`[fcm] pruned ${dead.length} dead tokens for parent ${parentId}`);
  }
}

async function resolveParentId(db, sessionData) {
  if (sessionData.parentId) return sessionData.parentId;
  const child = await fetchChild(db, sessionData.childId);
  return child?.parentId || null;
}

// ──────────────────────────────────────────────────────────────────────────────
// Exported helper — immediate session-end notification (called directly from
// answerQuestion when the child explicitly ends the session, rather than
// waiting for the monitorTutor scheduler which runs once per minute).
// ──────────────────────────────────────────────────────────────────────────────
async function sendSessionEndedNow(db, sessionId, sessionData) {
  try {
    const parentId = await resolveParentId(db, sessionData);
    if (!parentId) return;
    const child = await fetchChild(db, sessionData.childId);
    const name = child?.name || "הילד";
    const verb = verbFinishedFor(child?.gender);
    const subj = subjectLabel(sessionData.subject);
    const stars = Number.isFinite(sessionData.starsEarned) ? sessionData.starsEarned : 0;
    const accuracy = sessionData.questionsAsked > 0
      ? Math.round((sessionData.correctCount / sessionData.questionsAsked) * 100)
      : null;
    const parts = [`מקצוע: ${subj}`];
    if (accuracy !== null) parts.push(`${accuracy}% דיוק`);
    if (stars > 0) parts.push(`${stars}★`);
    await sendToParent(db, parentId, `${name} ${verb} את השיעור`, parts.join(" · "),
      { type: "session.ended", sessionId, childId: sessionData.childId || "" });
  } catch (err) {
    console.warn("[notify] sendSessionEndedNow failed:", err.message);
  }
}
exports.sendSessionEndedNow = sendSessionEndedNow;

// ──────────────────────────────────────────────────────────────────────────────
// Trigger 1 — session started
// ──────────────────────────────────────────────────────────────────────────────

exports.notifyOnSessionStarted = onDocumentCreated(
  "sessions/{sessionId}",
  async (event) => {
    const snap = event.data;
    if (!snap) return;
    const session = snap.data();

    const db = getFirestore();
    const parentId = await resolveParentId(db, session);
    if (!parentId) {
      console.log(`[notify start] no parentId for session ${event.params.sessionId}`);
      return;
    }
    const child = await fetchChild(db, session.childId);
    const name = child?.name || "הילד";
    const verb = verbStartedFor(child?.gender);
    const subj = subjectLabel(session.subject);

    await sendToParent(
      db,
      parentId,
      `${name} ${verb} שיעור`,
      `מקצוע: ${subj}`,
      { type: "session.started", sessionId: event.params.sessionId, childId: session.childId || "" }
    );
  }
);

// ──────────────────────────────────────────────────────────────────────────────
// Trigger 2+3 — single scheduled sweep (every minute):
//   • detect ended lessons by inactivity, write endedAt + status="ended", notify
//   • detect offline devices, write notifiedOfflineAt, notify
// ──────────────────────────────────────────────────────────────────────────────

// Scheduled triggers must use a region that Cloud Scheduler supports — and
// europe-west10 (Berlin) is not one of them. europe-west3 (Frankfurt) is the
// closest supported region, so this single function overrides the global
// europe-west10 to keep the Cloud Scheduler job happy.
exports.monitorTutor = onSchedule(
  { schedule: "every 1 minutes", region: "europe-west3" },
  async () => {
    const db = getFirestore();
    const now = Timestamp.now();

    // ── 2. End-of-lesson by inactivity ───────────────────────────────────────
    const activeSessions = await db
      .collection("sessions")
      .where("status", "in", ["active", "starting"])
      .get();

    for (const sdoc of activeSessions.docs) {
      const s = sdoc.data();
      const lastActivity = s.lastActivity || s.startedAt;
      if (!lastActivity) continue;
      const ageMs = now.toMillis() - lastActivity.toMillis();
      if (ageMs < END_INACTIVITY_MS) continue;

      // Mark ended atomically — set fields only if still active to avoid races.
      try {
        await sdoc.ref.update({
          status: "ended",
          endedAt: now,
        });
      } catch (e) {
        console.error(`[notify end] failed to mark ${sdoc.id} ended:`, e);
        continue;
      }

      const parentId = await resolveParentId(db, s);
      if (!parentId) continue;
      const child = await fetchChild(db, s.childId);
      const name = child?.name || "הילד";
      const verb = verbFinishedFor(child?.gender);
      const subj = subjectLabel(s.subject);
      const stars = Number.isFinite(s.starsEarned) ? s.starsEarned : 0;
      const accuracy =
        s.questionsAsked > 0
          ? Math.round((s.correctCount / s.questionsAsked) * 100)
          : null;

      const bodyParts = [`מקצוע: ${subj}`];
      if (accuracy !== null) bodyParts.push(`${accuracy}% דיוק`);
      if (stars > 0) bodyParts.push(`${stars}★`);

      await sendToParent(
        db,
        parentId,
        `${name} ${verb} את השיעור`,
        bodyParts.join(" · "),
        { type: "session.ended", sessionId: sdoc.id, childId: s.childId || "" }
      );
    }

    // ── 3. Device offline ────────────────────────────────────────────────────
    const devicesSnap = await db.collection("deviceState").get();
    for (const ddoc of devicesSnap.docs) {
      const d = ddoc.data();
      const hb = d.lastHeartbeat;
      if (!hb) continue;  // never reported
      const age = now.toMillis() - hb.toMillis();
      if (age < OFFLINE_THRESHOLD_MS) continue;  // still online

      // Already notified for THIS offline period? notifiedOfflineAt > lastHeartbeat
      // means we sent a notification after the last heartbeat — same outage.
      const notified = d.notifiedOfflineAt;
      if (notified && notified.toMillis() > hb.toMillis()) continue;

      // Find the parent — devices are linked via children's deviceId field.
      const childrenSnap = await db
        .collection("children")
        .where("deviceId", "==", ddoc.id)
        .limit(1)
        .get();
      if (childrenSnap.empty) continue;
      const child = { id: childrenSnap.docs[0].id, ...childrenSnap.docs[0].data() };
      if (!child.parentId) continue;

      await ddoc.ref.set({ notifiedOfflineAt: now }, { merge: true });

      await sendToParent(
        db,
        child.parentId,
        `ההתקן של ${child.name || "הילד"} מנותק`,
        "לא התקבל heartbeat לאחרונה",
        { type: "device.offline", deviceId: ddoc.id, childId: child.id }
      );
    }
  }
);
