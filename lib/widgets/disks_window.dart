import 'package:flutter/material.dart';

import '../services/disk_service.dart';
import '../services/ssh_service.dart';
import 'remote_snapshot.dart';
import 'ui_errors.dart';

class RemoteDisksWindow extends StatefulWidget {
  final SshClientService sshService;
  final Color domainColor;
  final ValueChanged<String> onOpenDirectory;
  const RemoteDisksWindow({
    super.key,
    required this.sshService,
    required this.domainColor,
    required this.onOpenDirectory,
  });
  @override
  State<RemoteDisksWindow> createState() => _RemoteDisksWindowState();
}

class _RemoteDisksWindowState extends State<RemoteDisksWindow> {
  late final _disks = DiskService(widget.sshService);
  late final _snapshot = RemoteSnapshotController(
    widget.sshService,
    load: _disks.inspect,
  );
  String? _selectedPath;
  String? _selectedTarget;
  String _filter = '';
  bool _filesystems = false, _busy = false;
  FilesystemUsage? _usage;
  String? _usageError;
  bool _usageLoading = false;
  int _usageGeneration = 0;
  DateTime? _lastSnapshot;

  bool get _enabled =>
      _snapshot.available &&
      !_snapshot.loading &&
      !_busy &&
      _snapshot.error == null;
  BlockDevice? get _selected =>
      _snapshot.data?.devices.where((d) => d.path == _selectedPath).firstOrNull;
  DiskMount? get _selectedMount => _snapshot.data?.mounts
      .where((m) => m.target == _selectedTarget)
      .firstOrNull;

  @override
  void initState() {
    super.initState();
    _snapshot.addListener(_changed);
    _snapshot.refresh();
  }

  @override
  void dispose() {
    _usageGeneration++;
    _snapshot.removeListener(_changed);
    _snapshot.dispose();
    super.dispose();
  }

  void _changed() {
    if (!mounted) return;
    final updated = _lastSnapshot != _snapshot.updatedAt;
    _lastSnapshot = _snapshot.updatedAt;
    if (updated) {
      if (_selectedPath != null && _selected == null) _selectedPath = null;
      if (_selectedTarget != null && _selectedMount == null) {
        _selectedTarget = null;
        _usageGeneration++;
        _usageLoading = false;
      }
    }
    if (!_snapshot.available) {
      _usageGeneration++;
      _usageLoading = false;
    }
    setState(() {});
    if (updated && _selectedTarget != null) _loadUsage(_selectedTarget!);
  }

  void _selectDevice(BlockDevice device) {
    final mounts = _snapshot.data!.mountsFor(device);
    setState(() {
      _selectedPath = device.path;
      _selectedTarget = mounts.firstOrNull?.target;
      _usage = null;
      _usageError = null;
      _usageLoading = false;
    });
    _usageGeneration++;
    if (_selectedTarget != null) _loadUsage(_selectedTarget!);
  }

  void _selectMount(DiskMount mount) {
    final device = _snapshot.data!.devices
        .where((d) => d.identity == mount.identity)
        .firstOrNull;
    setState(() {
      _selectedPath = device?.path;
      _selectedTarget = mount.target;
      _usage = null;
      _usageError = null;
    });
    _loadUsage(mount.target);
  }

  Future<void> _loadUsage(String target) async {
    if (!_snapshot.available) return;
    final mount = _snapshot.data?.mounts
        .where((m) => m.target == target)
        .firstOrNull;
    if (mount == null) return;
    final generation = ++_usageGeneration;
    final client = widget.sshService.rawClient;
    setState(() {
      _usageLoading = true;
      _usageError = null;
      _usage = null;
    });
    try {
      final usage = await _disks.usage(mount);
      if (mounted &&
          generation == _usageGeneration &&
          _snapshot.available &&
          client == widget.sshService.rawClient) {
        setState(() => _usage = usage);
      }
    } catch (error) {
      if (mounted && generation == _usageGeneration) {
        setState(() => _usageError = error.toString());
      }
    } finally {
      if (mounted && generation == _usageGeneration) {
        setState(() => _usageLoading = false);
      }
    }
  }

