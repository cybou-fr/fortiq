import 'package:flutter_test/flutter_test.dart';
import 'package:fortiq/services/license_service.dart';

class MemoryStore implements LicenseStore {
  StoredFortiqLicense? value;
  @override
  Future<StoredFortiqLicense?> read() async => value;
  @override
  Future<void> write(StoredFortiqLicense record) async { value = record; }
  @override
  Future<void> clear() async { value = null; }
}

class FakeLemonSqueezy implements LicenseApi {
  String storeId = '12';
  String variantId = '31';
  String status = 'active';
  String? expiresAt = '2030-11-11T00:00:00Z';
  bool valid = true;
  int activationCalls = 0;
  int validationCalls = 0;
  int deactivationCalls = 0;
  String instance = 'device-instance';
  String key = 'my-key';
  bool networkFailure = false;

  Map<String, dynamic> _payload({String? instanceId}) => {
    'valid': valid,
    'license_key': {
      'key': key,
      'status': status,
      'expires_at': expiresAt,
    },
    'instance': instanceId == null ? null : {'id': instanceId},
    'meta': {
      'store_id': int.parse(storeId),
      'variant_id': int.parse(variantId),
    },
  };

  @override
  Future<Map<String, dynamic>> activate(String key, String instanceName) async {
    activationCalls++;
    if (networkFailure) throw const LicenseFailure('Offline');
    return {
      ..._payload(instanceId: instance),
      'activated': true,
    };
  }

  @override
  Future<Map<String, dynamic>> validate(String key, {String? instanceId}) async {
    validationCalls++;
    if (networkFailure) throw const LicenseFailure('Offline');
    return _payload(instanceId: instanceId);
  }

  @override
  Future<Map<String, dynamic>> deactivate(String key, String instanceId) async {
    deactivationCalls++;
    if (networkFailure) throw const LicenseFailure('Offline');
    return {'deactivated': true};
  }
}

FortiqLicenseService service(FakeLemonSqueezy api, MemoryStore store) =>
    FortiqLicenseService(
      api: api,
      store: store,
      storeId: '12',
      introVariantId: '31',
      proVariantId: '32',
      clock: () => DateTime.utc(2026, 10, 11),
    );

void main() {
  test('Intro activates once, validates instance, and persists record', () async {
    final api = FakeLemonSqueezy();
    final store = MemoryStore();
    final license = service(api, store);

    await license.activate('my-key');
    expect(license.isLicensed, isTrue);
    expect(license.entitlement!.offer, LicenseOffer.intro);
    expect(store.value!.instanceId, 'device-instance');
    expect(api.activationCalls, 1);

    await license.activate('my-key');
    expect(api.activationCalls, 1, reason: 'same key must not use another activation slot');
    await license.refresh();
    expect(license.isLicensed, isTrue);
  });

  test('wrong store and variant never consume an activation slot', () async {
    final api = FakeLemonSqueezy()..storeId = '987';
    final license = service(api, MemoryStore());
    await license.activate('my-key');
    expect(license.isLicensed, isFalse);
    expect(api.activationCalls, 0);

    api.storeId = '12';
    api.variantId = '999';
    await license.activate('my-key');
    expect(license.isLicensed, isFalse);
    expect(api.activationCalls, 0);
  });

  test('expired and disabled licenses are rejected', () async {
    final api = FakeLemonSqueezy()..expiresAt = '2025-01-01T00:00:00Z';
    final license = service(api, MemoryStore());
    await license.activate('my-key');
    expect(license.isLicensed, isFalse);
    expect(api.activationCalls, 0);

    api.expiresAt = '2030-11-11T00:00:00Z';
    api.status = 'disabled';
    await license.activate('my-key');
    expect(license.isLicensed, isFalse);
    expect(api.activationCalls, 0);
  });

  test('Pro subscription is accepted for its variant', () async {
    final api = FakeLemonSqueezy()
      ..variantId = '32'
      ..expiresAt = null;
    final license = service(api, MemoryStore());
    await license.activate('my-key');
    expect(license.isLicensed, isTrue);
    expect(license.entitlement!.offer, LicenseOffer.pro);
  });

  test('failed online verification does not grant offline access', () async {
    final api = FakeLemonSqueezy()..networkFailure = true;
    final store = MemoryStore()..value = const StoredFortiqLicense(
        'my-key', 'device-instance');
    final license = service(api, store);
    await license.load();
    expect(license.isLicensed, isFalse);
    expect(license.hasSavedKey, isTrue);
    expect(license.error, 'Offline');

    api.networkFailure = false;
    await license.refresh();
    expect(license.isLicensed, isTrue);
  });

  test('deactivate releases the instance and preserves unrelated data', () async {
    final api = FakeLemonSqueezy();
    final store = MemoryStore();
    final license = service(api, store);
    await license.activate('my-key');
    await license.deactivate();
    expect(api.deactivationCalls, 1);
    expect(license.isLicensed, isFalse);
    expect(store.value, isNull);
  });

  test('instance mismatch fails even with valid license key', () {
    final api = FakeLemonSqueezy();
    expect(
      () => FortiqEntitlement.verify(
        api._payload(instanceId: 'other-device'),
        'my-key',
        'this-device',
        storeId: '12',
        introVariantId: '31',
        proVariantId: '32',
        clock: () => DateTime.utc(2026, 10, 11),
      ),
      throwsA(isA<LicenseFailure>()),
    );
  });

  test('portal URL is absent until the actual store slug is configured', () {
    expect(FortiqLicenseConfig.portalUrl, isNull);
    expect(FortiqLicenseConfig.checkoutUrl(LicenseOffer.intro), isNull);
  });
}
