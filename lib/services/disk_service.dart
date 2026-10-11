import 'dart:convert';

import 'package:path/path.dart' as p;

import 'shell.dart';

class BlockDevice {
  final String path,
      identity,
      type,
      filesystem,
      label,
      uuid,
      model,
      serial,
      transport;
  final int size, depth;
  final bool readOnly, removable;
  final List<BlockDevice> children;

  BlockDevice(Map<String, dynamic> row, {this.depth = 0})
    : path = row['name']?.toString() ?? '',
      identity = row['maj:min']?.toString() ?? '',
      type = row['type']?.toString() ?? '',
      filesystem = row['fstype']?.toString() ?? '',
      label = row['label']?.toString() ?? '',
      uuid = row['uuid']?.toString() ?? '',
      model = row['model']?.toString().trim() ?? '',
      serial = row['serial']?.toString().trim() ?? '',
      transport = row['tran']?.toString() ?? '',
      size = int.tryParse('${row['size']}') ?? 0,
      readOnly = row['ro'] == true || '${row['ro']}' == '1',
      removable = row['rm'] == true || '${row['rm']}' == '1',
      children = (row['children'] as List? ?? [])
          .map(
            (child) => BlockDevice(
              Map<String, dynamic>.from(child as Map),
              depth: depth + 1,
            ),
          )
          .toList();

  Iterable<BlockDevice> get descendants sync* {
    yield this;
    for (final child in children) {
      yield* child.descendants;
    }
  }

  bool get mountable =>
      filesystem.isNotEmpty &&
      !{
        'swap',
        'LVM2_member',
        'linux_raid_member',
        'crypto_LUKS',
      }.contains(filesystem);
}

class DiskMount {
  final String source, target, filesystem, options, identity;
  DiskMount(Map<String, dynamic> row)
    : source = row['source']?.toString() ?? '',
      target = row['target']?.toString() ?? '',
      filesystem = row['fstype']?.toString() ?? '',
      options = row['options']?.toString() ?? '',
      identity = row['maj:min']?.toString() ?? '';
  bool get protected => isProtectedMount(target);
}

bool isProtectedMount(String target) =>
    target == '/' ||
    [
      '/boot',
      '/usr',
      '/var',
      '/etc',
      '/dev',
      '/proc',
      '/sys',
      '/run',
      '/home',
      '/bin',
      '/sbin',
      '/lib',
      '/lib64',
    ].any((path) => target == path || target.startsWith('$path/'));

class DiskSnapshot {
  final List<BlockDevice> roots;
  final List<DiskMount> mounts;
  DiskSnapshot(this.roots, this.mounts);
  List<BlockDevice> get devices =>
      roots.expand((device) => device.descendants).toList();
  List<DiskMount> mountsFor(BlockDevice device) =>
      mounts.where((mount) => mount.identity == device.identity).toList();
}

class FilesystemUsage {
  final int total, used, available;
  final double percent;
  FilesystemUsage(this.total, this.used, this.available, this.percent);
}

class DiskService {
  final RemoteShell _ssh;
  DiskService(this._ssh);
  static const _marker = '\n===FORTIQ-DISK-MOUNTS===\n';

  Future<DiskSnapshot> inspect() async {
    final output = await _ssh.execute(
      'LC_ALL=C timeout 20s lsblk --json --bytes --paths '
      '--output NAME,MAJ:MIN,TYPE,SIZE,RO,RM,FSTYPE,LABEL,UUID,MOUNTPOINT,MODEL,SERIAL,TRAN && '
      'printf ${shQuote(_marker)} && '
      'LC_ALL=C timeout 20s findmnt --json --list --output SOURCE,TARGET,FSTYPE,OPTIONS,MAJ:MIN',
    );
    final parts = output.split(_marker);
    if (parts.length != 2) {
      throw const FormatException('Incomplete disk inventory from server.');
    }
    final blocks = jsonDecode(parts[0]) as Map;
    final mounts = jsonDecode(parts[1]) as Map;
    return DiskSnapshot(
      (blocks['blockdevices'] as List)
          .map((row) => BlockDevice(Map<String, dynamic>.from(row as Map)))
          .toList(),
      (mounts['filesystems'] as List)
          .map((row) => DiskMount(Map<String, dynamic>.from(row as Map)))
          .toList(),
    );
  }

