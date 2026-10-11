import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fortiq/services/server_setup_service.dart';
import 'package:fortiq/widgets/server_status.dart';
import 'fake_shell.dart';
import 'server_setup_test.dart' show server;

Widget app(ServerSetupService setup, {bool runOnOpen = true}) => MaterialApp(
      home: Builder(
        builder: (context) => Scaffold(
          body: Center(
            child: ElevatedButton(
              onPressed: () => showServerSetupDialog(context, setup, runOnOpen: runOnOpen),
              child: const Text('open'),
            ),
          ),
        ),
      ),
    );

void main() {
  testWidgets('the dialog does the setup in front of the user and keeps the log', (tester) async {
    final shell = FakeShell(server());
    final setup = ServerSetupService(shell, host: 'vps.example', port: 22);
    await tester.runAsync(setup.inspect);

    await tester.pumpWidget(app(setup));
    await tester.tap(find.text('open'));
    await tester.pumpAndSettle();

    expect(find.text('Server setup: vps.example'), findsOneWidget);
    for (final title in [
      'Required packages',
      'System updates',
      'Automatic security updates',
      'Brute-force protection',
      'SSH hardening',
      'Firewall',
    ]) {
      expect(find.text(title), findsOneWidget, reason: title);
    }
    expect(find.textContaining('Finished: 6 steps done'), findsOneWidget);
    // the log of what ran is on screen
    expect(find.textContaining('apt-get install', findRichText: true), findsWidgets);
    expect(find.text('Close'), findsOneWidget);
  });

  testWidgets('a step that cannot run says why, and a failure is shown in red text', (tester) async {
    final shell = FakeShell(server(), usedKeyAuth: false, loginResults: [false]);
    final setup = ServerSetupService(shell, host: 'vps.example', port: 22);
    await tester.runAsync(setup.inspect);

    await tester.pumpWidget(app(setup));
    await tester.tap(find.text('open'));
    await tester.pumpAndSettle();

    expect(find.textContaining('Connect with an SSH key first'), findsOneWidget);
    // firewall: the second login failed, so it was undone
    expect(find.textContaining('switched off again'), findsWidgets); // in the step and in the log
    expect(find.textContaining('Finished with problems'), findsOneWidget);
  });

  testWidgets('the indicator reports the state of the server', (tester) async {
    final shell = FakeShell(server());
    final setup = ServerSetupService(shell, host: 'vps.example', port: 22);
    await tester.runAsync(setup.inspect);

    await tester.pumpWidget(MaterialApp(home: Scaffold(body: ServerStatusChip(setup: setup))));
    expect(find.text('6 to do'), findsOneWidget);

    await tester.runAsync(setup.run);
    await tester.pump();
    expect(find.text('Server ready'), findsOneWidget);
  });
}
