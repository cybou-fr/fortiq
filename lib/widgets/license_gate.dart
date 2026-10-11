import 'dart:io';

import 'package:flutter/material.dart';

import '../services/license_service.dart';
import 'qubes_window_frame.dart';

/// Licensing is intentionally independent of any SSH connection or workspace.
class FortiqLicenseGate extends StatefulWidget {
  final Widget child;
  const FortiqLicenseGate({super.key, required this.child});

  @override
  State<FortiqLicenseGate> createState() => _FortiqLicenseGateState();
}

class _FortiqLicenseGateState extends State<FortiqLicenseGate> {
  FortiqLicenseService? _license;

  @override
  void initState() {
    super.initState();
    if (FortiqLicenseConfig.requireLicense && FortiqLicenseConfig.configured) {
      _license = FortiqLicenseService(
        api: LemonSqueezyLicenseApi(),
        store: WindowsDpapiLicenseStore(),
        storeId: FortiqLicenseConfig.storeId,
        introVariantId: FortiqLicenseConfig.introVariantId,
        proVariantId: FortiqLicenseConfig.proVariantId,
      );
      _license!.load();
    }
  }

  @override
  void dispose() {
    _license?.dispose();
    super.dispose();
  }

  void _showLicenseDetails() {
    final license = _license;
    if (license == null) return;
    showDialog<void>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('FORTIQ license'),
        content: SizedBox(
          width: 420,
          child: _LicenseDetails(service: license),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.of(context).pop(),
            child: const Text('Close'),
          ),
        ],
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    if (!FortiqLicenseConfig.requireLicense) return widget.child;

    if (!FortiqLicenseConfig.configured) {
      return const QubesWindowFrame(
        title: 'FORTIQ — License configuration',
        hostLabel: 'FORTIQ',
        child: Center(
          child: Padding(
            padding: EdgeInsets.all(30),
            child: SelectableText(
              'License configuration is missing from this build. '
              'Set FORTIQ_LS_STORE_ID and the Intro/Pro variant IDs '
              'when building the paid release.',
              textAlign: TextAlign.center,
            ),
          ),
        ),
      );
    }

    final service = _license!;
    return AnimatedBuilder(
      animation: service,
      builder: (context, _) {
        if (service.isLicensed) {
          return Stack(
            children: [
              widget.child,
              Positioned(
                right: 16,
                bottom: 12,
                child: Material(
                  color: const Color(0xFF262534),
                  borderRadius: BorderRadius.circular(8),
                  child: IconButton(
                    tooltip: 'License and subscription',
                    onPressed: _showLicenseDetails,
                    icon: const Icon(Icons.vpn_key_outlined,
                        size: 19, color: Color(0xFFD5C9FF)),
                  ),
                ),
              ),
            ],
          );
        }

        return QubesWindowFrame(
          title: 'FORTIQ — Activate',
          hostLabel: 'FORTIQ',
          child: _LicenseActivation(service: service),
        );
      },
    );
  }
}

class _LicenseActivation extends StatefulWidget {
  final FortiqLicenseService service;
  const _LicenseActivation({required this.service});

  @override
  State<_LicenseActivation> createState() => _LicenseActivationState();
}

class _LicenseActivationState extends State<_LicenseActivation> {
  final _keyController = TextEditingController();
  String? _linkError;

  @override
  void dispose() {
    _keyController.dispose();
    super.dispose();
  }

  Future<void> _buy(LicenseOffer offer) async {
    final url = FortiqLicenseConfig.checkoutUrl(offer);
    if (url == null) {
      setState(() => _linkError =
          'Checkout is not configured yet. Contact FORTIQ support.');
      return;
    }
    try {
      await _openExternal(url);
      if (mounted) setState(() => _linkError = null);
    } catch (_) {
      if (mounted) setState(() => _linkError = 'Unable to open checkout in your browser.');
    }
  }

