import 'dart:convert';
import 'package:shared_preferences/shared_preferences.dart';
import '../models/server_profile.dart';

class ProfileService {
  static const _key = 'fortiq_saved_profiles';

  static Future<List<ServerProfile>> loadProfiles() async {
    final prefs = await SharedPreferences.getInstance();
    final raw = prefs.getString(_key);
    if (raw == null || raw.isEmpty) {
      return [];
    }

    try {
      final List<dynamic> list = jsonDecode(raw);
      return list.map((item) => ServerProfile.fromJson(item as Map<String, dynamic>)).toList();
    } catch (_) {
      return [];
    }
  }

  static Future<void> saveProfiles(List<ServerProfile> profiles) async {
    final prefs = await SharedPreferences.getInstance();
    final jsonString = jsonEncode(profiles.map((p) => p.toJson()).toList());
    await prefs.setString(_key, jsonString);
  }
}
