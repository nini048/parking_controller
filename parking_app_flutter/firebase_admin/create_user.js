const admin = require('firebase-admin');
const serviceAccount = require('./serviceAccountKey.json');

admin.initializeApp({
  credential: admin.credential.cert(serviceAccount),
  databaseURL: "https://parking-app-2025-default-rtdb.asia-southeast1.firebasedatabase.app" // ← THAY DB CỦA BẠN
});

async function createUser() {
  const email = "user1@example.com";
  const password = "123456"; // ← MÃ HÓA TRONG AUTH
  const rfidUid = "A1B2C3D4";

  try {
    const userRecord = await admin.auth().createUser({ email, password });
    const uid = userRecord.uid;
    console.log("Tạo user thành công! UID:", uid);

    // LƯU RFID UID VÀO DATABASE (KHÔNG LƯU PASSWORD)
    await admin.database().ref('users').child(uid).set({
      rfidUid: rfidUid,
      role: "user"
    });
    console.log("Lưu RFID thành công!");
  } catch (error) {
    console.error("Lỗi:", error.message);
  } finally {
    process.exit();
  }
}

createUser();