  Future<void> _mount(BlockDevice device) async {
    if (!_enabled) return;
    final client = widget.sshService.rawClient;
    final target = TextEditingController(
      text: '/mnt/${device.path.split('/').last}',
    );
    var readOnly = true;
    final result = await showDialog<(String, bool)>(
      context: context,
      builder: (context) => StatefulBuilder(
        builder: (context, setDialogState) => AlertDialog(
          title: Text('Mount ${device.path}'),
          content: SizedBox(
            width: 460,
            child: SingleChildScrollView(
              child: Column(
                mainAxisSize: MainAxisSize.min,
                children: [
                  Text('${device.filesystem} · ${diskBytes(device.size)}'),
                  const SizedBox(height: 12),
                  TextField(
                    controller: target,
                    decoration: const InputDecoration(
                      labelText: 'Empty directory under /mnt or /media',
                    ),
                  ),
                  CheckboxListTile(
                    contentPadding: EdgeInsets.zero,
                    value: readOnly || device.readOnly,
                    onChanged: device.readOnly
                        ? null
                        : (value) =>
                              setDialogState(() => readOnly = value ?? true),
                    title: const Text('Read only'),
                  ),
                  const Text(
                    'Creates the directory if needed. This mount lasts until it is unmounted or the server restarts.',
                    style: TextStyle(fontSize: 12),
                  ),
                ],
              ),
            ),
          ),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(context),
              child: const Text('Cancel'),
            ),
            TextButton(
              onPressed: () => Navigator.pop(context, (target.text, readOnly)),
              child: const Text('Mount'),
            ),
          ],
        ),
      ),
    );
    if (!mounted || result == null) return;
    if (!_snapshot.available || widget.sshService.rawClient != client) {
      showActionError(
        context,
        'Mount',
        'SSH session changed. Refresh Disks first.',
      );
      return;
    }
    await _action(
      'Mount',
      () => _disks.mount(device, result.$1, readOnly: result.$2),
    );
  }

  Future<void> _unmount(BlockDevice device, DiskMount mount) async {
    if (!_enabled || mount.protected) return;
    final client = widget.sshService.rawClient;
    final confirm = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        title: Text('Unmount ${mount.target}?'),
        content: Text(
          '${device.path}\nClose files and terminals using this volume first.',
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context, false),
            child: const Text('Cancel'),
          ),
          TextButton(
            onPressed: () => Navigator.pop(context, true),
            child: const Text('Unmount'),
          ),
        ],
      ),
    );
    if (!mounted || confirm != true) return;
    if (!_snapshot.available || widget.sshService.rawClient != client) {
      showActionError(
        context,
        'Unmount',
        'SSH session changed. Refresh Disks first.',
      );
      return;
    }
    await _action('Unmount', () => _disks.unmount(device, mount));
  }

  Future<void> _action(String name, Future<void> Function() action) async {
    if (_busy) return;
    setState(() => _busy = true);
    try {
      await action();
      if (mounted) {
        _snapshot.invalidate();
        await _snapshot.refresh();
      }
    } catch (error) {
      if (mounted) showActionError(context, name, error);
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  @override
  Widget build(BuildContext context) => Column(
    children: [
      Padding(
        padding: const EdgeInsets.all(8),
        child: Row(
          children: [
            IconButton(
              tooltip: 'Refresh disks',
              onPressed: _snapshot.available && !_snapshot.loading && !_busy
                  ? _snapshot.refresh
                  : null,
              icon: const Icon(Icons.refresh, size: 18, color: Colors.white70),
            ),
            Expanded(
              child: TextField(
                onChanged: (value) =>
                    setState(() => _filter = value.toLowerCase()),
                style: const TextStyle(color: Colors.white, fontSize: 12),
                decoration: const InputDecoration(
                  isDense: true,
                  hintText: 'Device, label or mount',
                  hintStyle: TextStyle(color: Colors.white38),
                ),
              ),
            ),
            PopupMenuButton<bool>(
              tooltip: 'View',
              icon: const Icon(
                Icons.view_list,
                size: 18,
                color: Colors.white70,
              ),
              onSelected: (value) => setState(() => _filesystems = value),
              itemBuilder: (_) => [
                CheckedPopupMenuItem(
                  value: false,
                  checked: !_filesystems,
                  child: const Text('Devices & partitions'),
                ),
                CheckedPopupMenuItem(
                  value: true,
                  checked: _filesystems,
                  child: const Text('Mounted filesystems'),
                ),
              ],
            ),
          ],
        ),
      ),
      RemoteSnapshotStatus(snapshot: _snapshot),
      Expanded(
        child: LayoutBuilder(
          builder: (context, constraints) {
            final compact = constraints.maxWidth < 700;
            final selected = _selectedPath != null || _selectedTarget != null;
            if (compact) {
              return selected
                  ? Column(
                      children: [
                        Align(
                          alignment: Alignment.centerLeft,
                          child: TextButton.icon(
                            onPressed: () => setState(() {
                              _selectedPath = null;
                              _selectedTarget = null;
                              _usageGeneration++;
                            }),
                            icon: const Icon(Icons.arrow_back, size: 16),
                            label: const Text('All disks'),
                          ),
                        ),
                        Expanded(child: _details()),
                      ],
                    )
                  : _list();
            }
            return Row(
              children: [
                SizedBox(width: 290, child: _list()),
                const VerticalDivider(width: 1),
                Expanded(child: _details()),
              ],
            );
          },
        ),
      ),
      if (_busy)
        LinearProgressIndicator(minHeight: 2, color: widget.domainColor),
    ],
  );

  Widget _list() {
    final snapshot = _snapshot.data;
    if (snapshot == null) {
      return const Center(
        child: Text(
          'Load disk inventory from the server',
          style: TextStyle(color: Colors.white54),
        ),
      );
    }
    if (_filesystems) {
      final mounts = snapshot.mounts
          .where(
            (m) => '${m.source} ${m.target} ${m.filesystem}'
                .toLowerCase()
                .contains(_filter),
          )
          .toList();
      return ListView.builder(
        itemCount: mounts.length,
        itemBuilder: (context, index) {
          final mount = mounts[index];
          return ListTile(
            dense: true,
            selected: _selectedTarget == mount.target,
            leading: const Icon(
              Icons.folder_outlined,
              size: 18,
              color: Colors.white54,
            ),
            title: Text(
              mount.target,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: const TextStyle(color: Colors.white, fontSize: 12),
            ),
            subtitle: Text(
              '${mount.source} · ${mount.filesystem}',
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: const TextStyle(color: Colors.white54, fontSize: 11),
            ),
            onTap: () => _selectMount(mount),
          );
        },
      );
    }
    final devices = snapshot.devices
        .where(
          (d) =>
              '${d.path} ${d.label} ${d.model} ${d.filesystem} ${snapshot.mountsFor(d).map((m) => m.target).join(' ')}'
                  .toLowerCase()
                  .contains(_filter),
        )
        .toList();
    if (devices.isEmpty) {
      return Center(
        child: Padding(
          padding: const EdgeInsets.all(16),
          child: Text(
            snapshot.devices.isEmpty
                ? 'No block devices exposed by this server. Switch to Mounted filesystems.'
                : 'No matching devices',
            style: const TextStyle(color: Colors.white54),
            textAlign: TextAlign.center,
          ),
        ),
      );
    }
    return ListView.builder(
      itemCount: devices.length,
      itemBuilder: (context, index) {
        final device = devices[index];
        return Padding(
          padding: EdgeInsets.only(left: (device.depth * 12.0).clamp(0, 48)),
          child: ListTile(
            dense: true,
            selected: _selectedPath == device.path,
            leading: Icon(
              device.type == 'disk' ? Icons.storage : Icons.disc_full_outlined,
              size: 18,
              color: widget.domainColor,
            ),
            title: Text(
              device.path,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: const TextStyle(color: Colors.white, fontSize: 12),
            ),
            subtitle: Text(
              '${diskBytes(device.size)} · ${device.type}${device.filesystem.isEmpty ? '' : ' · ${device.filesystem}'}',
              style: const TextStyle(color: Colors.white54, fontSize: 11),
            ),
            onTap: () => _selectDevice(device),
          ),
        );
      },
    );
  }

  Widget _details() {
    final device = _selected, mount = _selectedMount;
    if (device == null && mount == null) {
      return const Center(
        child: Text(
          'Select a disk, partition or filesystem',
          style: TextStyle(color: Colors.white54),
        ),
      );
    }
    final mounts = device == null
        ? [?mount]
        : _snapshot.data!.mountsFor(device);
    return SingleChildScrollView(
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          SelectableText(
            device?.model.isNotEmpty == true
                ? device!.model
                : device?.path ?? mount!.target,
            style: const TextStyle(
              color: Colors.white,
              fontSize: 20,
              fontWeight: FontWeight.w600,
            ),
          ),
          const SizedBox(height: 12),
          if (device != null) ...[
            _property('Device', device.path),
            _property('Capacity', diskBytes(device.size)),
            _property('Type', device.type),
            _property('Filesystem', device.filesystem),
            _property('Label', device.label),
            _property('UUID', device.uuid),
            _property('Serial', device.serial),
            _property('Transport', device.transport),
            _property('Access', device.readOnly ? 'Read only' : 'Read / write'),
            if (device.removable) _property('Media', 'Removable'),
            if (device.children.isNotEmpty) ...[
              const Divider(),
              const Text(
                'Partitions & volumes',
                style: TextStyle(color: Colors.white70),
              ),
              for (final child in device.children)
                ListTile(
                  dense: true,
                  title: Text(
                    child.path,
                    style: const TextStyle(color: Colors.white),
                  ),
                  subtitle: Text(
                    '${diskBytes(child.size)} · ${child.filesystem}',
                    style: const TextStyle(color: Colors.white54),
                  ),
                  onTap: () => _selectDevice(child),
                ),
            ],
            if (device.mountable && mounts.isEmpty)
              TextButton.icon(
                onPressed: _enabled ? () => _mount(device) : null,
                icon: const Icon(Icons.add_link, size: 16),
                label: const Text('Mount filesystem'),
              ),
          ],
          if (mounts.isNotEmpty) ...[
            const Divider(),
            const Text('Mounts', style: TextStyle(color: Colors.white70)),
            for (final current in mounts)
              Padding(
                padding: const EdgeInsets.symmetric(vertical: 8),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    _property('Directory', current.target),
                    _property('Source', current.source),
                    _property('Options', current.options),
                    Wrap(
                      spacing: 8,
                      children: [
                        TextButton.icon(
                          onPressed: _snapshot.available
                              ? () => widget.onOpenDirectory(current.target)
                              : null,
                          icon: const Icon(Icons.folder_open, size: 16),
                          label: const Text('Open files'),
                        ),
                        TextButton.icon(
                          onPressed: _snapshot.available
                              ? () {
                                  setState(
                                    () => _selectedTarget = current.target,
                                  );
                                  _loadUsage(current.target);
                                }
                              : null,
                          icon: const Icon(Icons.pie_chart_outline, size: 16),
                          label: const Text('Space usage'),
                        ),
                        if (device != null && !current.protected)
                          TextButton.icon(
                            onPressed: _enabled
                                ? () => _unmount(device, current)
                                : null,
                            icon: const Icon(Icons.link_off, size: 16),
                            label: const Text('Unmount'),
                          ),
                      ],
                    ),
                    if (current.protected)
                      const Text(
                        'System mount',
                        style: TextStyle(color: Colors.white38, fontSize: 11),
                      ),
                  ],
                ),
              ),
          ],
          if (_selectedTarget != null) ...[
            const Divider(),
            _property('Space on', _selectedTarget!),
            if (_usageLoading) const LinearProgressIndicator(minHeight: 2),
            if (_usageError != null)
              SelectableText(
                _usageError!,
                style: const TextStyle(color: Colors.amberAccent, fontSize: 12),
              ),
            if (_usage != null) ...[
              const SizedBox(height: 8),
              LinearProgressIndicator(
                value: (_usage!.percent / 100).clamp(0, 1),
                color: _usage!.percent >= 90
                    ? Colors.orangeAccent
                    : widget.domainColor,
                minHeight: 8,
              ),
              const SizedBox(height: 8),
              _property(
                'Used',
                '${diskBytes(_usage!.used)} (${_usage!.percent.toStringAsFixed(0)}%)',
              ),
              _property('Available', diskBytes(_usage!.available)),
              _property('Filesystem size', diskBytes(_usage!.total)),
            ],
          ],
        ],
      ),
    );
  }

  Widget _property(String label, String value) => Padding(
    padding: const EdgeInsets.symmetric(vertical: 3),
    child: SelectableText(
      '$label: ${value.isEmpty ? '—' : value}',
      style: const TextStyle(color: Colors.white70, fontSize: 12),
    ),
  );
}
