import 'package:flutter/material.dart';

import '../services/himalaya_mail_service.dart';
import '../services/ssh_service.dart';

class RemoteMailWindow extends StatefulWidget {
  final SshClientService sshService;
  final Color domainColor;
  final ValueChanged<bool>? onDirtyChanged;
  final VoidCallback onOpenTerminal;

  const RemoteMailWindow({
    super.key,
    required this.sshService,
    required this.domainColor,
    required this.onOpenTerminal,
    this.onDirtyChanged,
  });

  @override
  State<RemoteMailWindow> createState() => _RemoteMailWindowState();
}

class _RemoteMailWindowState extends State<RemoteMailWindow> {
  late final HimalayaMailService _mail = HimalayaMailService(widget.sshService);
  late final String _source = _currentSource;
  String get _currentSource =>
      '${widget.sshService.currentUsername}@${widget.sshService.currentHost}:${widget.sshService.currentPort}';
  bool get _online =>
      widget.sshService.isConnected && _currentSource == _source;
  Object? _client;
  int _generation = 0;
  bool _busy = false;
  bool _sending = false;
  String? _error;
  String? _version;
  List<MailAccount> _accounts = [];
  List<String> _folders = [];
  List<MailEnvelope> _messages = [];
  String? _account, _folder;
  int _page = 1;
  MailEnvelope? _selected;
  String _body = '';
  MailDraft? _draft;
  String? _draftAccount;
  final _to = TextEditingController();
  final _subject = TextEditingController();
  final _content = TextEditingController();

  @override
  void initState() {
    super.initState();
    _client = widget.sshService.rawClient;
    widget.sshService.addListener(_connectionChanged);
    _initialize();
  }

  @override
  void dispose() {
    widget.sshService.removeListener(_connectionChanged);
    _generation++;
    _to.dispose();
    _subject.dispose();
    _content.dispose();
    super.dispose();
  }

  void _connectionChanged() {
    if (!mounted) return;
    if (_client != widget.sshService.rawClient || !_online) {
      _client = widget.sshService.rawClient;
      _generation++;
      setState(() {
        _busy = false;
        if (_sending) {
          _error = 'Sending was not confirmed. Check the Sent folder before retrying; your draft was kept.';
          _sending = false;
        }
      });
    } else {
      setState(() {});
    }
    // Refresh is explicit: reconnecting must not resend a draft or switch its
    // account. The existing message and draft remain readable while offline.
  }

  Future<void> _perform<T>(
    Future<T> Function() action,
    void Function(T) apply,
  ) async {
    if (_busy || !_online) return;
    final generation = ++_generation;
    setState(() {
      _busy = true;
      _error = null;
    });
    try {
      final value = await action();
      if (mounted && generation == _generation && _online) {
        setState(() => apply(value));
      }
    } catch (error) {
      if (mounted && generation == _generation) {
        setState(() => _error = error.toString());
      }
    } finally {
      if (mounted && generation == _generation) setState(() => _busy = false);
    }
  }

