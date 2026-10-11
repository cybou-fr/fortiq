import 'package:flutter/material.dart';

/// Shows a failed remote action to the user instead of swallowing it.
void showActionError(BuildContext context, String action, Object error) {
  if (!context.mounted) return;
  final text = error.toString().replaceFirst('Exception: ', '');
  ScaffoldMessenger.of(context).showSnackBar(
    SnackBar(
      backgroundColor: Colors.red.shade900,
      duration: const Duration(seconds: 5),
      content: Text('$action failed: $text'),
    ),
  );
}
