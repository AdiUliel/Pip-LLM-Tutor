// OfflineBanner — slim warm-orange banner shown above the Shell content
// whenever there are pending writes queued locally. Translated from
// `OfflineBanner` in ~/Downloads/ioT/shared.jsx.

import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../services/firebase_service.dart';
import '../theme.dart';
import '../utils/offline_queue.dart';

class OfflineBanner extends StatelessWidget {
  const OfflineBanner({super.key});

  @override
  Widget build(BuildContext context) {
    final queue = context.watch<OfflineQueue>();
    if (!queue.hasPending) return const SizedBox.shrink();
    return Material(
      color: AppColors.warn,
      child: InkWell(
        onTap: () async {
          final fb = context.read<FirebaseService>();
          await queue.flush(fb);
        },
        child: Container(
          padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 10),
          decoration: const BoxDecoration(
            border: Border(
              bottom: BorderSide(color: Color(0xFFFCE3BD), width: 1.5),
            ),
          ),
          child: Row(
            children: [
              Container(
                width: 9,
                height: 9,
                decoration: const BoxDecoration(
                  color: Color(0xFFE8920C),
                  shape: BoxShape.circle,
                ),
              ),
              const SizedBox(width: 10),
              Expanded(
                child: Text(
                  '${queue.pendingCount} שינויים ממתינים לסנכרון · הקישו כדי לנסות שוב',
                  style: const TextStyle(
                    color: AppColors.warnInk,
                    fontWeight: FontWeight.w700,
                    fontSize: 13,
                  ),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}
