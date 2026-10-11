import 'dart:convert';
import 'package:flutter/material.dart';

class ServerProfile {
  final String id;
  String name;
  String host;
  int port;
  String username;
  bool useKeyAuth;
  String? keyPath;

  /// A key imported into the app (see PrivateKeyStore) is used instead of keyPath.
  bool hasStoredKey;
  Color domainColor;

  ServerProfile({
    required this.id,
    required this.name,
    required this.host,
    this.port = 22,
    required this.username,
    this.useKeyAuth = true,
    this.keyPath,
    this.hasStoredKey = false,
    this.domainColor = const Color(0xFFEF4444),
  });

  Map<String, dynamic> toJson() => {
        'id': id,
        'name': name,
        'host': host,
        'port': port,
        'username': username,
        'useKeyAuth': useKeyAuth,
        'keyPath': keyPath,
        'hasStoredKey': hasStoredKey,
        'domainColor': domainColor.value,
      };

  factory ServerProfile.fromJson(Map<String, dynamic> json) => ServerProfile(
        id: json['id'] as String,
        name: json['name'] as String,
        host: json['host'] as String,
        port: (json['port'] as int?) ?? 22,
        username: json['username'] as String,
        useKeyAuth: (json['useKeyAuth'] as bool?) ?? true,
        keyPath: json['keyPath'] as String?,
        hasStoredKey: (json['hasStoredKey'] as bool?) ?? false,
        domainColor: Color((json['domainColor'] as int?) ?? 0xFF8B5CF6),
      );
}
