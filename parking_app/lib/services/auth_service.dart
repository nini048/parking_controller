import 'package:firebase_auth/firebase_auth.dart';
import 'package:firebase_database/firebase_database.dart';
import 'package:flutter/material.dart';

class AuthService extends ChangeNotifier {
  final FirebaseAuth _auth = FirebaseAuth.instance;
  final DatabaseReference _db = FirebaseDatabase.instance.ref();

  User? get currentUser => _auth.currentUser;

  Future<bool> signIn(String email, String password) async {
    try {
      print('Đăng nhập: $email');
      
      final cred = await _auth.signInWithEmailAndPassword(email: email, password: password);
      
      // KIỂM TRA USER THỰC TẾ
      if (cred.user != null) {
        print('Đăng nhập thành công: ${cred.user?.uid}');
        notifyListeners();
        return true;
      }
      
      return false;
    } catch (e) {
      print('Lỗi đăng nhập: $e');
      return false;
    }
  }

  Future<void> signOut() async {
    await _auth.signOut();
    notifyListeners();
  }

  Future<String?> getRfidUid(String uid) async {
    try {
      final snap = await _db.child('users').child(uid).child('rfidUid').get();
      return snap.value as String?;
    } catch (e) {
      print('Lỗi lấy RFID: $e');
      return null;
    }
  }
}