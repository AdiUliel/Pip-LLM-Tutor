const admin = require("firebase-admin");
const { initializeFirestore } = require("firebase-admin/firestore");
const app = admin.initializeApp();
const db = initializeFirestore(app, { preferRest: true });
(async () => {
  const NAME = "יציק";
  // exact match first
  let snap = await db.collection("children").where("name", "==", NAME).get();
  let docs = snap.docs;
  if (!docs.length) {
    // fallback: scan all children and substring-match (handles spacing/niqqud)
    const all = await db.collection("children").get();
    docs = all.docs.filter(d => String(d.data().name || "").includes(NAME));
    console.log(`(exact match: 0 · scanned ${all.size} children for a substring match)`);
  }
  if (!docs.length) { console.log(`No child named "${NAME}" found.`); process.exit(0); }
  for (const d of docs) {
    const c = d.data();
    console.log(`\n— child "${c.name}" (id: ${d.id})`);
    console.log(`   parentId: ${c.parentId} · deviceId: ${c.deviceId || "(none)"} · age: ${c.age}`);
    if (c.parentId) {
      const p = await db.collection("parents").doc(c.parentId).get();
      if (p.exists) { const pd = p.data(); console.log(`   parent: ${pd.name || "(no name)"} · ${pd.email || "(no email)"}`); }
      else console.log("   parent: (parents doc not found)");
    }
  }
  process.exit(0);
})().catch(e => { console.error("ERR:", e.message); process.exit(1); });
