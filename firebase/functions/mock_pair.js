#!/usr/bin/env node
/**
 * mock_pair.js — simulate a NEW app/parent pairing a child to the device.
 *
 * WHY: pairing normally needs a second phone running the Flutter app. This script
 * does exactly what the app's PairingSheet + child config do, so you can test
 * re-pairing (and the enforceDeviceUniqueness "newest wins" trigger) from a
 * terminal:
 *
 *   1. Resolve the device's Firebase UID from its pairing code
 *      (pairingCodes/{TUTOR-XXXXXX}.firebaseUid) — same lookup the app does.
 *   2. (Optional) Verify deviceState/{uid}.lastHeartbeat is fresh, exactly like
 *      the app's liveness gate (pairingMaxHeartbeatAgeSec = 60 s). If your device's
 *      heartbeats are failing, a real app pairing would fail here too.
 *   3. Set a child's deviceId = UID — either a brand-new mock child (a "new
 *      connection") or an existing one (--child <id>). That write fires the
 *      deployed enforceDeviceUniqueness trigger, which unlinks the device from
 *      every OTHER child (newest pairing wins).
 *   4. Reboot the device (or let the session end) so firestoreResolveChildId()
 *      picks up the newly-linked child.
 *
 * RE-PAIRING IS ALLOWED: you do NOT need to unpair first. enforceDevicePairing.js
 * clears the old child's deviceId automatically when the new one is set.
 *
 * ── Usage ────────────────────────────────────────────────────────────────────
 *   cd firebase/functions
 *   # Against the REAL project (needs credentials — see AUTH below):
 *   node mock_pair.js --code TUTOR-123456 --name "בדיקה" --parent <parentUid>
 *   node mock_pair.js --uid <deviceFirebaseUid> --child <existingChildId>
 *   node mock_pair.js --code TUTOR-123456 --list          # just show current links
 *   node mock_pair.js --code TUTOR-123456 --name Test --dry-run
 *
 *   # Against the Firestore emulator (no credentials needed):
 *   FIRESTORE_EMULATOR_HOST=localhost:8080 node mock_pair.js --uid dev123 --name Test
 *
 * Flags:
 *   --code TUTOR-XXXXXX   device pairing code (resolved via pairingCodes)
 *   --uid  <uid>          device Firebase UID directly (skips the code lookup)
 *   --name <name>         name for a NEW mock child (default "MockKid")
 *   --parent <uid>        parentId for the NEW child (default a generated mock id;
 *                         pass a REAL parent auth uid if you want the app to show it)
 *   --child <childId>     re-pair an EXISTING child instead of creating one
 *   --list               show which children currently point at the device, then exit
 *   --dry-run            print what would happen, write nothing
 *   --project <id>       Firebase project id (default: GCLOUD_PROJECT / firebase config)
 *
 * ── AUTH (real project) ──────────────────────────────────────────────────────
 *   Provide Application Default Credentials, e.g. a service-account key:
 *     export GOOGLE_APPLICATION_CREDENTIALS=/path/to/serviceAccount.json
 *   (or run inside `firebase emulators:start` and use FIRESTORE_EMULATOR_HOST).
 */

"use strict";

const admin = require("firebase-admin");

// ── Defaults mirrored from the Flutter app (AppConstants / ChildSettings) ──────
const DEVICE_ID_PREFIX = "TUTOR-";
const PAIRING_MAX_HEARTBEAT_AGE_SEC = 60;
const DEFAULT_SETTINGS = {
  sessionMinutes: 15,
  breakEveryMinutes: 7,
  dailyLimitMinutes: 30,
  breakFirstQuestions: 7,
  breakEveryQuestions: 4,
  breakAfterMinutes: 15,
  screenOffMinutes: 15,
  deviceSleepMinutes: 50,
};

// ── Tiny arg parser ───────────────────────────────────────────────────────────
function parseArgs(argv) {
  const out = { _: [] };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a.startsWith("--")) {
      const key = a.slice(2);
      const next = argv[i + 1];
      if (next === undefined || next.startsWith("--")) {
        out[key] = true;               // boolean flag
      } else {
        out[key] = next;
        i++;
      }
    } else {
      out._.push(a);
    }
  }
  return out;
}

function die(msg) {
  console.error("✖ " + msg);
  process.exit(1);
}

