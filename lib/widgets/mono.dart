/// Monospace font for terminals, editors and logs.
///
/// A font family is a single name, not a CSS list: "Consolas, monospace" never
/// matched anything. Consolas exists on Windows; the fallbacks cover macOS,
/// Android (Roboto Mono, the system "monospace") and Linux.
const String kMonoFont = 'Consolas';
const List<String> kMonoFontFallback = ['Menlo', 'Roboto Mono', 'monospace'];
