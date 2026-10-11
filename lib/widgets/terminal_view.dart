import 'dart:async';
import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:xterm/xterm.dart';
import 'package:dartssh2/dartssh2.dart';
import '../services/ssh_service.dart';
import 'ui_errors.dart';

class RemoteTerminalView extends StatefulWidget {
  final SshClientService sshService;
  final Color domainColor;

  /// tmux session behind this terminal. The same name always leads back to the
  /// same shell, also after a lost connection or an app restart.
  final String sessionName;

  const RemoteTerminalView({
    super.key,
    required this.sshService,
    required this.domainColor,
    this.sessionName = 'fortiq-1',
  });

  @override
  State<RemoteTerminalView> createState() => _RemoteTerminalViewState();
}

class _RemoteTerminalViewState extends State<RemoteTerminalView> {

  // Async loads may finish after the window was closed.
  @override
  void setState(VoidCallback fn) {
    if (mounted) super.setState(fn);
  }
  late final Terminal _terminal;
  late final TerminalController _terminalController;
  final FocusNode _terminalFocusNode = FocusNode();
  SSHSession? _session;
  StreamSubscription? _stdoutSub;
  StreamSubscription? _stderrSub;
  bool _ready = false;
  bool _attaching = false;
  bool _usingTmux = false;
  bool _tmuxMissing = false;
  bool _installingTmux = false;
  bool _sessionEnded = false; // the shell itself exited (e.g. `exit`)
  String? _error;

  @override
  void initState() {
    super.initState();
    _terminal = Terminal(
      maxLines: 10000,
    );
    _terminalController = TerminalController();

    // Input and resize always go to whichever session is current, so they keep
    // working after the terminal is re-attached on a new connection.
    _terminal.onOutput = (data) {
      _session?.stdin.add(utf8.encode(data));
    };
    _terminal.onResize = (width, height, pixelWidth, pixelHeight) {
      _session?.resizeTerminal(width, height, pixelWidth, pixelHeight);
    };

    widget.sshService.addListener(_onConnectionChanged);
    _attach();
  }

  void _onConnectionChanged() {
    if (!mounted) return;
    setState(() {}); // refresh the "reconnecting" overlay
    if (widget.sshService.isConnected && _session == null && !_sessionEnded) {
      _attach();
    }
  }

