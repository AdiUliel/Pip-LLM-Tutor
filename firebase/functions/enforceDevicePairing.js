/**
 * enforceDeviceUniqueness — the device belongs to the parent account: all the
 * parent's children share it. On every children/{childId} write:
 *   1. deviceId set/changed → clear it from other parents' children
 *      (cross-account safety) and copy it to all same-parent siblings.
 *   2. child created without a deviceId → inherit the family device.
 * A session is attributed to a specific child only by the voice identify flow.
 * Loop safety: siblings that already hold the deviceId are skipped and
 * unchanged/empty writes return early, so the cascade converges.
 */

const { onDocumentWritten } = require("firebase-functions/v2/firestore");
const { getFirestore } = require("firebase-admin/firestore");

exports.enforceDeviceUniqueness = onDocumentWritten(
  "children/{childId}",
  async (event) => {
    const after = event.data?.after?.data();
    if (!after) return;                    // deletion — nothing to enforce
    const db = getFirestore();
    const thisId = event.params.childId;
    const before = event.data?.before?.data();
    const deviceId = after.deviceId;

    // ── No device on this child ──────────────────────────────────────────────
    if (!deviceId) {
      // Newly CREATED child (no `before`): inherit the family device from a
      // sibling, so a child added later is connected without re-pairing.
      // Existing docs being cleared (reset/unpair) return here untouched.
      if (!before && after.parentId) {
        const family = await db
          .collection("children")
          .where("parentId", "==", after.parentId)
          .get();
        const donor = family.docs.find((d) => d.id !== thisId && d.data().deviceId);
        if (donor) {
          await event.data.after.ref.update({ deviceId: donor.data().deviceId });
          console.log(
            `[pairing] new child ${thisId} inherited family device ` +
            `${donor.data().deviceId} from sibling ${donor.id}`
          );
        }
      }
      return;
    }

    // Act only when THIS write actually set/changed the deviceId, so ordinary
    // child edits (name / settings / level …) don't trigger a full scan. On a
    // fresh create `before` is undefined → treat as a change.
    if (before && before.deviceId === deviceId) return;

    // ── deviceId set/changed → the device follows the whole family ──────────
    const thisParentId = after.parentId;
    const [dupes, family] = await Promise.all([
      db.collection("children").where("deviceId", "==", deviceId).get(),
      thisParentId
        ? db.collection("children").where("parentId", "==", thisParentId).get()
        : Promise.resolve({ docs: [] }),
    ]);

    // a. Cross-account safety: strip the device from other parents' children.
    const otherParents = dupes.docs.filter(
      (d) => d.id !== thisId && d.data().parentId !== thisParentId
    );
    // b. Family share: copy to same-parent siblings that don't have it yet
    //    (also swaps them over when the family pairs a REPLACEMENT device).
    const toShare = family.docs.filter(
      (d) => d.id !== thisId && d.data().deviceId !== deviceId
    );

    if (otherParents.length === 0 && toShare.length === 0) return;
    await Promise.all([
      ...otherParents.map((d) => d.ref.update({ deviceId: "" })),
      ...toShare.map((d) => d.ref.update({ deviceId })),
    ]);
    console.log(
      `[pairing] device ${deviceId} -> child ${thisId}; ` +
      `shared with ${toShare.length} sibling(s), ` +
      `unlinked ${otherParents.length} other-parent child(ren)`
    );
  }
);
