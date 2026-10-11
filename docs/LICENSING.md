# FORTIQ — Licensing and Paddle Integration

**Decision:** FORTIQ is paid Distributed Desktop Software. Paddle handles checkout and recurring billing.
**Status:** product contract / planned implementation. No licensing integration exists in the audited baseline `f0ca0954`.
**Public presentation:** describe what the desktop does; avoid defining the product by unrelated service categories.

## 1. Exactly two offers

| | Intro | Pro |
|---|---|---|
| Payment | **€1 once** | **Annual recurring subscription** |
| Entitlement | **7 × 24 hours** after verified purchase | Active paid annual term |
| Renewal | None | Yearly automatic renewal until cancelled |
| Access | All FORTIQ applications | All FORTIQ applications |
| Eligibility | Once per verified purchaser | Available directly or after Intro |
| Pricing | €1 | **Annual amount undecided** |

No feature gating between Intro and Pro. Intro must never turn into Pro or charge the customer again without a separate purchase and clear authorization. Intro is a one-time Paddle product/price, while Pro uses a yearly recurring price.

The actual buyer-facing amount, taxes, cancellation details and next renewal must be shown accurately in checkout before confirmation. Any change of annual price is a separate commercial decision.

## 2. User journeys

**Initial install:** app starts → activation window offers Intro and Pro → user chooses offer → opens approved Paddle checkout → backend verifies completed payment → client activates → launches workspace.

**Intro:** verified transaction grants seven consecutive days; license status displays exact expiration. One-time eligibility is enforced server-side against a stable purchaser identity. A failed payment or abandoned checkout grants no access.

**Intro → Pro:** user can purchase Pro at any time; a completed annual purchase supersedes Intro in the entitlement view. Pro never starts automatically.

**Pro:** verified active paid subscription grants access through its valid paid period. Yearly renewal updates the entitlement. Cancellation stops future renewal but keeps access for time already paid.

**Expired:** show clear renewal or restore-purchase actions; keep the local profiles, files, documents, drafts and saved state intact. Provide an appropriate way to retrieve user data.

**Refund, chargeback, disputed payment:** apply policy to entitlement after verifying canonical billing state and applicable law; provide a clear, recoverable state. Never treat unverified checkout redirects as payment evidence.

## 3. Systems and secrets

    Windows FORTIQ Desktop
      ↔ License API (activate, status, restore, checkout intent)
          ↔ Entitlement DB (customer, purchases, subscriptions, decisions)
          ← Verified Paddle webhooks
          ↔ Paddle API when reconciliation is needed

The backend holds all Paddle credentials and webhook signing secrets. The desktop holds only minimal identifiers and a server-signed entitlement or short-lived status token. The entitlement is tied to an authenticated customer identity without demanding a full account-management product at launch.

Do not use machine hardware serials, IP addresses or email string matching alone as anti-fraud authority. Select, document and privacy-review a proportionate customer verification mechanism and device policy before release.

## 4. Entitlement model

Suggested logical states: `unactivated`, `intro_active`, `pro_active`, `grace`, `expired`, `billing_attention`, `revoked`.

Recommended fields: `customerId`, `productId`, `offer`, `entitlementId`, `sourceTransactionId`/`subscriptionId`, `startsAt`, `paidThrough`, `status`, `issuedAt`, `expiresAt`, `tokenVersion`.

Separate Paddle's subscription state from effective app access. Use provider event ID, transaction ID and subscription ID to make all processing idempotent. Webhook processing verifies signature, stores the event, updates state transactionally and tolerates duplicates, delay and out-of-order events. A reconciliation job re-checks Paddle as source of truth.

Intro eligibility cannot be restored to unused simply by deleting the app, reinstalling Windows or changing an email alias.

## 5. Offline behavior

The application and its remote-resource operations should tolerate short Internet outages. Design a bounded offline entitlement cache validated by server signatures, with monotonic time protection where available and revalidation on reconnection.

**To decide before release:** exact offline grace duration, active device count and revocation behavior. Do not silently choose 72 hours or two devices; both were only earlier suggestions, not confirmed product decisions.

Do not delete or modify user data when a paid entitlement is no longer active.

## 6. Paddle integration steps (LIC-01…LIC-07)

| ID | Task | Completion criterion |
|---|---|---|
| LIC-01 | Configure verified Paddle seller/product and two prices | Intro is one-time €1; Pro is annual recurring with approved price; sandbox/production isolated |
| LIC-02 | Checkout + activation window in desktop | Buyer sees correct terms; canceled checkout grants no access; restore purchase works |
| LIC-03 | Minimal License API + webhook ingress | Signed verification, idempotent event handling, canonical reconciliation, secrets server-side |
| LIC-04 | Entitlement persistence + signed local cache | Restart, short outage, expiration and subscription cancel work predictably |
| LIC-05 | Intro once per verified purchaser | Reinstall/retry/duplicate webhook cannot grant a second intro |
| LIC-06 | Customer self-service subscription management | Pro cancel and renewal state visible; link to Paddle portal; no conversion from Intro |
| LIC-07 | Financial/compliance release checklist | Accurate receipts and billing UX, correct product descriptions, terms, privacy notices and validated payout setup |

For the €1 Intro, measure the payment and refund costs as part of the economics review; do not set a second low-value payment simply to work around an integration failure.

## 7. Release and support expectations

The application presents one consistent full-featured paid experience. Account/payment and product incidents require a minimal usable contact/recovery flow. Product diagnostics should favor reproducible error codes, clear self-help and privacy-protective logs.

Product site must communicate verified functionality, platform availability, license duration, annual renewal, cancellation and any permitted offline behavior.

Paddle provides the customer-facing merchant-of-record checkout and billing layer, while the software publisher remains responsible for delivering a functioning licensed product and for its own local business/tax obligations.

## 8. Acceptance tests

- One €1 Intro purchase activates seven days and produces no subsequent charges.
- One purchaser cannot repeat Intro after reinstalling the application.
- Pro purchased directly activates a full annual paid term; direct purchase from Intro works.
- Pro cancellation retains access to the paid-through date.
- Expired Intro offers only Pro; subscription payment failure follows a visible recoverable status.
- Duplicate and out-of-order Paddle webhooks do not double activate or shorten a valid paid term.
- Offline cache works strictly within the approved policy; changed client clock does not trivially extend access.
- A spoofed checkout redirect, replayed event or locally edited preferences file does not grant a license.
- License expiration or refund does not delete customer documents/settings.
- Installation, purchase and restore run when no SSH host is connected.
- No client release contains server-side Paddle secrets.

## 9. References

- Paddle Billing / subscriptions: https://developer.paddle.com/
- One-time digital purchases: https://developer.paddle.com/get-started/how-paddle-works/digital-products/
- FORTIQ architecture: [ARCHITECTURE.md](ARCHITECTURE.md)
- Development sequence: [DEVELOPMENT_MATRIX.md](DEVELOPMENT_MATRIX.md)

The exact SDK, API fields and webhook event contracts are implementation choices to validate against current official Paddle documentation and Sandbox before coding.
