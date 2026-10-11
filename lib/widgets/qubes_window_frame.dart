import 'dart:io';
import 'package:flutter/material.dart';
import 'package:window_manager/window_manager.dart';

class QubesWindowFrame extends StatelessWidget {
  final String title;
  final String hostLabel;
  final Color domainColor;
  final Widget child;
  final List<Widget>? actions;

  const QubesWindowFrame({
    super.key,
    required this.title,
    required this.hostLabel,
    this.domainColor = const Color(0xFF8B5CF6), // Default purple
    required this.child,
    this.actions,
  });

  @override
  Widget build(BuildContext context) {
    final isDesktop = Platform.isWindows || Platform.isLinux || Platform.isMacOS;

    return Container(
      decoration: BoxDecoration(
        color: const Color(0xFF13141B),
        // Qubes-style distinct colored window border
        border: Border.all(
          color: domainColor,
          width: isDesktop ? 2.5 : 1.0,
        ),
      ),
      child: Scaffold(
        backgroundColor: Colors.transparent,
        body: SafeArea(
          top: !isDesktop,
          bottom: !isDesktop,
          child: Column(
            children: [
              // Qubes-style Title Bar
              Container(
                height: 40,
                decoration: BoxDecoration(
                  color: const Color(0xFF1E1F29),
                  border: Border(
                    bottom: BorderSide(
                      color: domainColor.withValues(alpha: 0.5),
                      width: 1.5,
                    ),
                  ),
                ),
                child: Row(
                  children: [
                    // Drag region for Windows / Host Badge
                    Expanded(
                      child: GestureDetector(
                        behavior: HitTestBehavior.translucent,
                        onPanStart: (details) {
                          if (isDesktop) windowManager.startDragging();
                        },
                        onDoubleTap: () async {
                          if (isDesktop) {
                            if (await windowManager.isMaximized()) {
                              windowManager.unmaximize();
                            } else {
                              windowManager.maximize();
                            }
                          }
                        },
                        child: Padding(
                          padding: const EdgeInsets.symmetric(horizontal: 10),
                          child: Row(
                            children: [
                              // Qubes Domain Pill Badge
                              Container(
                                padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
                                decoration: BoxDecoration(
                                  color: domainColor.withValues(alpha: 0.2),
                                  borderRadius: BorderRadius.circular(4),
                                  border: Border.all(color: domainColor, width: 1),
                                ),
                                child: Row(
                                  mainAxisSize: MainAxisSize.min,
                                  children: [
                                    Container(
                                      width: 7,
                                      height: 7,
                                      decoration: BoxDecoration(
                                        color: domainColor,
                                        shape: BoxShape.circle,
                                        boxShadow: [
                                          BoxShadow(
                                            color: domainColor.withValues(alpha: 0.8),
                                            blurRadius: 4,
                                          ),
                                        ],
                                      ),
                                    ),
                                    const SizedBox(width: 6),
                                    Text(
                                      hostLabel.toUpperCase(),
                                      style: TextStyle(
                                        color: domainColor,
                                        fontSize: 11,
                                        fontWeight: FontWeight.w700,
                                        letterSpacing: 0.5,
                                      ),
                                    ),
                                  ],
                                ),
                              ),
                            const SizedBox(width: 10),
                            // Window Title
                            Expanded(
                              child: Text(
                                title,
                                style: const TextStyle(
                                  color: Color(0xFFE2E8F0),
                                  fontSize: 13,
                                  fontWeight: FontWeight.w600,
                                  overflow: TextOverflow.ellipsis,
                                ),
                              ),
                            ),
                          ],
                        ),
                      ),
                    ),
                  ),

                  // Custom actions if any
                  if (actions != null) ...actions!,

                  // Window control buttons for Desktop
                  if (isDesktop) ...[
                    _WindowBtn(
                      icon: Icons.remove,
                      onTap: () => windowManager.minimize(),
                    ),
                    _WindowBtn(
                      icon: Icons.crop_square,
                      onTap: () async {
                        if (await windowManager.isMaximized()) {
                          windowManager.unmaximize();
                        } else {
                          windowManager.maximize();
                        }
                      },
                    ),
                    _WindowBtn(
                      icon: Icons.close,
                      isClose: true,
                      onTap: () => windowManager.close(),
                    ),
                  ],
                ],
              ),
            ),

            // Window Content
            Expanded(child: child),
          ],
        ),
      ),
    ),
  );
}
}

class _WindowBtn extends StatelessWidget {
  final IconData icon;
  final VoidCallback onTap;
  final bool isClose;

  const _WindowBtn({
    required this.icon,
    required this.onTap,
    this.isClose = false,
  });

  @override
  Widget build(BuildContext context) {
    return InkWell(
      onTap: onTap,
      hoverColor: isClose ? Colors.red.withValues(alpha: 0.8) : Colors.white10,
      child: SizedBox(
        width: 44,
        height: 40,
        child: Icon(
          icon,
          size: 14,
          color: const Color(0xFF94A3B8),
        ),
      ),
    );
  }
}