  @override
  Widget build(BuildContext context) {
    final service = widget.service;
    return Center(
      child: ConstrainedBox(
        constraints: const BoxConstraints(maxWidth: 700),
        child: SingleChildScrollView(
          padding: const EdgeInsets.all(26),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              const Icon(Icons.desktop_windows_outlined,
                  color: Color(0xFFAA8BFF), size: 46),
              const SizedBox(height: 12),
              const Text(
                'FORTIQ — Distributed Desktop Software',
                style: TextStyle(fontSize: 21, fontWeight: FontWeight.bold),
                textAlign: TextAlign.center,
              ),
              const SizedBox(height: 10),
              const Text('One workspace. Full functionality with both options.',
                  textAlign: TextAlign.center,
                  style: TextStyle(color: Colors.white70)),
              const SizedBox(height: 22),
              Row(
                children: [
                  Expanded(
                    child: _PurchaseCard(
                      title: 'Intro',
                      price: '2 €',
                      detail: '14 days · One-time payment',
                      note: 'No automatic renewal',
                      onBuy: () => _buy(LicenseOffer.intro),
                    ),
                  ),
                  const SizedBox(width: 12),
                  Expanded(
                    child: _PurchaseCard(
                      title: 'Pro',
                      price: '39.90 €',
                      detail: '12 months · Annual subscription',
                      note: 'Renews yearly until canceled',
                      onBuy: () => _buy(LicenseOffer.pro),
                    ),
                  ),
                ],
              ),
              const SizedBox(height: 22),
              const Text('Already purchased? Enter your license key:',
                  style: TextStyle(fontWeight: FontWeight.w600)),
              const SizedBox(height: 8),
              TextField(
                controller: _keyController,
                obscureText: true,
                autocorrect: false,
                enableSuggestions: false,
                decoration: const InputDecoration(
                  hintText: 'License key from your Lemon Squeezy receipt',
                  border: OutlineInputBorder(),
                  prefixIcon: Icon(Icons.key),
                ),
                onSubmitted: service.busy ? null : service.activate,
              ),
              const SizedBox(height: 10),
              FilledButton.icon(
                onPressed: service.busy
                    ? null
                    : () => service.activate(_keyController.text),
                icon: service.busy
                    ? const SizedBox(width: 16, height: 16,
                        child: CircularProgressIndicator(strokeWidth: 2))
                    : const Icon(Icons.verified_user_outlined),
                label: Text(service.busy ? 'Checking license…' : 'Activate FORTIQ'),
              ),
              const SizedBox(height: 6),
              TextButton.icon(
                onPressed: () => _openExternal(
                  Uri.parse('https://app.lemonsqueezy.com/my-orders'),
                ),
                icon: const Icon(Icons.receipt_long_outlined),
                label: const Text('Find my order and license key'),
              ),
              if (service.hasSavedKey) ...[
                const SizedBox(height: 8),
                OutlinedButton(
                  onPressed: service.busy ? null : service.refresh,
                  child: const Text('Retry saved license'),
                ),
              ],
              if (service.error != null || _linkError != null) ...[
                const SizedBox(height: 12),
                Text(_linkError ?? service.error!,
                    style: const TextStyle(color: Colors.orangeAccent),
                    textAlign: TextAlign.center),
              ],
              if (!service.initialized) ...[
                const SizedBox(height: 12),
                const Text('Checking saved license…', textAlign: TextAlign.center),
              ],
              const SizedBox(height: 12),
              const Text(
                'After checkout, the license key is delivered by Lemon Squeezy. '
                'Intro never converts automatically to Pro. '
                'Prices include applicable taxes; final terms are shown at checkout.',
                textAlign: TextAlign.center,
                style: TextStyle(color: Colors.white54, fontSize: 12),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class _PurchaseCard extends StatelessWidget {
  final String title;
  final String price;
  final String detail;
  final String note;
  final VoidCallback onBuy;

  const _PurchaseCard({
    required this.title, required this.price, required this.detail,
    required this.note, required this.onBuy,
  });

  @override
  Widget build(BuildContext context) => Container(
    padding: const EdgeInsets.all(16),
    decoration: BoxDecoration(
      color: const Color(0xFF222330),
      borderRadius: BorderRadius.circular(12),
      border: Border.all(color: const Color(0xFF5D5477)),
    ),
    child: Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(title, style: const TextStyle(fontSize: 18, fontWeight: FontWeight.bold)),
        const SizedBox(height: 6),
        Text(price, style: const TextStyle(fontSize: 29, fontWeight: FontWeight.w800)),
        const SizedBox(height: 6),
        Text(detail, style: const TextStyle(fontSize: 12)),
        const SizedBox(height: 4),
        Text(note, style: const TextStyle(color: Colors.white60, fontSize: 11)),
        const SizedBox(height: 16),
        OutlinedButton(onPressed: onBuy, child: Text('Buy $title')),
      ],
    ),
  );
}

class _LicenseDetails extends StatelessWidget {
  final FortiqLicenseService service;
  const _LicenseDetails({required this.service});

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: service,
      builder: (context, _) {
        final entitlement = service.entitlement;
        final date = entitlement?.expiresAt?.toLocal();
        return Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text('Plan: ${entitlement?.offer.name.toUpperCase() ?? "Unverified"}'),
            const SizedBox(height: 8),
            Text(date == null ? 'Expiry: managed by subscription'
                : 'Expires: ${date.year}-${date.month.toString().padLeft(2, "0")}-${date.day.toString().padLeft(2, "0")}'),
            if (service.error != null) ...[
              const SizedBox(height: 8),
              Text(service.error!,
                  style: const TextStyle(color: Colors.orangeAccent)),
            ],
            const SizedBox(height: 16),
            Wrap(
              spacing: 8,
              children: [
                OutlinedButton(
                  onPressed: service.busy ? null : service.refresh,
                  child: const Text('Recheck'),
                ),
                OutlinedButton(
                  onPressed: service.busy || FortiqLicenseConfig.portalUrl == null
                      ? null
                      : () => _openExternal(FortiqLicenseConfig.portalUrl!),
                  child: const Text('Manage subscription'),
                ),
                TextButton(
                  onPressed: service.busy ? null : () async {
                    final accepted = await showDialog<bool>(
                      context: context,
                      builder: (ctx) => AlertDialog(
                        title: const Text('Deactivate FORTIQ?'),
                        content: const Text(
                          'Save your open work first. Deactivation '
                          'disconnects this device when the license screen returns.',
                        ),
                        actions: [
                          TextButton(
                            onPressed: () => Navigator.pop(ctx, false),
                            child: const Text('Keep license'),
                          ),
                          FilledButton(
                            onPressed: () => Navigator.pop(ctx, true),
                            child: const Text('Deactivate'),
                          ),
                        ],
                      ),
                    );
                    if (accepted == true) await service.deactivate();
                    if (context.mounted && !service.isLicensed) {
                      Navigator.of(context).pop();
                    }
                  },
                  child: const Text('Deactivate device'),
                ),
              ],
            ),
          ],
        );
      },
    );
  }
}

Future<void> _openExternal(Uri url) async {
  // Never pass unverified, untrusted URL strings to the host shell.
  if (url.scheme != 'https') throw const LicenseFailure('Unsupported link.');
  if (Platform.isWindows) {
    await Process.start('rundll32.exe',
        ['url.dll,FileProtocolHandler', url.toString()],
        mode: ProcessStartMode.detached);
  } else if (Platform.isMacOS) {
    await Process.start('open', [url.toString()],
        mode: ProcessStartMode.detached);
  } else if (Platform.isLinux) {
    await Process.start('xdg-open', [url.toString()],
        mode: ProcessStartMode.detached);
  } else {
    throw const LicenseFailure('External checkout is not configured on this platform.');
  }
}
