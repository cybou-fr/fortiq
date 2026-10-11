class LinuxProcess {
  final int pid;
  final String user;
  final double cpu;
  final double mem;
  final String command;

  LinuxProcess({
    required this.pid,
    required this.user,
    required this.cpu,
    required this.mem,
    required this.command,
  });
}

class LinuxSystemMetrics {
  final double cpuPercent;
  final int totalMemMb;
  final int usedMemMb;
  final String uptime;
  final String diskUsage;

  LinuxSystemMetrics({
    required this.cpuPercent,
    required this.totalMemMb,
    required this.usedMemMb,
    required this.uptime,
    required this.diskUsage,
  });

  double get memPercent => totalMemMb > 0 ? (usedMemMb / totalMemMb) * 100 : 0.0;
}

class LinuxService {
  final String unit;
  final String load;
  final String active;
  final String sub;
  final String description;

  LinuxService({
    required this.unit,
    required this.load,
    required this.active,
    required this.sub,
    required this.description,
  });

  bool get isRunning => active == 'active';
}

class LinuxFileInfo {
  final String permissions;
  final int links;
  final String owner;
  final String group;
  final int size;
  final DateTime modified;
  final String name;
  final bool isDirectory;
  final bool isLink;

  LinuxFileInfo({
    required this.permissions,
    required this.links,
    required this.owner,
    required this.group,
    required this.size,
    required this.modified,
    required this.name,
    required this.isDirectory,
    required this.isLink,
  });

  String get formattedSize {
    if (isDirectory) return '--';
    if (size < 1024) return '$size B';
    if (size < 1024 * 1024) return '${(size / 1024).toStringAsFixed(1)} KB';
    if (size < 1024 * 1024 * 1024) {
      return '${(size / (1024 * 1024)).toStringAsFixed(1)} MB';
    }
    return '${(size / (1024 * 1024 * 1024)).toStringAsFixed(1)} GB';
  }
}
