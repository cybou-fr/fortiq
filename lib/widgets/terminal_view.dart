import 'dart:async';
import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:xterm/xterm.dart';
import 'package:dartssh2/dartssh2.dart';
import '../services/ssh_service.dart';

class RemoteTerminalView extends StatefulWidget {
  final SshClientService sshService;
  final Color domainColor;

  const RemoteTerminalView({
    super.key,
    required this.sshService,
    required this.domainColor,
  });

  @override
  State<RemoteTerminalView> createState() => _RemoteTerminalViewState();
}

class _RemoteTerminalViewState extends State<RemoteTerminalView> {
  late final Terminal _terminal;
  late final TerminalController _terminalController;
  final FocusNode _terminalFocusNode = FocusNode();
  SSHSession? _session;
  StreamSubscription? _stdoutSub;
  StreamSubscription? _stderrSub;
  bool _ready = false;
  String? _error;

  @override
  void initState() {
    super.initState();
    _terminal = Terminal(
      maxLines: 10000,
    );
    _terminalController = TerminalController();

    _initPtyShell();
  }

  Future<void> _initPtyShell() async {
    try {
      final session = await widget.sshService.startShell(
        ptyWidth: 100,
        ptyHeight: 30,
      );
      _session = session;

      // When xterm generates input, send it directly to SSH stdin
      _terminal.onOutput = (data) {
        final bytes = utf8.encode(data);
        _session?.stdin.add(bytes);
      };

      // Handle terminal resize events
      _terminal.onResize = (width, height, pixelWidth, pixelHeight) {
        _session?.resizeTerminal(width, height, pixelWidth, pixelHeight);
      };

      // Listen for remote stdout
      _stdoutSub = _session?.stdout.listen((bytes) {
        _terminal.write(utf8.decode(bytes, allowMalformed: true));
      });

      // Listen for remote stderr
      _stderrSub = _session?.stderr.listen((bytes) {
        _terminal.write(utf8.decode(bytes, allowMalformed: true));
      });

      if (mounted) {
        setState(() => _ready = true);
        WidgetsBinding.instance.addPostFrameCallback((_) {
          _terminalFocusNode.requestFocus();
        });
      }
    } catch (e) {
      if (mounted) {
        setState(() => _error = e.toString());
      }
    }
  }

  KeyEventResult _handleRawKey(FocusNode node, KeyEvent event) {
    if (event is! KeyDownEvent && event is! KeyRepeatEvent) {
      return KeyEventResult.ignored;
    }

    final key = event.logicalKey;
    String? toSend;

    if (key == LogicalKeyboardKey.enter || key == LogicalKeyboardKey.numpadEnter) {
      toSend = '\r';
    } else if (key == LogicalKeyboardKey.backspace) {
      toSend = '\x7f';
    } else if (key == LogicalKeyboardKey.tab) {
      toSend = '\t';
    } else if (key == LogicalKeyboardKey.arrowUp) {
      toSend = '\x1b[A';
    } else if (key == LogicalKeyboardKey.arrowDown) {
      toSend = '\x1b[B';
    } else if (key == LogicalKeyboardKey.arrowRight) {
      toSend = '\x1b[C';
    } else if (key == LogicalKeyboardKey.arrowLeft) {
      toSend = '\x1b[D';
    } else if (key == LogicalKeyboardKey.home) {
      toSend = '\x1b[H';
    } else if (key == LogicalKeyboardKey.end) {
      toSend = '\x1b[F';
    } else if (key == LogicalKeyboardKey.pageUp) {
      toSend = '\x1b[5~';
    } else if (key == LogicalKeyboardKey.pageDown) {
      toSend = '\x1b[6~';
    } else if (key == LogicalKeyboardKey.delete) {
      toSend = '\x1b[3~';
    } else if (HardwareKeyboard.instance.isControlPressed) {
      // Handle Ctrl+C, Ctrl+D, Ctrl+Z, Ctrl+L, etc.
      final char = event.character;
      if (char != null && char.isNotEmpty) {
        final code = char.toUpperCase().codeUnitAt(0) - 64;
        if (code >= 1 && code <= 26) {
          toSend = String.fromCharCode(code);
        }
      }
    } else if (event.character != null && event.character!.isNotEmpty) {
      toSend = event.character;
    }

    if (toSend != null) {
      _session?.stdin.add(utf8.encode(toSend));
      return KeyEventResult.handled;
    }

    return KeyEventResult.ignored;
  }

  @override
  void dispose() {
    _terminalFocusNode.dispose();
    _stdoutSub?.cancel();
    _stderrSub?.cancel();
    _session?.close();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    if (_error != null) {
      return Center(
        child: Padding(
          padding: const EdgeInsets.all(16.0),
          child: Text('Terminal PTY Error: $_error', style: const TextStyle(color: Colors.redAccent)),
        ),
      );
    }

    if (!_ready) {
      return Center(
        child: CircularProgressIndicator(color: widget.domainColor),
      );
    }

    return Focus(
      focusNode: _terminalFocusNode,
      autofocus: true,
      onKeyEvent: _handleRawKey,
      child: GestureDetector(
        behavior: HitTestBehavior.opaque,
        onTap: () => _terminalFocusNode.requestFocus(),
        child: Container(
          color: const Color(0xFF0C0D14),
          padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 4),
          child: TerminalView(
            _terminal,
            controller: _terminalController,
            autofocus: false,
            backgroundOpacity: 0.0,
            textStyle: const TerminalStyle(
              fontSize: 13,
              fontFamily: 'Consolas, monospace',
            ),
            theme: TerminalTheme(
              cursor: widget.domainColor,
              selection: widget.domainColor.withValues(alpha: 0.3),
              foreground: const Color(0xFFE2E8F0),
              background: const Color(0xFF0C0D14),
              black: const Color(0xFF1E1E2E),
              red: const Color(0xFFF38BA8),
              green: const Color(0xFFA6E3A1),
              yellow: const Color(0xFFF9E2AF),
              blue: const Color(0xFF89B4FA),
              magenta: const Color(0xFFCBA6F7),
              cyan: const Color(0xFF89DCEB),
              white: const Color(0xFFBAC2DE),
              brightBlack: const Color(0xFF585B70),
              brightRed: const Color(0xFFF38BA8),
              brightGreen: const Color(0xFFA6E3A1),
              brightYellow: const Color(0xFFF9E2AF),
              brightBlue: const Color(0xFF89B4FA),
              brightMagenta: const Color(0xFFCBA6F7),
              brightCyan: const Color(0xFF89DCEB),
              brightWhite: const Color(0xFFA6ADC8),
              searchHitBackground: const Color(0xFFF9E2AF),
              searchHitBackgroundCurrent: const Color(0xFFFAB387),
              searchHitForeground: const Color(0xFF11111B),
            ),
          ),
        ),
      ),
    );
  }
}
