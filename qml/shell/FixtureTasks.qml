import QtQuick

// Visual-only sample tasks for the reference scene (`--fixture-preview transfers`).
// Same interface as the TaskManager; never production data.
QtObject {
    readonly property var tasks: [
        { id: 1, kind: "upload", title: "rapport-2026.pdf", source: "demo@linux", destination: "/home/demo/Documents", state: "running", active: true, cancellable: true, decision: "", message: "", code: "", done: 3460000, total: 5200000 },
        { id: 2, kind: "download", title: "access.log", source: "demo@linux", destination: "C:/Users/vous/Downloads/access.log", state: "succeeded", active: false, cancellable: true, decision: "", message: "Enregistré : C:/Users/vous/Downloads/access.log", code: "", done: 812000, total: 812000 },
        { id: 3, kind: "upload", title: "Budget.xlsx", source: "demo@linux", destination: "/home/demo", state: "queued", active: true, cancellable: true, decision: "conflict", message: "« Budget.xlsx » existe déjà dans ce dossier.", code: "", done: 0, total: 48200 }
    ]
    function requestCancel(id) { return false }
    function clearFinished() {}
}
