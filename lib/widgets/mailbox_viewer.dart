import 'package:flutter/material.dart';
import '../services/ssh_service.dart';

class RemoteMailboxViewer extends StatefulWidget {
  final SshClientService sshService;
  final Color domainColor;

  const RemoteMailboxViewer({
    super.key,
    required this.sshService,
    required this.domainColor,
  });

  @override
  State<RemoteMailboxViewer> createState() => _RemoteMailboxViewerState();
}

class _RemoteMailboxViewerState extends State<RemoteMailboxViewer> {

  // Async loads may finish after the window was closed.
  @override
  void setState(VoidCallback fn) {
    if (mounted) super.setState(fn);
  }
  bool _isLoading = true;
  List<Map<String, dynamic>> _messages = [];
  Map<String, dynamic>? _selectedMessage;
  String _selectedMailbox = 'debian'; // 'debian', 'root'
  bool _isComposing = false;

  // Compose controllers
  final TextEditingController _toController = TextEditingController(text: 'root');
  final TextEditingController _subjectController = TextEditingController();
  final TextEditingController _bodyController = TextEditingController();
  bool _isSending = false;

  @override
  void initState() {
    super.initState();
    _fetchMail();
  }

  @override
  void dispose() {
    _toController.dispose();
    _subjectController.dispose();
    _bodyController.dispose();
    super.dispose();
  }

  Future<void> _fetchMail() async {
    setState(() => _isLoading = true);
    final list = await widget.sshService.fetchSystemMail(mailboxUser: _selectedMailbox);
    if (mounted) {
      setState(() {
        _messages = list;
        _isLoading = false;
        if (_messages.isNotEmpty && _selectedMessage == null) {
          _selectedMessage = _messages.first;
        } else if (_messages.isEmpty) {
          _selectedMessage = null;
        }
      });
    }
  }