async function main() {
  const args = parseArgs(process.argv.slice(2));

  if (args.help || args.h) {
    console.log(require("fs").readFileSync(__filename, "utf8").split("*/")[0].slice(3));
    process.exit(0);
  }

  const usingEmulator = !!process.env.FIRESTORE_EMULATOR_HOST;
  const projectId =
    args.project || process.env.GCLOUD_PROJECT || process.env.GOOGLE_CLOUD_PROJECT ||
    (usingEmulator ? "demo-mock" : undefined);

  if (!args.code && !args.uid) {
    die("Provide --code TUTOR-XXXXXX or --uid <deviceUid>. Use --help for usage.");
  }

  admin.initializeApp(projectId ? { projectId } : undefined);
  const db = admin.firestore();

  console.log(
    `\n▸ Target: ${usingEmulator ? "EMULATOR " + process.env.FIRESTORE_EMULATOR_HOST
                                 : "LIVE project " + (projectId || "(default)")}`
  );

  // ── 1. Resolve the device UID ──────────────────────────────────────────────
  let uid = args.uid && args.uid !== true ? String(args.uid) : null;
  let code = args.code && args.code !== true ? String(args.code) : null;
  if (code && !code.startsWith(DEVICE_ID_PREFIX)) code = DEVICE_ID_PREFIX + code;

  if (!uid) {
    const pairSnap = await db.collection("pairingCodes").doc(code).get();
    if (!pairSnap.exists) {
      die(`pairingCodes/${code} not found. Has the device booted and published its code? ` +
          `(watch for "[Firebase] Published pairing code" on serial.)`);
    }
    uid = pairSnap.get("firebaseUid");
    if (!uid) die(`pairingCodes/${code} has no firebaseUid field.`);
    console.log(`▸ Code ${code} → device UID ${uid}`);
  } else {
    console.log(`▸ Device UID ${uid} (given directly)`);
  }

  // ── 2. Liveness gate (same as the app's PairingSheet) ──────────────────────
  const dsSnap = await db.collection("deviceState").doc(uid).get();
  if (!dsSnap.exists) {
    console.warn("⚠ deviceState doc missing — the app would say \"device not found / offline\". " +
                 "It only appears once the device writes deviceState at boot.");
  } else {
    const hb = dsSnap.get("lastHeartbeat");
    const hbMs = hb && hb.toMillis ? hb.toMillis() : (hb ? Date.parse(hb) : NaN);
    const ageSec = Number.isFinite(hbMs) ? Math.round((Date.now() - hbMs) / 1000) : NaN;
    if (!Number.isFinite(ageSec)) {
      console.warn("⚠ deviceState.lastHeartbeat unreadable — skipping liveness check.");
    } else if (ageSec > PAIRING_MAX_HEARTBEAT_AGE_SEC) {
      console.warn(`⚠ Device last seen ${ageSec}s ago (> ${PAIRING_MAX_HEARTBEAT_AGE_SEC}s). ` +
                   `The REAL app would REFUSE to pair here — its heartbeats are stale/failing.`);
    } else {
      console.log(`▸ Device alive (last heartbeat ${ageSec}s ago). Status: ${dsSnap.get("status") || "?"}`);
    }
  }

  // ── Helper: who currently owns this device? ────────────────────────────────
  const linkedChildren = async () => {
    const snap = await db.collection("children").where("deviceId", "==", uid).get();
    return snap.docs.map((d) => ({ id: d.id, name: d.get("name"), parentId: d.get("parentId") }));
  };

  const before = await linkedChildren();
  console.log(`\n▸ Children currently linked to ${uid} (${before.length}):`);
  before.forEach((c) => console.log(`    • ${c.id}  name="${c.name}"  parent=${c.parentId}`));
  if (before.length === 0) console.log("    (none — device is unpaired)");

  if (args.list) {
    console.log("\n(--list) No changes made.");
    process.exit(0);
  }

  // ── 3. Perform the pairing write (what the app does) ───────────────────────
  const existingChildId = args.child && args.child !== true ? String(args.child) : null;
  const name = args.name && args.name !== true ? String(args.name) : "MockKid";
  const parentId = args.parent && args.parent !== true ? String(args.parent)
                                                       : `mock-parent-${uid.slice(0, 8)}`;

  let targetId, action;
  if (existingChildId) {
    targetId = existingChildId;
    action = `re-pair EXISTING child ${existingChildId} → deviceId=${uid}`;
  } else {
    targetId = db.collection("children").doc().id;   // pre-generate so we can log it
    action = `create NEW mock child "${name}" (parent ${parentId}) → deviceId=${uid}`;
  }

  console.log(`\n▸ Will ${action}`);
  if (args["dry-run"]) {
    console.log("(--dry-run) Nothing written.");
    process.exit(0);
  }

  if (existingChildId) {
    const ref = db.collection("children").doc(existingChildId);
    if (!(await ref.get()).exists) die(`children/${existingChildId} does not exist.`);
    await ref.set({ deviceId: uid }, { merge: true });
  } else {
    await db.collection("children").doc(targetId).set({
      parentId,
      name,
      age: 8,
      gender: "boy",
      subjectsEnabled: ["math", "english"],
      topicFocus: {},
      level: { math: 1, english: 1 },
      settings: DEFAULT_SETTINGS,
      deviceId: uid,
      createdAt: admin.firestore.FieldValue.serverTimestamp(),
    });
  }
  console.log(`✔ Wrote children/${targetId} with deviceId=${uid}`);

  // ── 4. Give the enforceDeviceUniqueness trigger a moment, then show result ──
  console.log("\n▸ Waiting 4s for enforceDeviceUniqueness to unlink stale children…");
  await new Promise((r) => setTimeout(r, 4000));
  const after = await linkedChildren();
  console.log(`▸ Children now linked to ${uid} (${after.length}):`);
  after.forEach((c) => console.log(`    • ${c.id}  name="${c.name}"  parent=${c.parentId}`));

  if (after.length === 1 && after[0].id === targetId) {
    console.log("\n✔ Newest-wins pairing confirmed — exactly one child owns the device.");
  } else if (after.length > 1) {
    console.warn("\n⚠ More than one child still linked. The enforceDeviceUniqueness trigger " +
                 "may not be deployed (or the emulator isn't running functions). It will " +
                 "reconcile once deployed; the device's limit-1 query may pick the wrong child until then.");
  }
  console.log("\n➜ Next: reboot the device (or end its session) so it re-resolves its child by deviceId.");
  process.exit(0);
}

main().catch((e) => die(e && e.stack ? e.stack : String(e)));
