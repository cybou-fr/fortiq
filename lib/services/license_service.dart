import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'package:path_provider/path_provider.dart';

/// Public product IDs and hosted links only. No administrative API secrets.
class FortiqLicenseConfig {
  static const storeId = String.fromEnvironment('FORTIQ_LS_STORE_ID');
  static const introVariantId = String.fromEnvironment('FORTIQ_LS_INTRO_VARIANT_ID');
  static const proVariantId = String.fromEnvironment('FORTIQ_LS_PRO_VARIANT_ID');
  static const introCheckout = String.fromEnvironment('FORTIQ_LS_INTRO_CHECKOUT');
  static const proCheckout = String.fromEnvironment('FORTIQ_LS_PRO_CHECKOUT');
  static const storeSlug = String.fromEnvironment('FORTIQ_LS_STORE_SLUG');

  static const requireLicense = bool.fromEnvironment('dart.vm.product') ||
      bool.fromEnvironment('FORTIQ_REQUIRE_LICENSE');

  static bool get configured {
    final store = int.tryParse(storeId);
    final intro = int.tryParse(introVariantId);
    final pro = int.tryParse(proVariantId);
    return store != null && store > 0 && intro != null && intro > 0 &&
        pro != null && pro > 0 && intro != pro;
  }

  static Uri? get portalUrl {
    final slug = storeSlug.trim();
    if (!RegExp(r'^[a-zA-Z0-9-]+$').hasMatch(slug)) return null;
    return Uri.parse('https://' + slug + '.lemonsqueezy.com/billing');
  }

  static Uri? checkoutUrl(LicenseOffer offer) {
    final value = offer == LicenseOffer.intro ? introCheckout : proCheckout;
    final url = Uri.tryParse(value);
    if (url == null || url.scheme != 'https' ||
        !(url.host == 'lemonsqueezy.com' ||
          url.host.endsWith('.lemonsqueezy.com'))) {
      return null;
    }
    return url;
  }
}

enum LicenseOffer { intro, pro }

class LicenseFailure implements Exception {
  final String message;
  const LicenseFailure(this.message);
  @override
  String toString() => message;
}

abstract class LicenseApi {
  Future<Map<String, dynamic>> activate(String key, String instanceName);
  Future<Map<String, dynamic>> validate(String key, {String? instanceId});
  Future<Map<String, dynamic>> deactivate(String key, String instanceId);
}

/// Public License API: unlike store-management APIs these operations use no API
/// secret. All requests are form-encoded over HTTPS.
class LemonSqueezyLicenseApi implements LicenseApi {
  static final Uri _origin = Uri.parse('https://api.lemonsqueezy.com');
  final Uri origin;
  LemonSqueezyLicenseApi({Uri? origin}) : origin = origin ?? _origin;

  Future<Map<String, dynamic>> _post(
      String method, Map<String, String> fields) async {
    final client = HttpClient()..connectionTimeout = const Duration(seconds: 10);
    try {
      final request = await client.postUrl(origin.resolve('/v1/licenses/$method'))
          .timeout(const Duration(seconds: 12));
      request.headers.set(HttpHeaders.acceptHeader, 'application/json');
      request.headers.contentType = ContentType('application', 'x-www-form-urlencoded');
      request.followRedirects = false;
      request.write(Uri(queryParameters: fields).query);
      final response = await request.close().timeout(const Duration(seconds: 12));
      final body = await utf8.decoder.bind(response).join()
          .timeout(const Duration(seconds: 12));
      if (response.statusCode == 429) {
        throw const LicenseFailure('License service rate limit. Try again shortly.');
      }
      if (response.statusCode >= 500) {
        throw const LicenseFailure('License service is temporarily unavailable.');
      }
      final decoded = jsonDecode(body);
      if (decoded is! Map<String, dynamic>) {
        throw const LicenseFailure('Unexpected license service response.');
      }
      if (response.statusCode >= 400) {
        final error = decoded['error'];
        throw LicenseFailure(error is String && error.isNotEmpty
            ? error : 'License request was rejected.');
      }
      return decoded;
    } on LicenseFailure {
      rethrow;
    } on SocketException {
      throw const LicenseFailure('Internet connection unavailable. Retry online.');
    } on HandshakeException {
      throw const LicenseFailure('Unable to verify the secure connection.');
    } on FormatException {
      throw const LicenseFailure('Malformed license service response.');
    } on Exception {
      throw const LicenseFailure('Could not reach the license service. Retry.');
    } finally {
      client.close(force: true);
    }
  }

  @override
  Future<Map<String, dynamic>> activate(String key, String instanceName) =>
      _post('activate', {'license_key': key, 'instance_name': instanceName});

