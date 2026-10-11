import 'dart:convert';
import 'package:shared_preferences/shared_preferences.dart';

/// Remembers SSH host key fingerprints (trust on first use).
///
/// Fingerprints are public, so they live in plain preferences.
class KnownHostsService {
  static const _key = 'fortiq_known_hosts';

  static String _id(String host, int port) => '${host.toLowerCase()}:$port';

  static Future<Map<String, String>> _load() async {
    final prefs = await SharedPreferences.getInstance();
    final raw = prefs.getString(_key);
    if (raw == null || raw.isEmpty) return {};
    try {
      return (jsonDecode(raw) as Map<String, dynamic>).cast<String, String>();
    } catch (_) {
      return {};
    }
  }

  /// Returns the stored `"<type> <fingerprint>"` for the host, if any.
  static Future<String?> lookup(String host, int port) async {
    return (await _load())[_id(host, port)];
  }

  static Future<void> remember(String host, int port, String value) async {
    final all = await _load();
    all[_id(host, port)] = value;
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(_key, jsonEncode(all));
  }
}