  Future<FilesystemUsage> usage(DiskMount mount) async {
    final output = await _ssh.execute(
      'actual=\$(findmnt --noheadings --raw --mountpoint ${shQuote(mount.target)} --output MAJ:MIN) || exit 1; '
      '[ "\$actual" = ${shQuote(mount.identity)} ] || { echo "Mount changed; refresh Disks." >&2; exit 1; }; '
      'LC_ALL=C timeout 15s df --block-size=1 --output=size,used,avail,pcent -- ${shQuote(mount.target)}',
    );
    final rows = const LineSplitter()
        .convert(output)
        .where((line) => line.trim().isNotEmpty)
        .toList();
    if (rows.length != 2) {
      throw const FormatException('Unsupported filesystem usage response.');
    }
    final values = rows.last.trim().split(RegExp(r'\s+'));
    if (values.length != 4) {
      throw const FormatException('Invalid filesystem usage response.');
    }
    return FilesystemUsage(
      int.parse(values[0]),
      int.parse(values[1]),
      int.parse(values[2]),
      double.parse(values[3].replaceAll('%', '')),
    );
  }

  void _device(BlockDevice device) {
    if (!device.path.startsWith('/dev/') ||
        device.path.contains('\x00') ||
        !RegExp(r'^\d+:\d+$').hasMatch(device.identity)) {
      throw const FormatException('Invalid block device identity.');
    }
  }

  Future<void> mount(
    BlockDevice device,
    String target, {
    bool readOnly = true,
  }) async {
    _device(device);
    if (!device.mountable) {
      throw StateError('Select an existing filesystem to mount.');
    }
    target = p.posix.normalize(target);
    if ((!target.startsWith('/mnt/') && !target.startsWith('/media/')) ||
        target.contains('\x00')) {
      throw const FormatException(
        'Choose a mount directory inside /mnt or /media.',
      );
    }
    final script =
        '''
dev=${shQuote(device.path)}
target=${shQuote(target)}
expected=${shQuote(device.identity)}
[ -b "\$dev" ] || { echo "Device no longer exists." >&2; exit 1; }
actual=\$(lsblk --nodeps --noheadings --raw --output MAJ:MIN -- "\$dev") || exit 1
[ "\$actual" = "\$expected" ] || { echo "Device changed; refresh Disks." >&2; exit 1; }
${device.uuid.isEmpty ? '' : 'uuid=\$(lsblk --nodeps --noheadings --raw --output UUID -- "\$dev") || exit 1; [ "\$uuid" = ${shQuote(device.uuid)} ] || { echo "Filesystem changed; refresh Disks." >&2; exit 1; }'}
canonical=\$(readlink -m -- "\$target") || exit 1
case "\$canonical" in /mnt/*|/media/*) ;; *) echo "Mount path resolves outside /mnt or /media." >&2; exit 1 ;; esac
findmnt --noheadings --source "\$dev" >/dev/null 2>&1 && { echo "Device is already mounted; refresh Disks." >&2; exit 1; }
mkdir -p -- "\$canonical" || exit 1
findmnt --mountpoint "\$canonical" >/dev/null 2>&1 && { echo "Target is already a mountpoint." >&2; exit 1; }
entries=\$(find "\$canonical" -mindepth 1 -maxdepth 1 -print -quit) || exit 1
[ -z "\$entries" ] || { echo "Mount directory must be empty." >&2; exit 1; }
mount -o ${readOnly || device.readOnly ? 'ro' : 'rw'},nosuid,nodev -- "\$dev" "\$canonical"
''';
    await _ssh.execute(asRoot(script));
  }

  Future<void> unmount(BlockDevice device, DiskMount mount) async {
    _device(device);
    if (mount.protected ||
        !mount.target.startsWith('/') ||
        mount.target.contains('\x00')) {
      throw StateError('This system mount cannot be unmounted from Disks.');
    }
    final script =
        '''
target=${shQuote(mount.target)}
actual=\$(findmnt --noheadings --raw --mountpoint "\$target" --output MAJ:MIN) || exit 1
[ "\$actual" = ${shQuote(device.identity)} ] || { echo "Mount changed; refresh Disks." >&2; exit 1; }
count=\$(findmnt --noheadings --raw --submounts --mountpoint "\$target" --output TARGET | wc -l)
[ "\$count" -eq 1 ] || { echo "Unmount child mounts first." >&2; exit 1; }
umount -- "\$target"
''';
    await _ssh.execute(asRoot(script));
  }
}

String diskBytes(int bytes) {
  if (bytes < 0) return '—';
  const units = ['B', 'KiB', 'MiB', 'GiB', 'TiB', 'PiB'];
  var value = bytes.toDouble(), unit = 0;
  while (value >= 1024 && unit < units.length - 1) {
    value /= 1024;
    unit++;
  }
  return '${value.toStringAsFixed(unit == 0 ? 0 : 1)} ${units[unit]}';
}
