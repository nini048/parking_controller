import 'dart:async';

import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import 'package:firebase_auth/firebase_auth.dart';
import '../services/parking_service.dart';
import '../services/auth_service.dart';
import '../screens/login_screen.dart';

class HomeScreen extends StatefulWidget {
 


  @override
  State<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen> {
  Timer? _countdownTimer;
Duration remainingTime = const Duration();
  @override
  Widget build(BuildContext context) {
    final auth = Provider.of<AuthService>(context);
    final user = auth.currentUser;

    return Scaffold(
      backgroundColor: Colors.grey[100],
      appBar: AppBar(
        backgroundColor: Colors.white,
        foregroundColor: Colors.black87,
        elevation: 1,
        centerTitle: true,
        title: const Text(
          "Bãi đỗ xe",
          style: TextStyle(fontSize: 20, fontWeight: FontWeight.w600),
        ),
        actions: [
          if (user != null)
            IconButton(
              icon: const Icon(Icons.logout),
              onPressed: () => auth.signOut(),
            ),
        ],
      ),

      body: Consumer<ParkingService>(
        builder: (context, parking, _) {
          final free = parking.totalSlots - parking.occupied - parking.reserved;
          final hasReservation =
              user != null && parking.activeReservations.containsKey(user.uid);

          return SingleChildScrollView(
            child: Padding(
              padding: const EdgeInsets.all(18),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  // Chào user
                  if (user != null)
                    Container(
                      padding: const EdgeInsets.all(18),
                      decoration: BoxDecoration(
                        color: Colors.white,
                        borderRadius: BorderRadius.circular(16),
                        boxShadow: [
                          BoxShadow(
                            color: Colors.black12,
                            blurRadius: 6,
                            offset: const Offset(0, 2),
                          ),
                        ],
                      ),
                      child: Row(
                        children: [
                          CircleAvatar(
                            radius: 22,
                            backgroundColor: Colors.blue.shade100,
                            child: Icon(Icons.person, color: Colors.blue.shade700),
                          ),
                          const SizedBox(width: 14),
                          Expanded(
                            child: Text(
                              "Xin chào, ${user.email}",
                              style: TextStyle(
                                fontSize: 16,
                                fontWeight: FontWeight.w600,
                                color: Colors.grey[800],
                              ),
                            ),
                          ),
                        ],
                      ),
                    ),

                  const SizedBox(height: 20),

                  // Thống kê
                  Container(
                    padding: const EdgeInsets.all(20),
                    decoration: BoxDecoration(
                      color: Colors.white,
                      borderRadius: BorderRadius.circular(18),
                      boxShadow: [
                        BoxShadow(
                          color: Colors.black12,
                          blurRadius: 6,
                          offset: const Offset(0, 2),
                        ),
                      ],
                    ),
                    child: Row(
                      mainAxisAlignment: MainAxisAlignment.spaceAround,
                      children: [
                        _statCard(Icons.grid_view, "Tổng", parking.totalSlots.toString()),
                        _statCard(Icons.local_parking, "Đang đỗ", parking.occupied.toString()),
                        _statCard(Icons.event_seat, "Giữ chỗ", parking.reserved.toString()),
                        _statCard(Icons.check_circle, "Trống", free.toString()),
                      ],
                    ),
                  ),

                  const SizedBox(height: 28),

                  // Nút đặt / đăng nhập
                  if (user == null)
                    ElevatedButton(
                      style: ElevatedButton.styleFrom(
                        backgroundColor: Colors.blue,
                        foregroundColor: Colors.white,
                        minimumSize: const Size.fromHeight(52),
                        shape: RoundedRectangleBorder(
                          borderRadius: BorderRadius.circular(14),
                        ),
                      ),
                      onPressed: () => _requireLogin(context),
                      child: const Text(
                        "Đăng nhập để đặt chỗ",
                        style: TextStyle(fontSize: 16, fontWeight: FontWeight.w600),
                      ),
                    )
                  else
                    Column(
                      children: [
                        ElevatedButton(
  style: ElevatedButton.styleFrom(
    minimumSize: const Size.fromHeight(52),
    backgroundColor: hasReservation ? Colors.grey[400] : const Color.fromARGB(255, 97, 97, 227), // xanh lá nhẹ
    foregroundColor: Colors.white,
    shape: RoundedRectangleBorder(
      borderRadius: BorderRadius.circular(14),
    ),
  ),
  onPressed: hasReservation ? null : () => _onReserve(context, user),
  child: Text(
  hasReservation
    ? (remainingTime > Duration.zero
        ? "Còn ${_formatDuration(remainingTime)}"
        : "Bạn đang giữ 1 chỗ")

      : "Đặt trước",
  style: const TextStyle(fontSize: 16, fontWeight: FontWeight.w600),
),

),


                        const SizedBox(height: 10),

                        if (hasReservation)
                          OutlinedButton(
                            style: OutlinedButton.styleFrom(
                              minimumSize: const Size.fromHeight(48),
                              side: const BorderSide(color: Colors.red),
                              shape: RoundedRectangleBorder(
                                borderRadius: BorderRadius.circular(14),
                              ),
                            ),
                            onPressed: () => _onCancel(context, user.uid),
                            child: const Text(
                              "Hủy đặt",
                              style:
                                  TextStyle(fontSize: 15, fontWeight: FontWeight.w600, color: Colors.red),
                            ),
                          ),
                      ],
                    ),

                  const SizedBox(height: 24),

                  const Text(
                    'Ghi chú: Khi bạn check-in tại cổng, hệ thống sẽ huỷ giữ chỗ. Khi xe đỗ, cảm biến sẽ cập nhật trạng thái.',
                    textAlign: TextAlign.center,
                    style: TextStyle(color: Colors.black54),
                  ),
                ],
              ),
            ),
          );
        },
      ),
    );
  }

  // ======== Widget thống kê ========
  Widget _statCard(IconData icon, String label, String value) {
    return Column(
      children: [
        Icon(icon, size: 26, color: Colors.blue.shade700),
        const SizedBox(height: 6),
        Text(
          value,
          style: const TextStyle(fontSize: 18, fontWeight: FontWeight.bold),
        ),
        Text(
          label,
          style: const TextStyle(fontSize: 13, color: Colors.black54),
        ),
      ],
    );
  }

  // ======== Điều hướng Login ========
  void _requireLogin(BuildContext ctx) {
    Navigator.push(
      ctx,
      MaterialPageRoute(builder: (_) => const LoginScreen()),
    );
  }

  // ======== Xử lý đặt chỗ ========
  Future<void> _onReserve(BuildContext ctx, User user) async {
  final parking = Provider.of<ParkingService>(ctx, listen: false);

  try {
    await parking.reserveParking(user);

    // Bắt đầu đếm ngược 1h30p
    setState(() {
      remainingTime = const Duration(hours: 1, minutes: 30);
    });

    _countdownTimer?.cancel();
    _countdownTimer = Timer.periodic(const Duration(seconds: 1), (timer) async {
      if (remainingTime.inSeconds <= 1) {
        timer.cancel();
        await parking.cancelReservation(user.uid);

        if (ctx.mounted) {
          ScaffoldMessenger.of(ctx).showSnackBar(
            const SnackBar(content: Text("Hết thời gian giữ chỗ")),
          );
        }
        return;
      }

      setState(() {
        remainingTime -= const Duration(seconds: 1);
      });
    });

    if (ctx.mounted) {
      ScaffoldMessenger.of(ctx).showSnackBar(
        const SnackBar(content: Text('Đã giữ 1 chỗ cho bạn')),
      );
    }
  } catch (e) {
    final msg = e.toString().replaceFirst('Exception: ', '');
    if (ctx.mounted) {
      ScaffoldMessenger.of(ctx).showSnackBar(
        SnackBar(content: Text(msg), backgroundColor: Colors.red),
      );
    }
  }
}


  // ======== Xử lý hủy đặt ========
Future<void> _onCancel(BuildContext ctx, String userId) async {
  final parking = Provider.of<ParkingService>(ctx, listen: false);
  await parking.cancelReservation(userId);

  // Hủy timer nếu đang chạy
  _countdownTimer?.cancel();
  _countdownTimer = null;

  // Reset thời gian về 0
  setState(() {
    remainingTime = Duration.zero;
  });

  if (ctx.mounted) {
    ScaffoldMessenger.of(ctx).showSnackBar(
      const SnackBar(content: Text('Đã hủy đặt chỗ')),
    );
  }
}

  String _formatDuration(Duration d) {
  String twoDigits(int n) => n.toString().padLeft(2, '0');
  final h = twoDigits(d.inHours);
  final m = twoDigits(d.inMinutes % 60);
  final s = twoDigits(d.inSeconds % 60);
  return "$h:$m:$s";
}

}