  Future<void> _initialize() => _perform(
    () async {
      final client = widget.sshService.rawClient;
      final version = await _mail.inspect();
      if (!_online || widget.sshService.rawClient != client) {
        throw StateError(
          'The SSH session changed while loading accounts. Refresh Mail.',
        );
      }
      final accounts = await _mail.accounts();
      return (version, accounts);
    },
    (result) {
      _version = result.$1;
      _accounts = result.$2;
      if (_accounts.isEmpty) {
        _error = 'No accounts configured on this server. Use Himalaya account configure in Terminal.';
        return;
      }
      if (!_accounts.any((a) => a.name == _account)) {
        _account =
            _accounts.where((a) => a.isDefault).firstOrNull?.name ??
            _accounts.first.name;
      }
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (mounted && _draft == null) _loadFolders();
      });
    },
  );

  Future<void> _loadFolders() async {
    final account = _account;
    if (account == null) return;
    await _perform(() => _mail.folders(account), (folders) {
      _folders = folders;
      _folder = folders.contains(_folder)
          ? _folder
          : folders.where((f) => f.toLowerCase() == 'inbox').firstOrNull ??
                folders.firstOrNull;
      _page = 1;
      _selected = null;
      _body = '';
      _messages = [];
      if (_folder == null) {
        _error = 'This account has no mail folders.';
        return;
      }
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (mounted) _refresh();
      });
    });
  }

  Future<void> _refresh({int? page}) async {
    final account = _account, folder = _folder;
    if (account == null || folder == null) {
      await _initialize();
      return;
    }
    final requestedPage = page ?? _page;
    await _perform(() => _mail.envelopes(account, folder, requestedPage), (
      messages,
    ) {
      _messages = messages;
      _page = requestedPage;
      if (!messages.any((m) => m.id == _selected?.id)) {
        _selected = null;
        _body = '';
      }
    });
  }

  Future<void> _read(MailEnvelope message) =>
      _perform(() => _mail.read(_account!, _folder!, message.id), (body) {
        _selected = message;
        _body = body;
      });

  Future<void> _compose({bool reply = false}) async {
    final account = _account;
    if (account == null) return;
    await _perform(
      () => _mail
          .template(
            account,
            folder: reply ? _folder : null,
            replyTo: reply ? _selected?.id : null,
          )
          .then(MailDraft.fromTemplate),
      (draft) {
        _draft = draft;
        _draftAccount = account;
        _to.text = draft.to;
        _subject.text = draft.subject;
        _content.text = draft.body;
        widget.onDirtyChanged?.call(true);
      },
    );
  }

  Future<void> _send() async {
    final draft = _draft;
    if (draft == null || _draftAccount == null) return;
    draft.to = _to.text.trim();
    draft.subject = _subject.text;
    draft.body = _content.text;
    if (draft.to.isEmpty) {
      setState(() => _error = 'Enter a recipient.');
      return;
    }
    final generation = _generation + 1;
    await _perform(
      () {
        _sending = true;
        return _mail.send(_draftAccount!, draft);
      },
      (_) {
        _draft = null;
        _draftAccount = null;
        _to.clear();
        _subject.clear();
        _content.clear();
        widget.onDirtyChanged?.call(false);
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(
            content: Text(
              'Message sent. A copy was saved by the server mail client.',
            ),
          ),
        );
      },
    );
    if (mounted && _generation == generation) _sending = false;
  }

  Future<void> _discard() async {
    final discard = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('Discard this draft?'),
        content: const Text('The message has not been sent.'),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context, false),
            child: const Text('Keep editing'),
          ),
          TextButton(
            onPressed: () => Navigator.pop(context, true),
            child: const Text('Discard'),
          ),
        ],
      ),
    );
    if (discard == true && mounted) {
      setState(() {
        _draft = null;
        _draftAccount = null;
      });
      widget.onDirtyChanged?.call(false);
    }
  }

  Future<void> _move() async {
    final message = _selected;
    if (message == null) return;
    final target = await showDialog<String>(
      context: context,
      builder: (context) => SimpleDialog(
        title: const Text('Move to folder'),
        children: [
          for (final folder in _folders.where((f) => f != _folder))
            SimpleDialogOption(
              onPressed: () => Navigator.pop(context, folder),
              child: Text(folder),
            ),
        ],
      ),
    );
    if (!mounted || target == null || !_online) return;
    await _perform(() => _mail.move(_account!, _folder!, message.id, target), (
      _,
    ) {
      _messages.removeWhere((m) => m.id == message.id);
      _selected = null;
      _body = '';
    });
  }

  bool get _enabled => _online && !_busy;

  @override
  Widget build(BuildContext context) => Theme(
    data: ThemeData.dark().copyWith(
      colorScheme: ColorScheme.fromSeed(
        seedColor: widget.domainColor,
        brightness: Brightness.dark,
      ),
    ),
    child: Container(
      color: const Color(0xFF13141F),
      child: Column(
        children: [
          _toolbar(),
          if (!_online)
            _notice(
              _currentSource == _source
                  ? 'SSH disconnected. Mail is unavailable; your draft remains in this window.'
                  : 'This window belongs to $_source. Reopen Mail for the current server.',
            ),
          if (_error != null) _notice(_error!, error: true),
          if (_busy)
            LinearProgressIndicator(minHeight: 2, color: widget.domainColor),
          Expanded(child: _draft != null ? _composer() : _mailbox()),
          Container(
            padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 5),
            child: Row(
              children: [
                Expanded(
                  child: Text(
                    '$_source · ${_version ?? 'Server mail'}',
                    style: const TextStyle(fontSize: 10, color: Colors.white54),
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                  ),
                ),
                if (_draft != null)
                  const Text(
                    'Unsent draft',
                    style: TextStyle(fontSize: 10, color: Colors.amber),
                  ),
              ],
            ),
          ),
        ],
      ),
    ),
  );

  Widget _notice(String text, {bool error = false}) => Container(
    width: double.infinity,
    padding: const EdgeInsets.all(10),
    color: error ? const Color(0xFF462B30) : const Color(0xFF393323),
    child: SelectableText(
      text,
      style: const TextStyle(color: Colors.white, fontSize: 12),
    ),
  );

  Widget _toolbar() => Padding(
    padding: const EdgeInsets.all(8),
    child: Wrap(
      spacing: 8,
      runSpacing: 4,
      crossAxisAlignment: WrapCrossAlignment.center,
      children: [
        SizedBox(
          width: 180,
          child: DropdownButton<String>(
            isExpanded: true,
            value: _account,
            hint: const Text('Server account'),
            items: _accounts
                .map(
                  (a) => DropdownMenuItem(
                    value: a.name,
                    child: Text(a.name, overflow: TextOverflow.ellipsis),
                  ),
                )
                .toList(),
            onChanged: _enabled && _draft == null
                ? (value) {
                    setState(() {
                      _account = value;
                      _folder = null;
                      _folders = [];
                      _messages = [];
                      _selected = null;
                      _body = '';
                      _page = 1;
                    });
                    _loadFolders();
                  }
                : null,
          ),
        ),
        _button(
          'Compose',
          Icons.edit_outlined,
          _enabled && _account != null && _draft == null
              ? () => _compose()
              : null,
        ),
        _button(
          'Refresh',
          Icons.refresh,
          _enabled
              ? () {
                  if (_version == null || _accounts.isEmpty) {
                    _initialize();
                  } else if (_folder == null) {
                    _loadFolders();
                  } else {
                    _refresh();
                  }
                }
              : null,
        ),
        _button('Terminal', Icons.terminal, widget.onOpenTerminal),
      ],
    ),
  );

  Widget _button(String label, IconData icon, VoidCallback? action) =>
      TextButton.icon(
        onPressed: action,
        icon: Icon(icon, size: 16),
        label: Text(label, style: const TextStyle(fontSize: 12)),
      );

  Widget _mailbox() {
    if (_accounts.isEmpty) {
      return Center(
        child: SingleChildScrollView(
          padding: const EdgeInsets.all(24),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              Icon(Icons.mail_outline, size: 48, color: widget.domainColor),
              const SizedBox(height: 16),
              const Text(
                'Mail through your Linux server',
                style: TextStyle(fontSize: 18),
              ),
              const SizedBox(height: 12),
              const Text(
                'Mail uses the existing Himalaya 1.x client on this server.\n'
                'Configure its accounts in the server terminal, then refresh here.\n'
                'Credentials and IMAP/SMTP connections stay on the server.',
                textAlign: TextAlign.center,
              ),
              const SizedBox(height: 12),
              const SelectableText('himalaya account configure <account-name>'),
              _button(
                'Open server terminal',
                Icons.terminal,
                widget.onOpenTerminal,
              ),
            ],
          ),
        ),
      );
    }
    return LayoutBuilder(
      builder: (context, constraints) {
        final compact = constraints.maxWidth < 650;
        return Column(
          children: [
            Padding(
              padding: const EdgeInsets.symmetric(horizontal: 12),
              child: Row(
                children: [
                  Expanded(
                    child: DropdownButton<String>(
                      isExpanded: true,
                      value: _folder,
                      hint: const Text('Folder'),
                      items: _folders
                          .map(
                            (f) => DropdownMenuItem(
                              value: f,
                              child: Text(f, overflow: TextOverflow.ellipsis),
                            ),
                          )
                          .toList(),
                      onChanged: _enabled
                          ? (value) {
                              setState(() {
                                _folder = value;
                                _page = 1;
                                _selected = null;
                                _body = '';
                                _messages = [];
                              });
                              _refresh();
                            }
                          : null,
                    ),
                  ),
                  IconButton(
                    tooltip: 'Previous page',
                    icon: const Icon(Icons.chevron_left),
                    onPressed: _enabled && _page > 1
                        ? () => _refresh(page: _page - 1)
                        : null,
                  ),
                  Text('$_page'),
                  IconButton(
                    tooltip: 'Next page',
                    icon: const Icon(Icons.chevron_right),
                    onPressed:
                        _enabled &&
                            _messages.length == HimalayaMailService.pageSize
                        ? () => _refresh(page: _page + 1)
                        : null,
                  ),
                ],
              ),
            ),
            Expanded(
              child: compact
                  ? (_selected == null
                        ? _messageList()
                        : _reader(compact: true))
                  : Row(
                      children: [
                        SizedBox(width: 260, child: _messageList()),
                        const VerticalDivider(width: 1),
                        Expanded(child: _reader()),
                      ],
                    ),
            ),
          ],
        );
      },
    );
  }

  Widget _messageList() => _messages.isEmpty
      ? Center(
          child: Text(
            _busy ? 'Loading messages…' : 'No messages',
            style: const TextStyle(color: Colors.white54),
          ),
        )
      : ListView.builder(
          itemCount: _messages.length,
          itemBuilder: (context, index) {
            final message = _messages[index];
            return ListTile(
              selected: _selected?.id == message.id,
              selectedTileColor: widget.domainColor.withValues(alpha: 0.15),
              dense: true,
              enabled: _enabled,
              leading: Icon(
                message.unread && _selected?.id != message.id
                    ? Icons.mark_email_unread_outlined
                    : Icons.mail_outline,
                size: 18,
              ),
              title: Text(
                message.subject.isEmpty ? '(No subject)' : message.subject,
                maxLines: 2,
                overflow: TextOverflow.ellipsis,
              ),
              subtitle: Text(
                '${message.from}\n${message.date}',
                maxLines: 2,
                overflow: TextOverflow.ellipsis,
              ),
              trailing: message.hasAttachment
                  ? const Icon(Icons.attach_file, size: 14)
                  : null,
              onTap: () => _read(message),
            );
          },
        );

  Widget _reader({bool compact = false}) {
    final message = _selected;
    if (message == null) {
      return const Center(
        child: Text(
          'Select a message',
          style: TextStyle(color: Colors.white54),
        ),
      );
    }
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Padding(
          padding: const EdgeInsets.all(12),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              if (compact)
                _button(
                  'Back',
                  Icons.arrow_back,
                  () => setState(() => _selected = null),
                ),
              SelectableText(
                message.subject,
                style: const TextStyle(
                  fontSize: 18,
                  fontWeight: FontWeight.w600,
                ),
              ),
              const SizedBox(height: 8),
              SelectableText(
                'From: ${message.from}\nTo: ${message.to}\n${message.date}',
                style: const TextStyle(color: Colors.white60, fontSize: 12),
              ),
              Wrap(
                spacing: 4,
                children: [
                  _button(
                    'Reply',
                    Icons.reply,
                    _enabled ? () => _compose(reply: true) : null,
                  ),
                  _button(
                    'Move',
                    Icons.drive_file_move_outline,
                    _enabled ? _move : null,
                  ),
                  _button(
                    'Unread',
                    Icons.mark_email_unread_outlined,
                    _enabled
                        ? () => _perform(
                            () => _mail.markUnread(
                              _account!,
                              _folder!,
                              message.id,
                            ),
                            (_) {
                              _selected = null;
                              _body = '';
                              WidgetsBinding.instance.addPostFrameCallback((_) {
                                if (mounted) _refresh();
                              });
                            },
                          )
                        : null,
                  ),
                  _button(
                    'Attachments to server',
                    Icons.download,
                    _enabled
                        ? () => _perform(
                            () => _mail.downloadAttachments(
                              _account!,
                              _folder!,
                              message.id,
                            ),
                            (result) {
                              ScaffoldMessenger.of(context).showSnackBar(
                                SnackBar(
                                  content: Text(
                                    '$result Files are in this account’s server downloads directory.',
                                  ),
                                ),
                              );
                            },
                          )
                        : null,
                  ),
                ],
              ),
            ],
          ),
        ),
        const Divider(height: 1),
        Expanded(
          child: SingleChildScrollView(
            padding: const EdgeInsets.all(16),
            child: SelectableText(
              _body,
              style: const TextStyle(fontSize: 13, height: 1.5),
            ),
          ),
        ),
      ],
    );
  }

  Widget _composer() => Padding(
    padding: const EdgeInsets.all(12),
    child: Column(
      children: [
        Row(
          children: [
            Expanded(
              child: Text(
                'From ${_draft!.headers['from'] ?? _draftAccount}',
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
              ),
            ),
            _button('Discard', Icons.close, !_busy ? _discard : null),
            _button('Send', Icons.send_outlined, _enabled ? _send : null),
          ],
        ),
        TextField(
          controller: _to,
          enabled: !_busy,
          decoration: const InputDecoration(labelText: 'To'),
        ),
        TextField(
          controller: _subject,
          enabled: !_busy,
          decoration: const InputDecoration(labelText: 'Subject'),
        ),
        const SizedBox(height: 12),
        Expanded(
          child: TextField(
            controller: _content,
            enabled: !_busy,
            expands: true,
            maxLines: null,
            textAlignVertical: TextAlignVertical.top,
            decoration: const InputDecoration(
              hintText: 'Write your message',
              border: OutlineInputBorder(),
            ),
          ),
        ),
      ],
    ),
  );
}
