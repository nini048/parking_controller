// parking_service.dart
import 'dart:async';
import 'package:firebase_database/firebase_database.dart';
import 'package:flutter/foundation.dart';
import 'package:firebase_auth/firebase_auth.dart';

class ParkingService extends ChangeNotifier {
  // KẾT NỐI DB
  final DatabaseReference _db = FirebaseDatabase.instance.ref();

  // TRẠNG THÁI CẨN LƯU TRONG BỘ NHỚ
  int totalSlots = 0;
  int reserved = 0;
  int occupied = 0;
  Map<String, Map<String, dynamic>> activeReservations = {}; // userId -> data

  // THỜI GIAN GIỮ CHỖ
  static const Duration reservationHoldDuration = Duration(minutes: 90);

  // TIMER ĐỂ TỰ ĐỘNG HỦY reservation theo user
  final Map<String, Timer> _autoReleaseTimers = {};

  ParkingService() {
    _initListeners();
  }

  // KHỞI TẠO LẮNG NGHE DB: parking node và reservations
  void _initListeners() {
    // Lắng nghe node parking để cập nhật tổng/reserved/occupied
    _db.child('parking').onValue.listen((event) {
      final data = event.snapshot.value as Map<Object?, Object?>?;
      if (data != null) {
        totalSlots = (data['totalSlots'] as int?) ?? totalSlots;
        reserved = (data['reserved'] as int?) ?? reserved;
        occupied = (data['occupied'] as int?) ?? occupied;
        notifyListeners();
      }
    });

    // Lắng nghe toàn bộ reservations để đồng bộ activeReservations
    _db.child('reservations').onChildAdded.listen(_onReservationAdded);
    _db.child('reservations').onChildChanged.listen(_onReservationChanged);
    _db.child('reservations').onChildRemoved.listen(_onReservationRemoved);

    // Khởi tạo hiện có (nếu app mới start)
    _loadInitialParking();
    _loadInitialReservations();
  }

  // TẢI THÔNG SỐ BAN ĐẦU TỪ DB
  Future<void> _loadInitialParking() async {
    final snap = await _db.child('parking').get();
    final data = snap.value as Map<Object?, Object?>?;
    if (data != null) {
      totalSlots = (data['totalSlots'] as int?) ?? totalSlots;
      reserved = (data['reserved'] as int?) ?? reserved;
      occupied = (data['occupied'] as int?) ?? occupied;
      notifyListeners();
    }
  }

  // TẢI RESERVATIONS BAN ĐẦU
  Future<void> _loadInitialReservations() async {
    final snap = await _db.child('reservations').get();
    if (snap.exists) {
      for (final child in snap.children) {
        final userId = child.key!;
        final value = child.value as Map<Object?, Object?>;
        activeReservations[userId] = {
          'reservedAt': value['reservedAt'],
          'expiresAt': value['expiresAt'],
        };
        _scheduleAutoReleaseForUser(userId, value['expiresAt'] as int?);
      }
      notifyListeners();
    }
  }

  // XỬ LÝ SỰ KIỆN DATABASE RESERVATION ADDED/CHANGED/REMOVED
  void _onReservationAdded(DatabaseEvent event) {
    final userId = event.snapshot.key;
    final data = event.snapshot.value as Map<Object?, Object?>?;
    if (userId != null && data != null) {
      activeReservations[userId] = {
        'reservedAt': data['reservedAt'],
        'expiresAt': data['expiresAt'],
      };
      _scheduleAutoReleaseForUser(userId, data['expiresAt'] as int?);
      notifyListeners();
    }
  }

  void _onReservationChanged(DatabaseEvent event) {
    final userId = event.snapshot.key;
    final data = event.snapshot.value as Map<Object?, Object?>?;
    if (userId != null && data != null) {
      activeReservations[userId] = {
        'reservedAt': data['reservedAt'],
        'expiresAt': data['expiresAt'],
      };
      _scheduleAutoReleaseForUser(userId, data['expiresAt'] as int?);
      notifyListeners();
    }
  }

  void _onReservationRemoved(DatabaseEvent event) {
    final userId = event.snapshot.key;
    if (userId != null) {
      activeReservations.remove(userId);
      _autoReleaseTimers[userId]?.cancel();
      _autoReleaseTimers.remove(userId);
      notifyListeners();
    }
  }

  // LẬP LỊCH HỦY TỰ ĐỘNG CHO MỖI USER
  void _scheduleAutoReleaseForUser(String userId, int? expiresAtMs) {
    // Huỷ timer cũ nếu có
    _autoReleaseTimers[userId]?.cancel();
    if (expiresAtMs == null) return;

    final now = DateTime.now().millisecondsSinceEpoch;
    final delayMs = expiresAtMs - now;
    if (delayMs <= 0) {
      // Đã quá hạn -> hủy ngay
      _releaseReservationInternal(userId);
      return;
    }

    _autoReleaseTimers[userId] = Timer(Duration(milliseconds: delayMs), () async {
      await _releaseReservationInternal(userId);
    });
  }

  // HỖ TRỢ GIẢI PHÓNG RESERVATION (nội bộ)
  Future<void> _releaseReservationInternal(String userId) async {
    // Xóa bản ghi reservation
    await _db.child('reservations').child(userId).remove();

    // Giảm reserved trên parking node (đảm bảo không âm)
    final snap = await _db.child('parking/reserved').get();
    final curReserved = (snap.value as int?) ?? 0;
    final newReserved = (curReserved - 1) < 0 ? 0 : (curReserved - 1);
    await _db.child('parking/reserved').set(newReserved);

    // Hủy timer và state local
    _autoReleaseTimers[userId]?.cancel();
    _autoReleaseTimers.remove(userId);
    activeReservations.remove(userId);

    // Ghi event (tuỳ chọn)
    await _db.child('events').push().set({
      'event': 'reservation_released',
      'userId': userId,
      'timestamp': DateTime.now().millisecondsSinceEpoch,
    });

    notifyListeners();
  }

