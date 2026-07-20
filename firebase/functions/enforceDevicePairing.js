/**
 * enforceDeviceUniqueness — the device belongs to the PARENT ACCOUNT (family
 * device): every child under the same parent shares it automatically.
 *
 * A physical ESP32 has a STABLE anonymous UID and a MAC-derived pairing code,
 * and resolves "its" child on the device with:
 *     children where deviceId == <device UID>  limit 1
 * (see ESP32/homework_assistant/firebase_client.h: firestoreResolveChildId).
 * That limit-1 pick is arbitrary among siblings — which is fine: a session is
 * attributed to a specific child ONLY by the voice identify flow (the child
 * says their name → matchChildByName over the parent's children →
 * session.childId), and the firmware reports activeChildId only after that.
 *
 * What this trigger does on every children/{childId} write:
 *   1. deviceId SET/CHANGED on a child →
 *      a. clear that deviceId from every OTHER-PARENT child (cross-account
 *         safety: a re-paired device must not match two accounts), and
 *      b. COPY it to every same-parent sibling — pairing through ANY child
 *         links the whole family, so each child's app view shows the device.
 *   2. child CREATED without a deviceId → inherit the family device from any
 *      sibling that has one (adding a child auto-connects it).
 *
 * Loop safety: propagation re-fires the trigger, but siblings already holding
 * the same deviceId are filtered out and unchanged-deviceId writes return
 * early, so the cascade converges. Clearing (deviceId:"") on an EXISTING doc
 * returns early too — the technician reset (which blanks the whole family)
 * does not resurrect the link.
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