  Future<void> _sendMail() async {
    final to = _toController.text.trim();
    final subject = _subjectController.text.trim();
    final body = _bodyController.text.trim();

    if (to.isEmpty || subject.isEmpty || body.isEmpty) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('Please fill all fields (To, Subject, Body)')),
      );
      return;
    }

    setState(() => _isSending = true);
    final success = await widget.sshService.sendSystemMail(
      to: to,
      subject: subject,
      body: body,
    );

    if (mounted) {
      setState(() {
        _isSending = false;
        if (success) {
          _isComposing = false;
          _subjectController.clear();
          _bodyController.clear();
        }
      });

      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text(success ? 'System mail sent successfully!' : 'Failed to send mail'),
          backgroundColor: success ? Colors.green.shade700 : Colors.red.shade700,
        ),
      );

      if (success) {
        _fetchMail();
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    return Container(
      color: const Color(0xFF13141F),
      child: Column(
        children: [
          // Mail Toolbar
          _buildToolbar(),

          // Main View: 2 Panes (Message List + Message Body/Compose)
          Expanded(
            child: _isComposing ? _buildComposePane() : _buildSplitMailPane(),
          ),
        ],
      ),
    );
  }

  Widget _buildToolbar() {
    return Container(
      height: 44,
      padding: const EdgeInsets.symmetric(horizontal: 12),
      decoration: BoxDecoration(
        color: const Color(0xFF1B1C28),
        border: Border(
          bottom: BorderSide(
            color: widget.domainColor.withValues(alpha: 0.15),
          ),
        ),
      ),
      child: Row(
        children: [
          // Mailbox folder switcher
          Container(
            padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 2),
            decoration: BoxDecoration(
              color: const Color(0xFF222334),
              borderRadius: BorderRadius.circular(6),
            ),
            child: DropdownButtonHideUnderline(
              child: DropdownButton<String>(
                value: _selectedMailbox,
                dropdownColor: const Color(0xFF222334),
                style: const TextStyle(fontSize: 12, color: Colors.white, fontWeight: FontWeight.bold),
                items: const [
                  DropdownMenuItem(
                    value: 'debian',
                    child: Row(
                      children: [
                        Icon(Icons.inbox, size: 14, color: Colors.cyanAccent),
                        SizedBox(width: 6),
                        Text('User Mailbox (debian)'),
                      ],
                    ),
                  ),
                  DropdownMenuItem(
                    value: 'root',
                    child: Row(
                      children: [
                        Icon(Icons.shield, size: 14, color: Colors.redAccent),
                        SizedBox(width: 6),
                        Text('Root Mailbox (/var/mail/root)'),
                      ],
                    ),
                  ),
                ],
                onChanged: (val) {
                  if (val != null) {
                    setState(() {
                      _selectedMailbox = val;
                      _selectedMessage = null;
                      _isComposing = false;
                    });
                    _fetchMail();
                  }
                },
              ),
            ),
          ),

          const SizedBox(width: 10),

          // Compose button
          ElevatedButton.icon(
            icon: Icon(_isComposing ? Icons.list_alt : Icons.edit, size: 14),
            label: Text(_isComposing ? 'Inbox' : 'Compose', style: const TextStyle(fontSize: 11)),
            style: ElevatedButton.styleFrom(
              backgroundColor: _isComposing ? const Color(0xFF2E3048) : widget.domainColor,
              foregroundColor: Colors.white,
              padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
              minimumSize: const Size(0, 30),
              elevation: 0,
            ),
            onPressed: () {
              setState(() => _isComposing = !_isComposing);
            },
          ),

          const Spacer(),

          // Refresh button
          IconButton(
            icon: const Icon(Icons.refresh, size: 16, color: Colors.white70),
            tooltip: 'Check New Mail',
            splashRadius: 18,
            onPressed: _fetchMail,
          ),
        ],
      ),
    );
  }

  Widget _buildSplitMailPane() {
    if (_isLoading) {
      return Center(
        child: CircularProgressIndicator(
          color: widget.domainColor,
          strokeWidth: 2,
        ),
      );
    }

    if (_messages.isEmpty) {
      return Center(
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Icon(Icons.mark_email_read_outlined, size: 48, color: Colors.white.withValues(alpha: 0.2)),
            const SizedBox(height: 12),
            const Text(
              'Mailbox is clean. No system notifications or cron alerts.',
              style: TextStyle(color: Colors.white54, fontSize: 13),
            ),
            const SizedBox(height: 16),
            ElevatedButton.icon(
              icon: const Icon(Icons.outgoing_mail, size: 14),
              label: const Text('Send Test Mail', style: TextStyle(fontSize: 11)),
              style: ElevatedButton.styleFrom(
                backgroundColor: widget.domainColor,
                foregroundColor: Colors.white,
                padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 8),
              ),
              onPressed: () {
                setState(() => _isComposing = true);
              },
            ),
          ],
        ),
      );
    }

    return Row(
      children: [
        // Left Column: List of Messages (35%)
        SizedBox(
          width: 280,
          child: Container(
            decoration: BoxDecoration(
              color: const Color(0xFF161724),
              border: Border(
                right: BorderSide(
                  color: widget.domainColor.withValues(alpha: 0.1),
                ),
              ),
            ),
            child: ListView.separated(
              itemCount: _messages.length,
              separatorBuilder: (_, __) => Divider(
                height: 1,
                color: Colors.white.withValues(alpha: 0.04),
              ),
              itemBuilder: (context, idx) {
                final msg = _messages[idx];
                final isSelected = _selectedMessage == msg;
                final subject = msg['subject'] as String? ?? '(No Subject)';
                final from = msg['from'] as String? ?? 'Unknown';
                final date = msg['date'] as String? ?? '';
                final isAlert = subject.toLowerCase().contains('alert') || subject.toLowerCase().contains('fail') || subject.toLowerCase().contains('warn');

                return InkWell(
                  onTap: () {
                    setState(() => _selectedMessage = msg);
                  },
                  child: Container(
                    color: isSelected
                        ? widget.domainColor.withValues(alpha: 0.15)
                        : Colors.transparent,
                    padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Row(
                          children: [
                            Icon(
                              isAlert ? Icons.warning_amber_rounded : Icons.mail_outline,
                              size: 14,
                              color: isAlert ? Colors.orangeAccent : widget.domainColor,
                            ),
                            const SizedBox(width: 6),
                            Expanded(
                              child: Text(
                                from,
                                style: const TextStyle(
                                  color: Colors.white,
                                  fontSize: 11,
                                  fontWeight: FontWeight.bold,
                                ),
                                maxLines: 1,
                                overflow: TextOverflow.ellipsis,
                              ),
                            ),
                          ],
                        ),
                        const SizedBox(height: 4),
                        Text(
                          subject,
                          style: TextStyle(
                            color: isSelected ? Colors.white : Colors.white70,
                            fontSize: 11,
                            fontWeight: isAlert ? FontWeight.w600 : FontWeight.normal,
                          ),
                          maxLines: 2,
                          overflow: TextOverflow.ellipsis,
                        ),
                        if (date.isNotEmpty) ...[
                          const SizedBox(height: 4),
                          Text(
                            date,
                            style: const TextStyle(color: Colors.white38, fontSize: 9),
                            maxLines: 1,
                            overflow: TextOverflow.ellipsis,
                          ),
                        ],
                      ],
                    ),
                  ),
                );
              },
            ),
          ),
        ),

        // Right Column: Message Reader (65%)
        Expanded(
          child: _selectedMessage == null
              ? const Center(
                  child: Text('Select an email from list to preview', style: TextStyle(color: Colors.white30)),
                )
              : _buildMessageDetail(_selectedMessage!),
        ),
      ],
    );
  }

  Widget _buildMessageDetail(Map<String, dynamic> msg) {
    final from = msg['from'] as String? ?? '';
    final to = msg['to'] as String? ?? '';
    final subject = msg['subject'] as String? ?? '';
    final date = msg['date'] as String? ?? '';
    final body = msg['body'] as String? ?? '';

    return Container(
      color: const Color(0xFF13141F),
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          // Header
          Container(
            padding: const EdgeInsets.all(12),
            decoration: BoxDecoration(
              color: const Color(0xFF1B1C28),
              borderRadius: BorderRadius.circular(8),
              border: Border.all(color: Colors.white.withValues(alpha: 0.05)),
            ),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  subject,
                  style: const TextStyle(
                    fontSize: 15,
                    fontWeight: FontWeight.bold,
                    color: Colors.white,
                  ),
                ),
                const SizedBox(height: 8),
                Row(
                  children: [
                    const Text('From: ', style: TextStyle(fontSize: 11, color: Colors.white38)),
                    Text(from, style: const TextStyle(fontSize: 11, color: Colors.cyanAccent, fontWeight: FontWeight.bold)),
                    const SizedBox(width: 16),
                    if (to.isNotEmpty) ...[
                      const Text('To: ', style: TextStyle(fontSize: 11, color: Colors.white38)),
                      Text(to, style: const TextStyle(fontSize: 11, color: Colors.white70)),
                    ],
                    const Spacer(),
                    Text(date, style: const TextStyle(fontSize: 10, color: Colors.white38)),
                  ],
                ),
              ],
            ),
          ),

          const SizedBox(height: 12),

          // Email Body (formatted terminal-friendly / text)
          Expanded(
            child: Container(
              width: double.infinity,
              padding: const EdgeInsets.all(14),
              decoration: BoxDecoration(
                color: const Color(0xFF0F1017),
                borderRadius: BorderRadius.circular(8),
                border: Border.all(color: Colors.white.withValues(alpha: 0.05)),
              ),
              child: SelectionArea(
                child: SingleChildScrollView(
                  child: Text(
                    body.isEmpty ? '(Empty message body)' : body,
                    style: const TextStyle(
                      fontFamily: 'Consolas',
                      fontSize: 12,
                      height: 1.5,
                      color: Color(0xFFDCDFE4),
                    ),
                  ),
                ),
              ),
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildComposePane() {
    return Container(
      color: const Color(0xFF13141F),
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              const Icon(Icons.send_rounded, size: 16, color: Colors.cyanAccent),
              const SizedBox(width: 8),
              const Text(
                'Compose System Notification / Mail',
                style: TextStyle(fontSize: 13, fontWeight: FontWeight.bold, color: Colors.white),
              ),
              const Spacer(),
              ElevatedButton.icon(
                icon: _isSending
                    ? const SizedBox(width: 12, height: 12, child: CircularProgressIndicator(strokeWidth: 2, color: Colors.white))
                    : const Icon(Icons.send, size: 12),
                label: Text(_isSending ? 'Sending...' : 'Send Message', style: const TextStyle(fontSize: 11)),
                style: ElevatedButton.styleFrom(
                  backgroundColor: widget.domainColor,
                  foregroundColor: Colors.white,
                  padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 8),
                ),
                onPressed: _isSending ? null : _sendMail,
              ),
            ],
          ),
          const SizedBox(height: 12),
          // Recipient
          TextField(
            controller: _toController,
            style: const TextStyle(color: Colors.white, fontSize: 12),
            decoration: InputDecoration(
              labelText: 'Recipient (user or email, e.g. root, debian)',
              labelStyle: const TextStyle(color: Colors.white54, fontSize: 11),
              filled: true,
              fillColor: const Color(0xFF1B1C28),
              border: OutlineInputBorder(borderRadius: BorderRadius.circular(6)),
              isDense: true,
            ),
          ),
          const SizedBox(height: 10),
          // Subject
          TextField(
            controller: _subjectController,
            style: const TextStyle(color: Colors.white, fontSize: 12),
            decoration: InputDecoration(
              labelText: 'Subject',
              labelStyle: const TextStyle(color: Colors.white54, fontSize: 11),
              filled: true,
              fillColor: const Color(0xFF1B1C28),
              border: OutlineInputBorder(borderRadius: BorderRadius.circular(6)),
              isDense: true,
            ),
          ),
          const SizedBox(height: 10),
          // Body
          Expanded(
            child: TextField(
              controller: _bodyController,
              maxLines: null,
              expands: true,
              style: const TextStyle(color: Colors.white, fontFamily: 'Consolas', fontSize: 12),
              textAlignVertical: TextAlignVertical.top,
              decoration: InputDecoration(
                hintText: 'Enter mail content or script alert message...',
                hintStyle: const TextStyle(color: Colors.white30, fontSize: 12),
                filled: true,
                fillColor: const Color(0xFF1B1C28),
                border: OutlineInputBorder(borderRadius: BorderRadius.circular(6)),
              ),
            ),
          ),
        ],
      ),
    );
  }
}