  /// Opens the shell: inside tmux when available, so that it survives a lost
  /// connection, otherwise a plain shell.
  Future<void> _attach() async {
    final service = widget.sshService;
    if (_attaching || !service.isConnected) return;
    _attaching = true;
    try {
      final tmux = await service.terminal.hasTmux();
      final cols = _terminal.viewWidth > 0 ? _terminal.viewWidth : 100;
      final rows = _terminal.viewHeight > 0 ? _terminal.viewHeight : 30;
      final session = tmux
          ? await service.terminal.attachTmux(widget.sessionName, ptyWidth: cols, ptyHeight: rows)
          : await service.terminal.startShell(ptyWidth: cols, ptyHeight: rows);
      if (!mounted) {
        // The window was closed while the shell was being opened.
        session.close();
        return;
      }
      await _stdoutSub?.cancel();
      await _stderrSub?.cancel();
      _session = session;

      // Streaming decoders: a multi-byte character (Cyrillic, emoji) may be
      // split across two packets, so each stream keeps its own decoder state.
      const decoder = Utf8Decoder(allowMalformed: true);
      _stdoutSub = session.stdout.cast<List<int>>().transform(decoder).listen(_terminal.write);
      _stderrSub = session.stderr.cast<List<int>>().transform(decoder).listen(_terminal.write);
      unawaited(session.done.then((_) => _onSessionDone(session)));

      setState(() {
        _ready = true;
        _usingTmux = tmux;
        _tmuxMissing = !tmux;
        _error = null;
      });
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (mounted) _terminalFocusNode.requestFocus();
      });
    } catch (e) {
      if (!mounted) return;
      if (!_ready) {
        setState(() => _error = e.toString());
      } else {
        // Re-attaching after a reconnect failed; try again shortly.
        Future.delayed(const Duration(seconds: 3), () {
          if (mounted && _session == null && !_sessionEnded) _attach();
        });
      }
    } finally {
      _attaching = false;
    }
  }

  Future<void> _onSessionDone(SSHSession session) async {
    if (!mounted || !identical(session, _session)) return;
    // A closing connection ends its channels a moment before the service
    // notices it; wait a little to tell "shell exited" from "connection lost".
    await Future.delayed(const Duration(milliseconds: 700));
    if (!mounted || !identical(session, _session)) return;
    _session = null;
    if (widget.sshService.isConnected) {
      _terminal.write('\r\n[session ended]\r\n');
      setState(() => _sessionEnded = true);
    }
    // Otherwise the connection was lost: _onConnectionChanged re-attaches once
    // the service has reconnected.
  }

  void _startNewSession() {
    setState(() => _sessionEnded = false);
    _attach();
  }

  Future<void> _installTmux() async {
    setState(() => _installingTmux = true);
    try {
      await widget.sshService.terminal.installTmux();
      // Restart the shell inside tmux.
      final old = _session;
      _session = null;
      old?.close();
      if (mounted) {
        _terminal.write('\r\n[restarting inside tmux]\r\n');
        setState(() => _tmuxMissing = false);
        await _attach();
      }
    } catch (e) {
      if (mounted) showActionError(context, 'Install tmux', e);
    } finally {
      if (mounted) setState(() => _installingTmux = false);
    }
  }

  /// Status layers around the terminal: tmux hint, reconnect overlay and the
  /// "session ended" prompt.
  Widget _withStatus(Widget terminal) {
    final connected = widget.sshService.isConnected;
    return Column(
      children: [
        if (_tmuxMissing && connected)
          Container(
            width: double.infinity,
            color: const Color(0xFF2A2415),
            padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 4),
            child: Row(
              children: [
                const Icon(Icons.info_outline, size: 14, color: Colors.amberAccent),
                const SizedBox(width: 8),
                const Expanded(
                  child: Text(
                    'tmux is not installed: this shell is lost if the connection drops.',
                    style: TextStyle(color: Colors.amberAccent, fontSize: 11),
                  ),
                ),
                TextButton(
                  onPressed: _installingTmux ? null : _installTmux,
                  child: Text(_installingTmux ? 'Installing…' : 'Install tmux'),
                ),
              ],
            ),
          ),
        Expanded(
          child: Stack(
            children: [
              terminal,
              if (!connected)
                Positioned.fill(
                  child: ColoredBox(
                    color: const Color(0xCC0C0D14),
                    child: Center(
                      child: Column(
                        mainAxisSize: MainAxisSize.min,
                        children: [
                          const SizedBox(
                            width: 22,
                            height: 22,
                            child: CircularProgressIndicator(strokeWidth: 2),
                          ),
                          const SizedBox(height: 12),
                          const Text(
                            'Connection lost, reconnecting…',
                            style: TextStyle(color: Colors.white, fontSize: 13),
                          ),
                          const SizedBox(height: 4),
                          Text(
                            _usingTmux
                                ? 'Your shell keeps running on the server and will be re-attached.'
                                : 'tmux is not installed: the running shell cannot be restored.',
                            style: const TextStyle(color: Colors.white54, fontSize: 11),
                          ),
                        ],
                      ),
                    ),
                  ),
                ),
              if (_sessionEnded && connected)
                Positioned(
                  right: 12,
                  bottom: 12,
                  child: ElevatedButton.icon(
                    onPressed: _startNewSession,
                    icon: const Icon(Icons.refresh, size: 16),
                    label: const Text('Start new session'),
                  ),
                ),
            ],
          ),
        ),
      ],
    );
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
    widget.sshService.removeListener(_onConnectionChanged);
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

    final terminal = Focus(
      focusNode: _terminalFocusNode,
      autofocus: true,
      onKeyEvent: _handleRawKey,
      child: GestureDetector(
        behavior: HitTestBehavior.opaque,
        onTap: () => _terminalFocusNode.requestFocus(),
        child: Container(
          color: const Color(0xFF0C0D14),
          child: Column(
            children: [
              // Mobile Virtual Accessory Keys Bar
              Container(
                height: 32,
                color: const Color(0xFF141520),
                padding: const EdgeInsets.symmetric(horizontal: 4),
                child: SingleChildScrollView(
                  scrollDirection: Axis.horizontal,
                  physics: const BouncingScrollPhysics(),
                  child: Row(
                    children: [
                      _buildVirtualKey('ESC', '\x1b'),
                      _buildVirtualKey('TAB', '\t'),
                      _buildVirtualKey('CTRL+C', '\x03'),
                      _buildVirtualKey('CTRL+D', '\x04'),
                      _buildVirtualKey('CTRL+Z', '\x1a'),
                      _buildVirtualKey('CTRL+L', '\x0c'),
                      _buildVirtualKey('▲', '\x1b[A'),
                      _buildVirtualKey('▼', '\x1b[B'),
                      _buildVirtualKey('◀', '\x1b[D'),
                      _buildVirtualKey('▶', '\x1b[C'),
                      _buildVirtualKey('HOME', '\x1b[H'),
                      _buildVirtualKey('END', '\x1b[F'),
                    ],
                  ),
                ),
              ),
              // Main Terminal View
              Expanded(
                child: Padding(
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
            ],
          ),
        ),
      ),
    );
    return _withStatus(terminal);
  }

  Widget _buildVirtualKey(String label, String sequence) {
    return Container(
      margin: const EdgeInsets.symmetric(horizontal: 2, vertical: 3),
      child: Material(
        color: const Color(0xFF222436),
        borderRadius: BorderRadius.circular(4),
        child: InkWell(
          borderRadius: BorderRadius.circular(4),
          onTap: () {
            _session?.stdin.add(utf8.encode(sequence));
            _terminalFocusNode.requestFocus();
          },
          child: Padding(
            padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
            child: Text(
              label,
              style: TextStyle(
                fontFamily: 'Consolas',
                fontSize: 10,
                fontWeight: FontWeight.bold,
                color: widget.domainColor,
              ),
            ),
          ),
        ),
      ),
    );
  }
}
