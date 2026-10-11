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
