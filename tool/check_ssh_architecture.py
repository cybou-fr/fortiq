#!/usr/bin/env python3
"""FORTIQ: fail CI if the SSH-only Linux CLI architecture is silently bypassed.

This is a static guard, not a replacement for real SSH/SOCKS/Mail integration
tests on disposable Linux hosts. Exceptions here are only for the product's
Lemon Squeezy licensing control plane, never work-facing applications.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

ACTIVE_DOCS = (
    "README.md",
    "docs/PRODUCT_SCOPE.md",
    "docs/ARCHITECTURE.md",
    "docs/DEVELOPMENT_MATRIX.md",
    "docs/LICENSING.md",
    "docs/LICENSING_SETUP.md",
    "docs/website/README.md",
    "docs/AI_CLI.md",
)
CONTRACT = ROOT / "docs/SSH_FRONTEND_CONTRACT.md"

# This is intentionally conservative: a direct network client in a work-facing
# service requires design review even if somebody intends to tunnel it later.
FORBIDDEN_DART = (
    re.compile(r"""import\s+['"]package:(?:mailer|enough_mail|imap[^/]*|smtp[^/]*|pop3[^/]*|http|dio)/""", re.I),
    re.compile(r"\bHttpClient\s*\("),
    re.compile(r"\bSocket\.connect\s*\("),
    re.compile(r"\bWebSocket\.connect\s*\("),
    re.compile(r"\bInternetAddress\.lookup\s*\("),
    re.compile(r"\bSMTPClient\s*\("),
    re.compile(r"\bIMAPClient\s*\("),
)

# This is a control-plane-only file (Lemon Squeezy License API, not a data
# backend). Direct HTTPS for billing/entitlements is allowed here.
CONTROL_PLANE_EXCEPTIONS = {"lib/services/license_service.dart"}

RETIRED_DOCS = (
    "docs/website/PROJECT_README.md",
    "docs/website/DEVELOPMENT_MATRIX.md",
)

OBSOLETE_DIRECT_MAIL_REQUIREMENTS = (
    "Один IMAP/SMTP-аккаунт",
    "будущая IMAP/SMTP-почта должны работать без SSH",
    "почтовый аккаунт работает независимо от SSH",
    "локальная почта и документы в целевой модели не зависят от SSH",
    "независимые Local/Account-приложения",
    "локальную черновую копию",
    "самостоятельный локальный backend",
)

errors: list[str] = []


def expect(condition: bool, msg: str) -> None:
    if not condition:
        errors.append(msg)


expect(CONTRACT.is_file(), "Missing mandatory docs/SSH_FRONTEND_CONTRACT.md")
if CONTRACT.is_file():
    contract_text = CONTRACT.read_text(encoding="utf-8")
    for index in range(1, 15):
        app = f"APP-{index:02d}"
        # All fourteen apps must have one explicit CLI mapping, not just a
        # mention in a paragraph or as a dependency.
        expect(
            re.search(rf"^\| {re.escape(app)} \|", contract_text, re.M) is not None,
            f"Missing executable mapping for {app} in SSH frontend contract",
        )
    expect("Mailbox UI → MailService → SSH" in contract_text,
           "Missing Mail GUI → SSH → Linux CLI requirement")
    expect("Linux-сервер = backend" in contract_text,
           "Linux must remain the sole backend")
    expect("Все серверные настройки" in contract_text,
           "Server settings and installation must remain on Linux")
    expect("в оперативной памяти" in contract_text,
           "UI drafts must remain RAM-only, not a local work store")
    expect("шифрованием at rest" in contract_text,
           "Future Linux at-rest encryption requirement missing")
    expect("не планируются по умолчанию" in contract_text,
           "Custom server CLIs/transport must not be default work")
    expect("Lemon Squeezy" in contract_text,
           "Missing separate licensing-control-plane exception")

# AI remains a server-side, optional CLI feature.
# llama.cpp loads a verified GGUF on CPU only when cache has no fresh answer.
# FORTIQ must never expose an AI HTTP service or client-side inference.
ai_file = ROOT / "docs/AI_CLI.md"
if ai_file.is_file():
    ai_text = ai_file.read_text(encoding="utf-8")
    for required in (
        "llama.cpp",
        "GGUF",
        "`ai`",
        "CPU-only",
        "без запуска llama.cpp",
        "только после явного",
        "cache",
    ):
        expect(required in ai_text,
               f"AI_CLI.md: missing Linux-only llama.cpp/cache contract: {required}")
    expect("Ollama" not in ai_text,
           "AI_CLI.md: obsolete Ollama runtime must not return")
else:
    errors.append("Missing optional AI CLI contract")

for relative in ACTIVE_DOCS:
    file = ROOT / relative
    expect(file.is_file(), f"Missing active documentation: {relative}")
    if not file.exists():
        continue
    text = file.read_text(encoding="utf-8")
    expect("SSH_FRONTEND_CONTRACT" in text,
           f"{relative}: link to mandatory SSH/CLI contract missing")
    for phrase in OBSOLETE_DIRECT_MAIL_REQUIREMENTS:
        expect(phrase not in text,
               f"{relative}: obsolete direct/independent account requirement: {phrase}")

for relative in RETIRED_DOCS:
    file = ROOT / relative
    expect(file.is_file(), f"Missing archival pointer: {relative}")
    if file.is_file():
        text = file.read_text(encoding="utf-8")
        expect("SSH_FRONTEND_CONTRACT" in text and "вывед" in text,
               f"{relative}: old snapshot must be retired and linked to contract")

for file in (ROOT / "lib").rglob("*.dart"):
    relative = file.relative_to(ROOT).as_posix()
    if relative in CONTROL_PLANE_EXCEPTIONS:
        continue
    content = file.read_text(encoding="utf-8")
    for forbidden in FORBIDDEN_DART:
        if forbidden.search(content):
            errors.append(f"{relative}: possible direct remote network client ({forbidden.pattern})")

# A new business-data database in Flutter is a deviation from the UI-only
# contract: workspace state and authoritative drafts live on Linux.
# Existing bootstrap/preferences and encrypted license state are explicitly
# separate and remain permitted.
for file in (ROOT / "lib").rglob("*.dart"):
    relative = file.relative_to(ROOT).as_posix()
    content = file.read_text(encoding="utf-8")
    for package in ("sqflite", "drift", "isar", "realm", "hive"):
        if re.search(rf"import\s+['\"]package:{package}(?:/|['\"])", content):
            errors.append(f"{relative}: new local work database requires architecture review")

pubspec = (ROOT / "pubspec.yaml").read_text(encoding="utf-8")
for prefix in ("mailer:", "enough_mail:", "imap_client:", "smtp_client:", "pop3:"):
    expect(re.search(rf"^\s*{re.escape(prefix)}", pubspec, re.M) is None,
           f"pubspec.yaml: direct-mail client dependency {prefix} is forbidden")

if errors:
    print("SSH/CLI architecture contract: FAILED")
    for item in errors:
        print(" -", item)
    sys.exit(1)

print("SSH/CLI architecture contract: PASS")
print("All 14 APP mappings and Linux-only compute/storage/config guardrails present; no direct client protocols found.")
