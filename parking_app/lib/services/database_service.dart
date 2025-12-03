import 'package:firebase_database/firebase_database.dart';
import 'package:flutter/material.dart';

class DatabaseService extends ChangeNotifier {
  final DatabaseReference _db = FirebaseDatabase.instance.ref();
  Map<String, Map<String, dynamic>> _slots = {};

  Map<String, Map<String, dynamic>> get slots => _slots;

  // Danh sách mã QR cố định (dán tại slot)
  static const Map<String, String> fixedQRCodes = {
    'slot1': 'SLOT_001',
    'slot2': 'SLOT_002',
    'slot3': 'SLOT_003',
    'slot4': 'SLOT_004',
    // Thêm bao nhiêu slot tùy ý
  };

  static const Duration reservationHoldDuration = Duration(minutes: 90);

  // Stream realtime
  Stream<Map<String, Map<String, dynamic>>> get slotsStream => _db.child('parking/slots').onValue.map((event) {
    final data = event.snapshot.value as Map<Object?, Object?>?;
    final Map<String, Map<String, dynamic>> result = {};

    data?.forEach((key, value) {
      final map = (value as Map).cast<String, dynamic>();
      final slotId = key.toString();
      map['qrCode'] = fixedQRCodes[slotId] ?? 'UNKNOWN';
      result[slotId] = map;
    });

    return result;
  });

  DatabaseService() {
    print('[DB] Lắng nghe /parking/slots...');
    _db.child('parking/slots').onValue.listen((event) {
      final data = event.snapshot.value as Map<Object?, Object?>?;
      if (data == null) {
        _slots = {};
      } else {
        _slots = data.map((key, value) {
          final map = (value as Map).cast<String, dynamic>();
          final slotId = key.toString();
          map['qrCode'] = fixedQRCodes[slotId] ?? 'UNKNOWN';
          return MapEntry(slotId, map);
        });
      }
      print('[DB] Cập nhật slots: $_slots');
      notifyListeners();
      _checkAllExpirations();
    });

    // Khởi tạo QR cố định lần đầu (nếu chưa có)
    _initializeFixedQRCodes();
  }

  // KHỞI TẠO MÃ QR CỐ ĐỊNH (chạy 1 lần)
  Future<void> _initializeFixedQRCodes() async {
    for (var entry in fixedQRCodes.entries) {
      final slotId = entry.key;
      final qrCode = entry.value;

      final snap = await _db.child('parking/slots').child(slotId).child('qrCode').get();
      if (!snap.exists) {
        await _db.child('parking/slots').child(slotId).set({
          'status': 0,
          'qrCode': qrCode,
          'qrScanned': false,
        });
        print('[DB] Khởi tạo QR cố định: $slotId = $qrCode');
      }
    }
  }

  // ĐẶT CHỖ TRƯỚC
  Future<void> reserveSlot(String slotId, String userId) async {
    final now = DateTime.now().millisecondsSinceEpoch;
    final expiry = now + reservationHoldDuration.inMilliseconds; // 90 phút

    await _db.child('parking/slots').child(slotId).update({
      'status': 2,
      'reservedBy': userId,
      'reservationTime': now,
      'timerExpiry': expiry,
      'qrScanned': false,
      'qrScannedTime': null,
    });
    print('[DB] Đặt trước $slotId');
  }

  // HỦY ĐẶT CHỖ
  Future<void> cancelReservation(String slotId) async {
    await _db.child('parking/slots').child(slotId).update({
      'status': 0,
      'reservedBy': null,
      'reservationTime': null,
      'timerExpiry': null,
      'qrScanned': false,
      'qrScannedTime': null,
    });
    print('[DB] Hủy đặt trước $slotId');
  }

  // CẬP NHẬT TỪ CẢM BIẾN
  Future<void> updateSlotFromSensor(String slotId, bool isEmpty) async {
    final status = isEmpty ? 0 : 1;
    await _db.child('parking/slots').child(slotId).update({'status': status});
  }

  // XÁC NHẬN QUÉT QR
  Future<String> confirmQRScan(String scannedQR, String userId) async {
    // Tìm slot có mã QR này
    for (var entry in fixedQRCodes.entries) {
      if (entry.value == scannedQR) {
        final slotId = entry.key;

        final data = _slots[slotId];
        if (data == null) return "Slot không tồn tại";

        // Kiểm tra: có đặt trước và đúng người?
        if (data['status'] != 2 || data['reservedBy'] != userId) {
          await _sendEvent("qr_wrong_user", slotId, userId);
          return "Bạn không đặt chỗ này!";
        }

        final now = DateTime.now().millisecondsSinceEpoch;
        await _db.child('parking/slots').child(slotId).update({
          'qrScanned': true,
          'qrScannedTime': now,
        });

        await _sendEvent("qr_scanned", slotId, userId);
        return "Xác nhận thành công!";
      }
    }
    await _sendEvent("qr_invalid", "", userId);
    return "Mã QR không hợp lệ!";
  }

  // TỰ ĐỘNG HỦY NẾU KHÔNG QUÉT TRONG THỜI GIAN GIỮ CHỖ
  void _checkAllExpirations() {
    final now = DateTime.now().millisecondsSinceEpoch;
    _slots.forEach((slotId, data) async {
      if (data['status'] == 2 && data['qrScanned'] != true) {
        final reservationTime = data['reservationTime'] as int?;
        if (reservationTime != null && now - reservationTime > reservationHoldDuration.inMilliseconds) {
          await cancelReservation(slotId);
          await _sendEvent("qr_timeout", slotId, data['reservedBy']);
        }
      }
    });
  }

  // GỬI SỰ KIỆN
  Future<void> _sendEvent(String event, String slotId, String userId) async {
    await _db.child('parking/events').push().set({
      'event': event,
      'slot': slotId,
      'userId': userId,
      'timestamp': DateTime.now().millisecondsSinceEpoch,
    });
  }
}