  // KIỂM TRA NGƯỜI DÙNG ĐÃ ĐẶT CHƯA
  Future<bool> hasExistingReservation(String userId) async {
    final snap = await _db.child('reservations').child(userId).get();
    return snap.exists;
  }

  // HÀM ĐẶT CHỖ (KHÔNG CHỌN SLOT)
  Future<void> reserveParking(User user) async {
    final userId = user.uid;

    // Kiểm tra đã có booking
    if (await hasExistingReservation(userId)) {
      throw Exception("Bạn đã giữ 1 chỗ rồi!");
    }

    // Lấy số chỗ hiện có
    final totalSnap = await _db.child('parking/totalSlots').get();
    final occSnap = await _db.child('parking/occupied').get();
    final reservedSnap = await _db.child('parking/reserved').get();

    final curTotal = (totalSnap.value as int?) ?? 0;
    final curOcc = (occSnap.value as int?) ?? 0;
    final curReserved = (reservedSnap.value as int?) ?? 0;

    final free = curTotal - curOcc - curReserved;
    if (free <= 0) {
      throw Exception("Bãi đã hết chỗ!");
    }

    // Tạo reservation
    final now = DateTime.now().millisecondsSinceEpoch;
    final expiry = now + reservationHoldDuration.inMilliseconds;

    await _db.child('reservations').child(userId).set({
      'reservedAt': now,
      'expiresAt': expiry,
    });

    // Tăng reserved
    await _db.child('parking').child('reserved').set(curReserved + 1);

    // Ghi event
    await _db.child('events').push().set({
      'event': 'reserved',
      'userId': userId,
      'timestamp': now,
    });
  }

  // HỦY ĐẶT CHỖ BỞI NGƯỜI DÙNG
  Future<void> cancelReservation(String userId) async {
    final snap = await _db.child('reservations').child(userId).get();
    if (!snap.exists) return;

    // Xóa reservation
    await _db.child('reservations').child(userId).remove();

    // Giảm reserved count
    final reservedSnap = await _db.child('parking/reserved').get();
    final curReserved = (reservedSnap.value as int?) ?? 0;
    final newReserved = (curReserved - 1) < 0 ? 0 : (curReserved - 1);
    await _db.child('parking/reserved').set(newReserved);

    // Ghi event
    await _db.child('events').push().set({
      'event': 'canceled',
      'userId': userId,
      'timestamp': DateTime.now().millisecondsSinceEpoch,
    });
  }

  // KHI NGƯỜI DÙNG QUA CỔNG (CHECK-IN) -- dùng RFID/UID từ cổng
  // HÀM NÀY sẽ xóa reservation (nếu có) và không làm thay đổi occupied ở đây.
  // occupied sẽ do sensor slot cập nhật khi xe thực sự đỗ vào slot.
  Future<void> checkInAtGate(String rfidUid, {String? userIdFromAuth}) async {
    // Nếu có mapping rfid -> user (tuỳ hệ thống), mình ưu tiên tìm reservation bằng userIdFromAuth.
    final userId = userIdFromAuth;

    if (userId != null) {
      final exists = await _db.child('reservations').child(userId).get();
      if (exists.exists) {
        // Xóa reservation và giảm reserved
        await cancelReservation(userId);
      }
    } else {
      // Nếu không có userId, cố gắng tìm reservation bằng rfidUid trong nodes reservations (nếu bạn lưu rfidUid)
      final snap = await _db.child('reservations').get();
      if (snap.exists) {
        for (final child in snap.children) {
          final data = child.value as Map<Object?, Object?>;
          if (data['rfidUid'] != null && data['rfidUid'] == rfidUid) {
            final uid = child.key!;
            await cancelReservation(uid);
            break;
          }
        }
      }
    }

    // Ghi event check-in
    await _db.child('events').push().set({
      'event': 'gate_checkin',
      'rfid': rfidUid,
      'userId': userId ?? '',
      'timestamp': DateTime.now().millisecondsSinceEpoch,
    });
  }

  // CẬP NHẬT KHI CẢM BIẾN SLOT PHÁT HIỆN XE (occupied)
  Future<void> updateOccupied(int delta) async {
    final snap = await _db.child('parking/occupied').get();
    final cur = (snap.value as int?) ?? 0;
    final newVal = cur + delta;
    final bounded = newVal < 0 ? 0 : (newVal > totalSlots ? totalSlots : newVal);
    await _db.child('parking/occupied').set(bounded);
    await _db.child('events').push().set({
      'event': 'occupied_updated',
      'delta': delta,
      'new': bounded,
      'timestamp': DateTime.now().millisecondsSinceEpoch,
    });
  }

  // HÀM DỌN DẸP: clear expired reservation thủ công (fallback)
  Future<void> clearExpiredReservations() async {
    final now = DateTime.now().millisecondsSinceEpoch;
    final snap = await _db.child('reservations').get();
    if (!snap.exists) return;
    for (final child in snap.children) {
      final data = child.value as Map<Object?, Object?>;
      final expiresAt = data['expiresAt'] as int? ?? 0;
      final userId = child.key!;
      if (now > expiresAt) {
        await _releaseReservationInternal(userId);
      }
    }
  }

  @override
  void dispose() {
    for (final t in _autoReleaseTimers.values) {
      t.cancel();
    }
    super.dispose();
  }
}