  @override
  Future<Map<String, dynamic>> validate(String key, {String? instanceId}) =>
      _post('validate', {
        'license_key': key,
        if (instanceId != null) 'instance_id': instanceId,
      });

  @override
  Future<Map<String, dynamic>> deactivate(String key, String instanceId) =>
      _post('deactivate', {'license_key': key, 'instance_id': instanceId});
}

class StoredFortiqLicense {
  final String key;
  final String instanceId;
  const StoredFortiqLicense(this.key, this.instanceId);
}

abstract class LicenseStore {
  Future<StoredFortiqLicense?> read();
  Future<void> write(StoredFortiqLicense record);
  Future<void> clear();
}

/// Windows DPAPI (current Windows user) encrypts the data before it reaches
/// disk. There is intentionally no plaintext/shared_preferences fallback.
class WindowsDpapiLicenseStore implements LicenseStore {
  static const _channel = MethodChannel('fortiq/protected_storage');

  Future<File> _file() async {
    if (!Platform.isWindows) {
      throw const LicenseFailure('Secure license storage is not configured on this platform.');
    }
    final directory = await getApplicationSupportDirectory();
    if (!await directory.exists()) await directory.create(recursive: true);
    return File('${directory.path}${Platform.pathSeparator}license.dat');
  }

  @override
  Future<StoredFortiqLicense?> read() async {
    final file = await _file();
    if (!await file.exists()) return null;
    try {
      final bytes = await _channel.invokeMethod<Uint8List>(
          'unprotect', Uint8List.fromList(await file.readAsBytes()));
      if (bytes == null) throw const FormatException('Empty secret');
      final data = jsonDecode(utf8.decode(bytes)) as Map<String, dynamic>;
      final key = data['key'];
      final instanceId = data['instanceId'];
      if (key is! String || key.isEmpty || instanceId is! String ||
          instanceId.isEmpty) throw const FormatException('Invalid secret');
      return StoredFortiqLicense(key, instanceId);
    } catch (_) {
      throw const LicenseFailure('Saved license cannot be decrypted. Enter your license again.');
    }
  }

  @override
  Future<void> write(StoredFortiqLicense record) async {
    final file = await _file();
    try {
      final plaintext = Uint8List.fromList(utf8.encode(jsonEncode({
        'key': record.key, 'instanceId': record.instanceId,
      })));
      final sealed = await _channel.invokeMethod<Uint8List>('protect', plaintext);
      if (sealed == null || sealed.isEmpty) {
        throw const LicenseFailure('Could not protect the license.');
      }
      final temp = File('${file.path}.tmp');
      await temp.writeAsBytes(sealed, flush: true);
      if (await file.exists()) await file.delete();
      await temp.rename(file.path);
    } catch (_) {
      throw const LicenseFailure('Could not securely save the license.');
    }
  }

  @override
  Future<void> clear() async {
    final file = await _file();
    if (await file.exists()) await file.delete();
  }
}

class FortiqEntitlement {
  final LicenseOffer offer;
  final DateTime? expiresAt;
  final String instanceId;
  const FortiqEntitlement(this.offer, this.expiresAt, this.instanceId);

  static FortiqEntitlement verify(
      Map<String, dynamic> response, String expectedKey, String expectedInstance,
      {required String storeId, required String introVariantId,
       required String proVariantId, DateTime Function()? clock}) {
    final metadata = response['meta'];
    final license = response['license_key'];
    final instance = response['instance'];
    if (metadata is! Map || license is! Map || instance is! Map ||
        response['valid'] != true ||
        license['status'] != 'active' ||
        license['key']?.toString() != expectedKey ||
        instance['id']?.toString() != expectedInstance ||
        metadata['store_id']?.toString() != storeId) {
      throw const LicenseFailure('License is invalid or is not activated on this device.');
    }
    final variant = metadata['variant_id']?.toString();
    final offer = variant == introVariantId
        ? LicenseOffer.intro
        : (variant == proVariantId ? LicenseOffer.pro : null);
    if (offer == null) {
      throw const LicenseFailure('This license is not for FORTIQ.');
    }
    final rawExpiry = license['expires_at'];
    final expiry = rawExpiry is String ? DateTime.tryParse(rawExpiry)?.toUtc() : null;
    if (offer == LicenseOffer.intro && expiry == null) {
      throw const LicenseFailure('Intro license has no expiration date.');
    }
    if (expiry != null && !expiry.isAfter((clock ?? DateTime.now)().toUtc())) {
      throw const LicenseFailure('License has expired.');
    }
    return FortiqEntitlement(offer, expiry, expectedInstance);
  }
}

