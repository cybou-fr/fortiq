# FORTIQ — Licensing and Lemon Squeezy Integration

**Product:** FORTIQ — Distributed Desktop Software.
**Decision:** Lemon Squeezy is the checkout, subscription and software-license provider.
**Status:** specification for R0; no billing or licensing integration is implemented in the currently audited application.

## 1. Exactly two offers

| | Intro | Pro |
|---|---|---|
| Payment | **€2 one-time** | **Annual recurring subscription** |
| Entitlement | **Seven consecutive days** | Paid annual subscription term |
| Renewal | **None** | Once a year, until canceled |
| Functionality | Complete FORTIQ | Complete FORTIQ |
| Availability | Intended: once per purchaser | Directly available or after Intro |
| Price | **€2** | **Annual amount not yet decided** |

Intro is a **one-time product**, not a paid or free introductory period attached to Pro. Purchasing Intro must never create or automatically renew a Pro subscription. Customer-facing pricing should state the final amount and renewal terms clearly, including applicable taxes.

The "one Intro per purchaser" rule is a **product requirement**, not an existing Lemon Squeezy guarantee. Built-in license keys track license/activation counts but do not automatically stop a customer buying a new Intro license in a second checkout. Decide on proportionate buyer identity/eligibility verification (and its cost) before calling this rule enforced.

## 2. Built-in Lemon Squeezy license capabilities

Lemon Squeezy can automatically generate a license key for each purchased software product/variant, deliver it in the email receipt and show it in My Orders. A one-time product supports a configurable **License length** and **Activation limit**; a subscription product keeps the key tied to its subscription lifecycle.

Configure the catalog:

- **FORTIQ Intro**: one-time **€2**, license keys enabled, **license length = seven days**, activation count to be decided.
- **FORTIQ Pro**: **yearly subscription**, license keys enabled, annual amount to be approved, activation count to be decided.
- Verify in test mode the exact expiration timestamp and timezone behavior. The key should expire after the first seven-day entitlement period, not after seven days of each activation.
- Record live/test store, product and variant identifiers separately. Only permit the configured FORTIQ products.

The License API supports `activate`, `validate` and `deactivate`. On successful activation, store the `instance.id` and validate both the license and that instance. Check `store_id`, `product_id` and/or `variant_id` on each response. License status can be `inactive`, `active`, `expired` or `disabled`; "inactive" can simply mean the key has not been activated on a device.

Lemon Squeezy's license key for a subscription expires when the subscription expires. A canceled annual subscription can remain usable until the end of its paid term. Do not treat the `cancelled` subscription status alone as immediate expiration.

Official docs: https://docs.lemonsqueezy.com/help/licensing/generating-license-keys
and https://docs.lemonsqueezy.com/help/licensing/license-keys-subscriptions

## 3. Minimum viable architecture

    FORTIQ Desktop
      ├── Purchase Intro / Pro → hosted Lemon Squeezy Checkout
      ├── Enter or restore license key
      ├── Activate / validate / deactivate via HTTPS License API
      ├── Local encrypted license-key storage + bounded validity cache
      └── Manage Pro → hosted Lemon Squeezy Customer Portal

Lemon Squeezy supplies product checkout, email receipt, license issuance, license activation/validation and the customer subscription portal. Reuse these first; do not build a large custom billing backend by default.

A **small server-side component is optional for ordinary licensed access**. It becomes necessary if strict one-per-purchaser Intro enforcement, central purchaser identity, signed offline entitlements or server-only API operations are implemented. If used, keep it narrow:
- Verify webhook HMAC signatures and reconcile provider changes;
- enforce a documented identity-based eligibility policy before allowing discounted Intro purchase;
- avoid exposing Lemon Squeezy administrative API or webhook secrets in the desktop;
- hold only the minimum identity, entitlement and event metadata needed.

**Do not claim duplicate Intro purchases are prevented** until the eligibility mechanism is implemented and tested. Changing account email or device may not be sufficient proof that a purchaser is different.

## 4. User experience and lifecycle

**Install → purchase:** launch desktop without requiring SSH; offer Intro and Pro with exact price and terms. Open Lemon Squeezy's hosted checkout. Failed, canceled or merely redirected checkout does not grant access.

