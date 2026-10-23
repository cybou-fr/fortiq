import QtQuick

// Visual-only reference data for the Files window (`fixture` mode). Never production data.
Item {
    id: data
    visible: false
    property string fixturePreview: ""
    readonly property var sampleEntries: [
        { name: "Bureau", directory: true, path: "/home/demo/Bureau", mtime: 1760788800, size: 4096, mode: "drwxr-xr-x", owner: "demo" },
        { name: "Documents", directory: true, path: "/home/demo/Documents", mtime: 1760781600, size: 4096, mode: "drwxr-xr-x", owner: "demo" },
        { name: "Images", directory: true, path: "/home/demo/Images", mtime: 1760695200, size: 4096, mode: "drwxr-xr-x", owner: "demo" },
        { name: "Vidéos", directory: true, path: "/home/demo/Vidéos", mtime: 1760608800, size: 4096, mode: "drwxr-xr-x", owner: "demo" },
        { name: "Musique", directory: true, path: "/home/demo/Musique", mtime: 1760522400, size: 4096, mode: "drwxr-xr-x", owner: "demo" },
        { name: "Téléchargements", directory: true, path: "/home/demo/Téléchargements", mtime: 1760788000, size: 4096, mode: "drwxr-xr-x", owner: "demo" },
        { name: "Projets", directory: true, path: "/home/demo/Projets", mtime: 1760700000, size: 4096, mode: "drwxr-xr-x", owner: "demo" },
        { name: "Travail", directory: true, path: "/home/demo/Travail", mtime: 1760600000, size: 4096, mode: "drwxr-xr-x", owner: "demo" },
        { name: "Montagne.jpg", path: "/home/demo/Montagne.jpg", mtime: 1760780000, size: 2483200, mode: "-rw-r--r--", owner: "demo" },
        { name: "Présentation.pdf", path: "/home/demo/Présentation.pdf", mtime: 1760770000, size: 1205000, mode: "-rw-r--r--", owner: "demo" },
        { name: "Budget.xlsx", path: "/home/demo/Budget.xlsx", mtime: 1760760000, size: 48200, mode: "-rw-r--r--", owner: "demo" },
        { name: "Notes.txt", path: "/home/demo/Notes.txt", mtime: 1760750000, size: 1830, mode: "-rw-r--r--", owner: "demo" }
    ]
    readonly property var sampleTrash: [
        { name: "ancien-rapport.pdf", path: "/home/demo/.local/share/Trash/files/ancien-rapport.pdf", mtime: 1760700000, size: 820000, mode: "-rw-r--r--", owner: "demo" },
        { name: "Brouillons", directory: true, path: "/home/demo/.local/share/Trash/files/Brouillons", mtime: 1760600000, size: 4096, mode: "drwxr-xr-x", owner: "demo" },
        { name: "capture.png", path: "/home/demo/.local/share/Trash/files/capture.png", mtime: 1760500000, size: 240000, mode: "-rw-r--r--", owner: "demo" }
    ]
    readonly property var sampleTransfers: [
        { id: 1, upload: true, name: "rapport-2026.pdf", remote: "/home/demo/Documents", state: "running", done: 3460000, total: 5200000, message: "" },
        { id: 2, upload: false, name: "access.log", remote: "/var/log/nginx/access.log", state: "done", done: 812000, total: 812000, message: "Enregistré : C:/Users/vous/Downloads/access.log" },
        { id: 3, upload: true, name: "Budget.xlsx", remote: "/home/demo", state: "conflict", done: 0, total: 48200, message: "« Budget.xlsx » existe déjà dans ce dossier." }
    ]
    readonly property QtObject sampleQueue: queue
    readonly property QtObject sampleSearch: search
    readonly property QtObject samplePreview: preview
    QtObject {
        id: queue
        readonly property var transfers: data.sampleTransfers
        function cancelTransfer(id) {}
        function resolveConflict(id, policy) {}
        function clearFinishedTransfers() {}
    }
    QtObject {
        id: search
        readonly property bool searchActive: true
        readonly property bool searchBusy: false
        readonly property string searchQuery: "rapport"
        readonly property string searchMode: "content"
        readonly property string searchScope: "/home/demo"
        readonly property string searchInfo: "3 résultats  ·  rg"
        readonly property var searchResults: [
            { name: "Rapport été 2026.txt", path: "/home/demo/Documents/Rapport été 2026.txt", line: 2, text: "Voir le rapport mensuel joint, section 3." },
            { name: "notes.md", path: "/home/demo/Projets/fortiq/notes.md", line: 14, text: "- envoyer le rapport à l'équipe avant vendredi" },
            { name: "deploy.log", path: "/home/demo/Travail/logs/deploy.log", line: 188, text: "[INFO] rapport de déploiement écrit dans /var/tmp" }
        ]
        function cancelSearch() {}
    }
    QtObject {
        id: preview
        readonly property bool previewBusy: false
        readonly property string previewKind: data.fixturePreview === "editor" ? "text" : data.fixturePreview
        readonly property string previewPath: data.fixturePreview === "image" ? "/home/demo/Montagne.jpg" : "/home/demo/Notes.txt"
        readonly property string previewInfo: data.fixturePreview === "image" ? "2,4 Mo" : "1,8 Ko — 12 lignes"
        readonly property string previewImage: data.fixturePreview === "image" ? Qt.resolvedUrl("../../assets/wallpapers/aurora.jpg") : ""
        readonly property bool previewEditable: data.fixturePreview === "text" || data.fixturePreview === "editor"
        readonly property bool saving: false
        readonly property string previewText: "# Notes — exemple de démonstration

Réunion projet CYBOU : 14:00 – 15:00
- vérifier les journaux de nginx
- mettre à jour /etc/fortiq/notes.conf

Привет, это тест UTF-8 ✓

TODO:
  1. Smart Files : aperçu
  2. Logs : filtres
Fin.
"
    }
}