class FortiqLicenseService extends ChangeNotifier {
  final LicenseApi api;
  final LicenseStore store;
  final String storeId;
  final String introVariantId;
  final String proVariantId;
  final DateTime Function() clock;

  FortiqLicenseService({
    required this.api, required this.store,
    required this.storeId, required this.introVariantId,
    required this.proVariantId, DateTime Function()? clock,
  }) : clock = clock ?? DateTime.now;

  bool busy = false;
  bool initialized = false;
  String? error;
  FortiqEntitlement? entitlement;
  StoredFortiqLicense? _record;

  bool get isLicensed =>
      entitlement != null &&
      (entitlement!.expiresAt == null ||
       entitlement!.expiresAt!.isAfter(clock().toUtc()));
  bool get hasSavedKey => _record != null;

  void _setBusy(bool value) {
    busy = value;
    notifyListeners();
  }

  Future<void> load() async {
    _setBusy(true);
    try {
      _record = await store.read();
      if (_record != null) await _validateStored();
    } on LicenseFailure catch (e) {
      error = e.message;
      entitlement = null;
    } finally {
      initialized = true;
      _setBusy(false);
    }
  }

  Future<void> refresh() async {
    _setBusy(true);
    try {
      _record ??= await store.read();
      if (_record == null) throw const LicenseFailure('No activated license on this device.');
      await _validateStored();
    } on LicenseFailure catch (e) {
      entitlement = null;
      error = e.message;
    } finally {
      _setBusy(false);
    }
  }

  Future<void> _validateStored() async {
    final record = _record!;
    entitlement = null; // No unapproved offline grace.
    error = null;
    final result = await api.validate(record.key, instanceId: record.instanceId);
    entitlement = FortiqEntitlement.verify(
      result, record.key, record.instanceId,
      storeId: storeId, introVariantId: introVariantId,
      proVariantId: proVariantId, clock: clock,
    );
  }

  Future<void> activate(String input) async {
    final key = input.trim();
    if (key.isEmpty) {
      error = 'Enter the license key from your receipt.';
      notifyListeners();
      return;
    }
    _setBusy(true);
    try {
      if (_record?.key == key) {
        await _validateStored(); // Do not consume a second activation.
      } else {
        // Validate product ownership BEFORE consuming an activation slot.
        final preflight = await api.validate(key);
        final meta = preflight['meta'];
        final info = preflight['license_key'];
        if (meta is! Map || info is! Map ||
            preflight['valid'] != true ||
            meta['store_id']?.toString() != storeId ||
            (meta['variant_id']?.toString() != introVariantId &&
             meta['variant_id']?.toString() != proVariantId) ||
            !['active', 'inactive'].contains(info['status'])) {
          throw const LicenseFailure('Invalid FORTIQ license key.');
        }
        final expiry = info['expires_at'];
        if (expiry is String) {
          final parsed = DateTime.tryParse(expiry)?.toUtc();
          if (parsed == null || !parsed.isAfter(clock().toUtc())) {
            throw const LicenseFailure('License has expired.');
          }
        }
        final activated = await api.activate(key, 'FORTIQ Windows Desktop');
        final instance = activated['instance'];
        final instanceId = instance is Map ? instance['id']?.toString() : null;
        if (activated['activated'] != true ||
            instanceId == null || instanceId.isEmpty) {
          throw LicenseFailure(
            activated['error'] is String ? activated['error'] as String
                : 'Could not activate the key.',
          );
        }
        try {
          final result = await api.validate(key, instanceId: instanceId);
          final entitlement = FortiqEntitlement.verify(
            result, key, instanceId, storeId: storeId,
            introVariantId: introVariantId,
            proVariantId: proVariantId, clock: clock,
          );
          final record = StoredFortiqLicense(key, instanceId);
          await store.write(record);
          _record = record;
          this.entitlement = entitlement;
          error = null;
        } catch (_) {
          try { await api.deactivate(key, instanceId); } catch (_) {}
          rethrow;
        }
      }
    } on LicenseFailure catch (e) {
      error = e.message;
    } catch (_) {
      error = 'Activation failed. Check your connection and try again.';
    } finally {
      _setBusy(false);
    }
  }

  Future<void> deactivate() async {
    final record = _record;
    if (record == null) return;
    _setBusy(true);
    try {
      final answer = await api.deactivate(record.key, record.instanceId);
      if (answer['deactivated'] != true) {
        throw const LicenseFailure('Could not deactivate this device.');
      }
      await store.clear();
      _record = null;
      entitlement = null;
      error = null;
    } on LicenseFailure catch (e) {
      error = e.message;
    } finally {
      _setBusy(false);
    }
  }
}
