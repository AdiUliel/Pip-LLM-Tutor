/**
 * enforceDeviceUniqueness — one device ↔ one PARENT (siblings may share it).
 *
 * A physical ESP32 has a STABLE anonymous UID and a MAC-derived pairing code,
 * and resolves "its" child on the device with:
 *     children where deviceId == <device UID>  limit 1
 * (see ESP32/homework_assistant/firebase_client.h: firestoreResolveChildId).
 *
 * Pairing the same device to a NEW child (pairing_sheet → child.deviceId = UID)
 * only sets the new child's deviceId — the PREVIOUS child keeps it. Two children
 * then share the same deviceId, and the device's limit-1 query sticks to the
 * stale/older one ("always loads the last child, e.g. Naama"). The device query
 * also has no parentId filter, so a device re-paired across accounts matches
 * children from both.
 *
 * This trigger keeps a device within ONE parent account: whenever a child's
 * deviceId becomes a (new) non-empty value, it clears that deviceId from every
 * OTHER-PARENT child (cross-account safety). Siblings under the SAME parent may
 * share the device — they're told apart per session by the voice identify flow
 * (matchChildByName → session.childId), so they're intentionally NOT unlinked.
 */

const { onDocumentWritten } = require("firebase-functions/v2/firestore");
const { getFirestore } = require("firebase-admin/firestore");

exports.enforceDeviceUniqueness = onDocumentWritten(
  "children/{childId}",
  async (event) => {
    const after = event.data?.after?.data();
    if (!after) return;                    // deletion — nothing to enforce
    const deviceId = after.deviceId;
    if (!deviceId) return;                 // child not paired — nothing to enforce

    // Act only when THIS write actually set/changed the deviceId, so ordinary
    // child edits (name / settings / level …) don't trigger a full scan. On a
    // fresh create `before` is undefined → treat as a change.
    const before = event.data?.before?.data();
    if (before && before.deviceId === deviceId) return;

    const db = getFirestore();
    const thisId = event.params.childId;

    const dupes = await db
      .collection("children")
      .where("deviceId", "==", deviceId)
      .get();

    // Siblings (same parent) may SHARE one device — the device tells them apart
    // per session via the voice identify flow (matchChildByName → session.childId).
    // So unlink only children of OTHER parents (keeps cross-account safety).
    const thisParentId = after.parentId;
    const stale = dupes.docs.filter(
      (d) => d.id !== thisId && d.data().parentId !== thisParentId
    );
    if (stale.length === 0) return;

    // Clear the link on every other child that still claims this device.
    // Writing deviceId:"" re-fires this trigger for those docs, but the empty
    // guard above returns immediately — no loop.
    await Promise.all(stale.map((d) => d.ref.update({ deviceId: "" })));
    console.log(
      `[pairing] device ${deviceId} -> child ${thisId}; unlinked ${stale.length} ` +
      `stale child(ren): ${stale.map((d) => d.id).join(", ")}`
    );
  }
);