**Activate:** after purchase, the buyer receives a license key from Lemon Squeezy. Allow paste/input, activation and validity check, then reveal the full workspace. Any optional automatic post-checkout activation must be verified against the provider, not a return URL alone.

**Intro:** one confirmed €2 payment grants seven days. Never automatically charge Pro. On expiry, offer a Pro purchase and preserve user settings/documents.

**Pro:** annual purchase and renewals maintain license validity. The "Manage subscription" action opens the hosted Customer Portal, which already supports cancellation and payment-method changes. Cancellation retains access through the valid term; handle provider `expired` and `disabled` states.

**Switch devices:** provide license-instance deactivation and reactivation within the approved activation policy.

**Connectivity:** avoid instantly disabling FORTIQ during a short network outage. A secure cache/offline grace is a release policy to decide and test, not a silently assumed 72-hour default. A customer-edited preferences file or changed system clock is not authoritative proof of payment.

**Data safety:** expiration, failed renewal and refunds must never delete local SSH profiles, keys, drafts, documents or workspace settings. Make account and license recovery clear.

## 5. R0 implementation tasks (LIC-01…LIC-07)

| ID | Work | Acceptance |
|---|---|---|
| LIC-01 | Activate Lemon Squeezy store, configure €2 Intro seven-day one-time license and annual Pro license product | Test-mode products and live products separately verified; Pro price approved before going live |
| LIC-02 | Desktop purchase, activation, restore-key and status UI | Buyer can activate without SSH; canceled checkout does not unlock app; correct expiry visible |
| LIC-03 | Integrate native License API with product/instance validation; only add minimal webhook/backend if the policy requires it | HTTP/error/rate limit handling; no store API secrets in Windows build |
| LIC-04 | Safe local license storage, bounded offline cache and validity refresh | Restart/outage/expiry/disabled and device migration covered; user data preserved |
| LIC-05 | One-time Intro purchaser eligibility | Define verifiable identity and implement an actual repeat-purchase gate; clearly flag unenforced state during development |
| LIC-06 | Pro self-service subscription management via Customer Portal | Annual charge shown; cancellation does not end paid access immediately |
| LIC-07 | Windows release acceptance, commercial/legal pages and purchase tests | Fresh-install checkout, receipts, refund flows, restore, error handling and accurate offer wording |

The annual Pro price, supported device count, buyer identity rules, offline grace period and distributor signing method remain **undecided**.

## 6. Verification checklist

1. Intro makes exactly one **€2** charge and its issued key expires after seven days; checkout does not create a subscription.
2. Intro → Pro requires a **separate authorized purchase**; no automatic upgrade or billing.
3. Pro renews annually; customer can cancel via Lemon Squeezy Portal and remains entitled until the applicable end date.
4. Expired and disabled license key cannot activate or validate as entitled; active key belongs to FORTIQ's configured store/product/variant.
5. Activation instances can be deactivated when migrating devices. Duplicated initialization does not waste activation slots.
6. A checkout redirect without a confirmed license never unlocks the desktop.
7. A second paid Intro checkout is blocked **only once** buyer eligibility enforcement has been built; test it separately rather than assuming native licensing provides it.
8. No private SSH keys are affected by license expiry; payment and license secrets are protected.
9. Test-mode purchase, license, subscription and Customer Portal flows are tested end-to-end; live identifiers are correctly replaced at release.
10. Subscription cancellation, renewal failure, refund and disabled/expired states produce a recoverable user-visible outcome.

## 7. Official documentation

- License keys: https://docs.lemonsqueezy.com/help/licensing/generating-license-keys
- License API: https://docs.lemonsqueezy.com/api/license-api
- Subscriptions and keys: https://docs.lemonsqueezy.com/help/licensing/license-keys-subscriptions
- Customer Portal: https://docs.lemonsqueezy.com/help/online-store/customer-portal
- Test mode: https://docs.lemonsqueezy.com/help/getting-started/test-mode
- Webhooks (if needed): https://docs.lemonsqueezy.com/help/webhooks

Confirm product approval and actual tax-inclusive checkout amounts in the live Lemon Squeezy store before publishing payment links.
