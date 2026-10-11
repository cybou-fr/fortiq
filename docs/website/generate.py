"""Generate the three static marketing pages. No runtime dependencies."""
from pathlib import Path
from html import escape

ROOT = Path(__file__).resolve().parents[2]

COPY = {
    'en': {
        'title': 'Fortiq — Linux. In a familiar place.',
        'description': 'Meet Fortiq. Your Linux files, documents and server tools, together in one familiar desktop for Windows.',
        'skip': 'Skip to content', 'home': 'Fortiq home', 'navigation': 'Main navigation', 'language': 'Language',
        'overview': 'Overview', 'tools': 'Tools', 'windows': 'For Windows',
        'pricing': 'Pricing', 'pricingTitle': 'Everything included.',
        'pricingText': 'Two ways to get Fortiq. The same full functionality.',
        'introPeriod': '14 days', 'proPeriod': 'per year', 'proPrice': '39.90 €',
        'introTerms': 'One-time purchase. No automatic renewal.',
        'proTerms': 'Annual subscription. Renews every year.', 'full': 'Full functionality',
        'hero1': 'Linux.', 'hero2': 'In a familiar place.',
        'intro': 'Your files. Your tools. Your way of working.<br> Bring your Linux server into one familiar desktop.',
        'explore': 'Explore Fortiq', 'see': 'Take a closer look',
        'work': 'Work', 'manage': 'Manage', 'previewLabel': 'Illustrative Fortiq desktop',
        'previewNote': 'Illustrative interface. Example content.', 'previewModes': 'Interface preview',
        'files': 'Files', 'terminal': 'Terminal', 'services': 'Services', 'homeFolder': 'Home',
        'documents': 'Documents', 'projects': 'Projects', 'folders': 'Folders', 'name': 'Name',
        'modified': 'Modified', 'today': 'Today', 'notes': 'Project notes',
        'notesSubtitle': 'A little clarity for the next big idea.', 'notesText': 'Ideas, decisions and next steps.<br> Everything worth keeping, close at hand.',
        'serviceName': 'Service', 'state': 'State', 'running': 'Running', 'active': 'Active',
        'system': 'System overview', 'cpu': 'CPU', 'memory': 'Memory', 'connection': 'Connected via SSH',
        'facts': ['12 built-in tools', 'A familiar desktop', 'Your server. Your account.'],
        'flowLabel': 'ONE PLACE FOR YOUR WORK', 'flow1': 'Less switching.', 'flow2': 'More doing.',
        'flowText': 'A document to finish. A service to check. A website to open.<br> Keep the parts of your work together.',
        'fileLabel': 'YOUR WORK, WITHIN REACH', 'fileTitle': 'From finding it<br> to finishing it.',
        'fileText': 'Browse your server’s files, edit text and keep notes in Markdown. Open an image. Pick up the task that matters.',
        'fileMini': 'Find. Open. Make it yours.',
        'systemLabel': 'A CLEARER VIEW OF LINUX', 'systemTitle': 'See what’s happening.<br> Know where to look.',
        'systemText': 'Processes, services, logs and network tools. Get a view of your system, with a terminal right there when you need it.',
        'systemMini': 'Services. Logs. Terminal.',
        'browserLabel': 'YOUR INTERNAL WORLD', 'browserTitle': 'The portal.<br> And the work around it.',
        'browserText': 'Open websites and internal services through your server’s SSH proxy. Keep your documents and tools alongside them.',
        'browserDemo': 'Your internal workspace', 'browserSub': 'Sites, documents and daily work.',
        'toolsLabel': 'BUILT TO WORK TOGETHER', 'toolsTitle': 'A whole set of possibilities.',
        'toolsText': 'Twelve tools. One desktop. Start with what you need.',
        'filters': 'Tool categories', 'all': 'All tools', 'systemFilter': 'System',
        'singular': 'tool', 'few': 'tools', 'many': 'tools',
        'toolsNote': 'System mail reads local Linux mailboxes. System actions depend on your server permissions.',
        'connectLabel': 'YOUR LINUX. YOUR CONNECTION.', 'connectTitle': 'A desktop here.<br> A server there.<br> Everything closer.',
        'connectText': 'Fortiq runs on your computer and connects over SSH. Use your own server and account, without a separate server agent. Confirm the server key when you first connect.',
        'computer': 'Your computer', 'server': 'Your Linux server',
        'windowsTitle': 'A new place<br> for your Linux work.',
        'windowsText': 'Install the app. Connect your server.<br> Make yourself at home.',
        'requirements': ['Windows 10/11 x64', 'Debian / Ubuntu', 'SSH access'],
        'steps': [('Install Fortiq', 'A desktop app on your Windows computer.'), ('Connect your Linux', 'Use your server account and confirm its key.'), ('Get to work', 'Open the files and tools your task needs.')],
        'footer': 'Your Linux. A familiar place.', 'top': 'Back to top',
        'cards': [
            ('Files', 'Find the right file. Open it, organise folders and work with what’s on your server.'),
            ('Editor', 'Edit text and configuration, with a reminder when you have unsaved changes.'),
            ('Docs & notes', 'Write in Markdown and see your document take shape as you go.'),
            ('System mail', 'Read local Linux mailboxes alongside the rest of your server tools.'),
            ('Browser', 'Open websites and internal services through your server’s SSH proxy.'),
            ('Images', 'View server images, zoom into details and rotate for a better look.'),
            ('Search', 'Find a file by its name or a piece of text you remember.'),
            ('Terminal', 'Run commands and scripts. With tmux, return to your session after a connection drop.'),
            ('Processes', 'See CPU and memory usage, and confirm before ending a selected process.'),
            ('Services', 'Check, start, stop and restart systemd services.'),
            ('Network', 'Inspect interfaces, open TCP ports and UFW firewall rules.'),
            ('Logs', 'Find diagnostic messages in Linux logs with text filters.'),
        ],
    },
    'fr': {
        'title': 'Fortiq — Linux. Comme chez vous.',
        'description': 'Découvrez Fortiq. Vos fichiers Linux, vos documents et vos outils serveur réunis dans un bureau familier pour Windows.',
        'skip': 'Aller au contenu', 'home': 'Accueil Fortiq', 'navigation': 'Navigation principale', 'language': 'Langue',
        'overview': 'Présentation', 'tools': 'Outils', 'windows': 'Pour Windows',
        'pricing': 'Tarifs', 'pricingTitle': 'Tout est inclus.',
        'pricingText': 'Deux façons de choisir Fortiq. Les mêmes fonctionnalités complètes.',
        'introPeriod': '14 jours', 'proPeriod': 'par an', 'proPrice': '39,90 €',
        'introTerms': 'Achat unique. Sans renouvellement automatique.',
        'proTerms': 'Abonnement annuel. Renouvellement chaque année.', 'full': 'Toutes les fonctionnalités',
        'hero1': 'Linux.', 'hero2': 'Comme chez vous.',
        'intro': 'Vos fichiers. Vos outils. Votre façon de travailler.<br> Retrouvez votre serveur Linux dans un bureau familier.',
        'explore': 'Découvrir Fortiq', 'see': 'Voir de plus près',
        'work': 'Travailler', 'manage': 'Administrer', 'previewLabel': 'Illustration du bureau Fortiq',
        'previewNote': 'Interface illustrée. Contenu de démonstration.', 'previewModes': 'Aperçu de l’interface',
        'files': 'Fichiers', 'terminal': 'Terminal', 'services': 'Services', 'homeFolder': 'Accueil',
        'documents': 'Documents', 'projects': 'Projets', 'folders': 'Dossiers', 'name': 'Nom',
        'modified': 'Modifié', 'today': 'Aujourd’hui', 'notes': 'Notes de projet',
        'notesSubtitle': 'Un peu de clarté pour la prochaine grande idée.', 'notesText': 'Idées, décisions et prochaines étapes.<br> Tout ce qui compte, à portée de main.',
        'serviceName': 'Service', 'state': 'État', 'running': 'En cours', 'active': 'Actif',
        'system': 'Vue du système', 'cpu': 'CPU', 'memory': 'Mémoire', 'connection': 'Connecté via SSH',
        'facts': ['12 outils intégrés', 'Un bureau familier', 'Votre serveur. Votre compte.'],
        'flowLabel': 'UN ESPACE POUR VOTRE TRAVAIL', 'flow1': 'Moins de détours.', 'flow2': 'Plus de résultats.',
        'flowText': 'Un document à terminer. Un service à vérifier. Un site à ouvrir.<br> Gardez les éléments de votre travail au même endroit.',
        'fileLabel': 'VOTRE TRAVAIL À PORTÉE DE MAIN', 'fileTitle': 'Trouvez-le.<br> Faites-en quelque chose.',
        'fileText': 'Parcourez les fichiers du serveur, modifiez vos textes et prenez des notes en Markdown. Ouvrez une image. Avancez sur ce qui compte.',
        'fileMini': 'Trouver. Ouvrir. Avancer.',
        'systemLabel': 'UNE VUE PLUS CLAIRE DE LINUX', 'systemTitle': 'Voyez ce qui se passe.<br> Sachez où regarder.',
        'systemText': 'Processus, services, journaux et réseau. Gardez une vue sur votre système, avec un terminal à disposition quand il vous faut une commande.',
        'systemMini': 'Services. Journaux. Terminal.',
        'browserLabel': 'VOTRE UNIVERS INTERNE', 'browserTitle': 'Le portail.<br> Et tout ce qui va avec.',
        'browserText': 'Ouvrez vos sites et services internes via le proxy SSH du serveur. Gardez vos documents et vos outils juste à côté.',
        'browserDemo': 'Votre espace interne', 'browserSub': 'Sites, documents et travail quotidien.',
        'toolsLabel': 'PENSÉS POUR TRAVAILLER ENSEMBLE', 'toolsTitle': 'Tout un monde de possibilités.',
        'toolsText': 'Douze outils. Un bureau. Commencez par l’essentiel.',
        'filters': 'Catégories d’outils', 'all': 'Tous les outils', 'systemFilter': 'Système',
        'singular': 'outil', 'few': 'outils', 'many': 'outils',
        'toolsNote': 'La messagerie système lit les boîtes locales de Linux. Les actions système dépendent de vos droits sur le serveur.',
        'connectLabel': 'VOTRE LINUX. VOTRE CONNEXION.', 'connectTitle': 'Le bureau ici.<br> Le serveur là-bas.<br> Tout se rapproche.',
        'connectText': 'Fortiq fonctionne sur votre ordinateur et se connecte via SSH. Utilisez votre serveur et votre compte, sans agent serveur distinct. Confirmez la clé du serveur lors de la première connexion.',
        'computer': 'Votre ordinateur', 'server': 'Votre serveur Linux',
        'windowsTitle': 'Une nouvelle place<br> pour votre travail Linux.',
        'windowsText': 'Installez l’application. Connectez votre serveur.<br> Faites comme chez vous.',
        'requirements': ['Windows 10/11 x64', 'Debian / Ubuntu', 'Accès SSH'],
        'steps': [('Installez Fortiq', 'Une application sur votre ordinateur Windows.'), ('Connectez votre Linux', 'Utilisez votre compte serveur et confirmez sa clé.'), ('Au travail', 'Ouvrez les fichiers et les outils dont vous avez besoin.')],
        'footer': 'Votre Linux. Comme chez vous.', 'top': 'Retour en haut',
        'cards': [
            ('Fichiers', 'Trouvez le bon fichier, ouvrez-le et organisez les dossiers de votre serveur.'),
            ('Éditeur', 'Modifiez vos textes et configurations, avec un rappel des changements non enregistrés.'),
            ('Documents et notes', 'Écrivez en Markdown et voyez votre document prendre forme en temps réel.'),
            ('Messagerie système', 'Consultez les boîtes locales de Linux à côté de vos outils serveur.'),
            ('Navigateur', 'Ouvrez vos sites et services internes via le proxy SSH du serveur.'),
            ('Images', 'Affichez les images du serveur, zoomez sur les détails et faites-les pivoter.'),
            ('Recherche', 'Retrouvez un fichier par son nom ou par un fragment de texte.'),
            ('Terminal', 'Exécutez commandes et scripts. Avec tmux, retrouvez votre session après une coupure.'),
            ('Processus', 'Consultez l’usage du CPU et de la mémoire, puis confirmez l’arrêt d’un processus.'),
            ('Services', 'Vérifiez, démarrez, arrêtez et redémarrez les services systemd.'),
            ('Réseau', 'Inspectez les interfaces, les ports TCP ouverts et les règles UFW.'),
            ('Journaux', 'Trouvez les messages de diagnostic dans les journaux Linux avec des filtres textuels.'),
        ],
    },
    'ru': {
        'title': 'Fortiq — Linux. Как дома.',
        'description': 'Знакомьтесь: Fortiq. Файлы Linux, документы и инструменты управления сервером в одном привычном рабочем столе для Windows.',
        'skip': 'Перейти к содержимому', 'home': 'Fortiq — главная', 'navigation': 'Основная навигация', 'language': 'Язык',
        'overview': 'Обзор', 'tools': 'Инструменты', 'windows': 'Для Windows',
        'pricing': 'Тарифы', 'pricingTitle': 'Всё включено.',
        'pricingText': 'Два варианта доступа к Fortiq. Один полный набор возможностей.',
        'introPeriod': '14 дней', 'proPeriod': 'в год', 'proPrice': '39,90 €',
        'introTerms': 'Разовая покупка, без автопродления.',
        'proTerms': 'Годовая подписка. Ежегодное продление.', 'full': 'Полный функционал',
        'hero1': 'Linux.', 'hero2': 'Как дома.',
        'intro': 'Ваши файлы. Ваши инструменты. Ваш привычный ритм.<br> Работайте с Linux-сервером на одном знакомом рабочем столе.',
        'explore': 'Открыть возможности', 'see': 'Посмотреть поближе',
        'work': 'Работа', 'manage': 'Управление', 'previewLabel': 'Иллюстрация рабочего стола Fortiq',
        'previewNote': 'Иллюстрация интерфейса. Демонстрационные данные.', 'previewModes': 'Просмотр интерфейса',
        'files': 'Файлы', 'terminal': 'Терминал', 'services': 'Службы', 'homeFolder': 'Домашняя папка',
        'documents': 'Документы', 'projects': 'Проекты', 'folders': 'Каталоги', 'name': 'Имя',
        'modified': 'Изменён', 'today': 'Сегодня', 'notes': 'Заметки проекта',
        'notesSubtitle': 'Немного ясности для следующей большой идеи.', 'notesText': 'Идеи, решения и следующие шаги.<br> Всё, что стоит сохранить, — под рукой.',
        'serviceName': 'Служба', 'state': 'Состояние', 'running': 'Работает', 'active': 'Активна',
        'system': 'Обзор системы', 'cpu': 'CPU', 'memory': 'Память', 'connection': 'Подключено по SSH',
        'facts': ['12 встроенных инструментов', 'Привычный рабочий стол', 'Ваш сервер. Ваша учётная запись.'],
        'flowLabel': 'ОДНО ПРОСТРАНСТВО ДЛЯ РАБОТЫ', 'flow1': 'Меньше переключений.', 'flow2': 'Больше дела.',
        'flowText': 'Закончить документ. Проверить службу. Открыть сайт.<br> Держите всё, что нужно для работы, рядом.',
        'fileLabel': 'ВАША РАБОТА ПОД РУКОЙ', 'fileTitle': 'Найдите нужное.<br> Сделайте главное.',
        'fileText': 'Открывайте файлы сервера, редактируйте тексты и ведите заметки в Markdown. Посмотрите изображение. Продолжайте важную задачу.',
        'fileMini': 'Найти. Открыть. Сделать.',
        'systemLabel': 'LINUX ПЕРЕД ГЛАЗАМИ', 'systemTitle': 'Видеть, что происходит.<br> Знать, где искать.',
        'systemText': 'Процессы, службы, журналы и сеть. Следите за состоянием системы, а когда нужна команда — терминал уже рядом.',
        'systemMini': 'Службы. Логи. Терминал.',
        'browserLabel': 'ВАШИ ВНУТРЕННИЕ РЕСУРСЫ', 'browserTitle': 'Портал.<br> И работа вокруг него.',
        'browserText': 'Открывайте сайты и внутренние сервисы через SSH-прокси сервера. Держите документы и нужные инструменты в соседних окнах.',
        'browserDemo': 'Ваше рабочее пространство', 'browserSub': 'Сайты, документы и повседневные задачи.',
        'toolsLabel': 'СОЗДАНЫ ДЛЯ ОБЩЕЙ РАБОТЫ', 'toolsTitle': 'Целый набор возможностей.',
        'toolsText': 'Двенадцать инструментов. Один рабочий стол. Начните с нужного.',
        'filters': 'Категории инструментов', 'all': 'Все инструменты', 'systemFilter': 'Система',
        'singular': 'инструмент', 'few': 'инструмента', 'many': 'инструментов',
        'toolsNote': 'Системная почта читает локальные ящики Linux. Системные действия зависят от ваших прав на сервере.',
        'connectLabel': 'ВАШ LINUX. ВАШЕ ПОДКЛЮЧЕНИЕ.', 'connectTitle': 'Рабочий стол здесь.<br> Сервер там.<br> Всё ближе.',
        'connectText': 'Fortiq работает на вашем компьютере и подключается по SSH. Используйте свой сервер и свою учётную запись без отдельного серверного агента. При первом подключении подтвердите ключ сервера.',
        'computer': 'Ваш компьютер', 'server': 'Ваш Linux-сервер',
        'windowsTitle': 'Новое место<br> для работы с Linux.',
        'windowsText': 'Установите приложение. Подключите сервер.<br> Чувствуйте себя как дома.',
        'requirements': ['Windows 10/11 x64', 'Debian / Ubuntu', 'SSH-доступ'],
        'steps': [('Установите Fortiq', 'Приложение на вашем Windows-компьютере.'), ('Подключите Linux', 'Укажите учётную запись и подтвердите ключ сервера.'), ('Начните работу', 'Откройте файлы и инструменты для вашей задачи.')],
        'footer': 'Ваш Linux. Как дома.', 'top': 'Наверх',
        'cards': [
            ('Файлы', 'Найдите нужный файл, откройте его и наведите порядок в каталогах сервера.'),
            ('Редактор', 'Редактируйте тексты и конфигурации с напоминанием о несохранённых правках.'),
            ('Документы и заметки', 'Пишите в Markdown и сразу видите, как выглядит ваш документ.'),
            ('Системная почта', 'Читайте локальные ящики Linux рядом с остальными инструментами сервера.'),
            ('Браузер', 'Открывайте сайты и внутренние сервисы через SSH-прокси сервера.'),
            ('Изображения', 'Рассмотрите изображение на сервере, увеличьте детали и поверните для удобства.'),
            ('Поиск', 'Найдите файл по имени или фрагменту текста, который помните.'),
            ('Терминал', 'Выполняйте команды и скрипты. С tmux возвращайтесь к сессии после обрыва связи.'),
            ('Процессы', 'Посмотрите нагрузку на CPU и память, подтвердите завершение выбранного процесса.'),
            ('Службы', 'Проверьте, запустите, остановите или перезапустите службу systemd.'),
            ('Сеть', 'Посмотрите интерфейсы, открытые TCP-порты и правила UFW.'),
            ('Логи', 'Найдите диагностические сообщения в журналах Linux с текстовыми фильтрами.'),
        ],
    },
}

PATHS = [
    '<path d="M3 7h7l2 2h9v11H3z"/><path d="M3 7V4h7l2 3"/>',
    '<path d="M14 4l6 6M4 20l4-1L20 7a2 2 0 0 0-3-3L5 16z"/>',
    '<path d="M5 3h10l4 4v14H5zM14 3v5h5M8 12h8M8 16h6"/>',
    '<rect x="3" y="5" width="18" height="14" rx="3"/><path d="m3 6 9 7 9-7"/>',
    '<circle cx="12" cy="12" r="9"/><ellipse cx="12" cy="12" rx="4" ry="9"/><path d="M3 12h18"/>',
    '<rect x="3" y="3" width="18" height="18" rx="3"/><circle cx="8" cy="8" r="1.5"/><path d="m4 18 6-6 4 4 3-3 4 4"/>',
    '<circle cx="10" cy="10" r="7"/><path d="m15 15 6 6"/>',
    '<path d="m5 7 5 5-5 5M13 17h6"/><rect x="2" y="3" width="20" height="18" rx="3"/>',
    '<path d="M5 20V9M12 20V4M19 20v-7"/>',
    '<path d="M12 8v4l3 2"/><circle cx="12" cy="12" r="8"/><path d="M12 1v3M12 20v3M1 12h3M20 12h3"/>',
    '<circle cx="12" cy="5" r="3"/><circle cx="5" cy="19" r="3"/><circle cx="19" cy="19" r="3"/><path d="M12 8v5M5 16v-3h14v3"/>',
    '<path d="M8 5h12M8 12h12M8 19h12M3 5h1M3 12h1M3 19h1"/>',
]

def icon(index, css=''):
    return f'<svg class="{css}" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">{PATHS[index]}</svg>'

def render(lang):
    c = COPY[lang]
    prefix = '' if lang == 'en' else '../'
    routes = {'en': prefix + 'index.html', 'fr': prefix + 'fr/index.html', 'ru': prefix + 'ru/index.html'}
    languages = ''.join(f'<a href="{routes[l]}" lang="{l}" hreflang="{l}" aria-label="{name}"'+(' aria-current="page"' if l == lang else '')+f'>{l.upper()}</a>' for l, name in [('en','English'),('fr','Français'),('ru','Русский')])
    alternate = ''.join(f'<link rel="alternate" hreflang="{l}" href="{url}">' for l, url in routes.items())
    cards = ''.join(f'<article class="tool" data-category="{("work system" if i in (0,1,2,3,4,6) else "work" if i==5 else "system")}"><span class="tool-icon">{icon(i)}</span><h3>{escape(title)}</h3><p>{escape(text)}</p></article>' for i,(title,text) in enumerate(c['cards']))
    facts = ''.join(f'<span>{escape(x)}</span>' for x in c['facts'])
    requirements = ''.join(f'<span>{escape(x)}</span>' for x in c['requirements'])
    steps = ''.join(f'<li><span class="step-number">0{i+1}</span><h3>{escape(title)}</h3><p>{escape(text)}</p></li>' for i,(title,text) in enumerate(c['steps']))
    file_rows = ''.join(f'<div class="file-row">{icon(i if i==0 else 2)}<span>{name}</span><small>{c["today"]}</small></div>' for i,name in enumerate([c['projects'],'brief.md','config.yml','notes.md']))
    service_rows = ''.join(f'<div class="service-row"><span class="status-dot"></span><span>{name}</span><small>{c["running"]}</small></div>' for name in ['nginx.service','ssh.service','cron.service'])
    html = f'''<!doctype html>
<html lang="{lang}">
<head>
  <meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
  <meta name="theme-color" content="#fafafa"><meta name="description" content="{escape(c['description'], quote=True)}">
  <title>{escape(c['title'])}</title>
  <link rel="icon" type="image/png" href="{prefix}assets/fortiq-icon.png">
  <link rel="stylesheet" href="{prefix}styles.css"><script src="{prefix}app.js" defer></script>
  {alternate}<link rel="alternate" hreflang="x-default" href="{routes['en']}">
</head>
<body id="top">
  <a class="skip" href="#main">{c['skip']}</a>
  <header class="site-header"><div class="nav-shell">
    <a class="brand" href="#top" aria-label="{c['home']}"><img src="{prefix}assets/fortiq-icon.png" alt="" width="34" height="34"><span>fortiq</span></a>
    <nav class="main-nav" aria-label="{c['navigation']}"><a href="#overview">{c['overview']}</a><a href="#tools">{c['tools']}</a><a href="#pricing">{c['pricing']}</a><a href="#windows">{c['windows']}</a></nav>
    <nav class="languages" aria-label="{c['language']}">{languages}</nav>
  </div></header>
  <main id="main">
    <section class="hero" aria-labelledby="hero-title">
      <div class="hero-copy wrap"><p class="product-name">Fortiq</p><h1 id="hero-title">{c['hero1']}<br> <span>{c['hero2']}</span></h1><p class="hero-intro">{c['intro']}</p><div class="hero-actions"><a class="button" href="#tools">{c['explore']}</a><a class="link" href="#workspace">{c['see']} <span aria-hidden="true">↘</span></a></div></div>
      <div class="showcase wrap" id="workspace">
        <div class="mode-switch" role="group" aria-label="{c['previewModes']}"><button type="button" data-mode="work" aria-pressed="true">{c['work']}</button><button type="button" data-mode="system" aria-pressed="false">{c['manage']}</button></div>
        <div class="desktop" data-scene="work" role="img" aria-label="{c['previewLabel']}">
          <div class="desktop-bar"><span class="desktop-brand"><img src="{prefix}assets/fortiq-icon.png" alt="" width="19" height="19">Fortiq</span><span class="server-label"><i></i> MY-LINUX</span><span class="desktop-connection">{c['connection']}</span></div>
          <div class="desktop-stage">
            <div class="desktop-wallpaper"><span>f.</span></div>
            <div class="app-window file-window"><div class="window-bar">{icon(0)}<b>{c['files']}</b><span class="window-controls">− &nbsp; □ &nbsp; ×</span></div><div class="file-address">← &nbsp; ↑ <span>/home/user/projects</span></div><div class="file-content"><aside><small>{c['folders']}</small><span class="selected">{c['homeFolder']}</span><span>{c['documents']}</span><span>{c['projects']}</span></aside><div class="file-list"><div class="file-table-head"><span>{c['name']}</span><span>{c['modified']}</span></div>{file_rows}</div></div></div>
            <div class="app-window note-window"><div class="window-bar">{icon(2)}<b>{c['notes']}</b><span class="window-controls">− &nbsp; ×</span></div><div class="note-content"><small>notes.md</small><h3>{c['notesSubtitle']}</h3><p>{c['notesText']}</p><div class="note-check"><i>✓</i><span></span></div><div class="note-check"><i>✓</i><span></span></div></div></div>
            <div class="app-window terminal-window"><div class="window-bar">{icon(7)}<b>{c['terminal']}</b><span class="window-controls">− &nbsp; ×</span></div><div class="terminal-content"><p><span>user@linux</span> <b>~</b> $ uptime -p</p><p>up 2 days, 4 hours</p><p><span>user@linux</span> <b>~</b> $ <i class="cursor"></i></p></div></div>
            <div class="app-window service-window"><div class="window-bar">{icon(9)}<b>{c['services']}</b><span class="window-controls">− &nbsp; ×</span></div><div class="service-content"><div class="file-table-head"><span>{c['serviceName']}</span><span>{c['state']}</span></div>{service_rows}</div></div>
            <div class="app-window metrics-window"><div class="window-bar">{icon(8)}<b>{c['system']}</b></div><div class="metrics-content"><div><small>{c['cpu']}</small><strong>12<span>%</span></strong><div class="metric-bars" aria-hidden="true"><i></i><i></i><i></i><i></i><i></i><i></i><i></i><i></i><i></i></div></div><div><small>{c['memory']}</small><strong>2.4<span> GB</span></strong><div class="memory-track"><i></i></div></div></div></div>
            <div class="desktop-dock" aria-hidden="true">{''.join(icon(i) for i in [0,1,2,4,6,7,9])}</div>
          </div>
        </div>
        <p class="preview-caption">{c['previewNote']}</p>
      </div>
      <div class="facts wrap">{facts}</div>
    </section>
    <section class="overview section-space" id="overview" aria-labelledby="overview-title">
      <div class="section-intro wrap"><p class="eyebrow">{c['flowLabel']}</p><h2 id="overview-title">{c['flow1']}<br> <span>{c['flow2']}</span></h2><p class="section-lead">{c['flowText']}</p></div>
      <div class="bento wrap">
        <article class="feature feature-files"><div class="feature-copy"><p class="eyebrow">{c['fileLabel']}</p><h3>{c['fileTitle']}</h3><p>{c['fileText']}</p></div><div class="floating-files" aria-hidden="true"><div class="file-tile">{icon(0)}<span>{c['projects']}</span></div><div class="file-tile">{icon(2)}<span>brief.md</span></div><div class="file-tile">{icon(5)}<span>image.png</span></div></div><p class="feature-bottom">{c['fileMini']}</p></article>
        <article class="feature feature-system"><div class="feature-copy"><p class="eyebrow">{c['systemLabel']}</p><h3>{c['systemTitle']}</h3><p>{c['systemText']}</p></div><div class="mini-services" aria-hidden="true">{service_rows}</div><p class="feature-bottom">{c['systemMini']}</p></article>
        <article class="feature feature-browser"><div class="feature-copy"><p class="eyebrow">{c['browserLabel']}</p><h3>{c['browserTitle']}</h3><p>{c['browserText']}</p></div><div class="browser-art" aria-hidden="true"><div class="browser-bar"><span class="browser-dots">● ● ●</span><span>intranet.local</span></div><div class="browser-page"><span class="portal-mark">{icon(4)}</span><h4>{c['browserDemo']}</h4><p>{c['browserSub']}</p><div class="portal-tiles"><span>{icon(2)}</span><span>{icon(0)}</span><span>{icon(9)}</span></div></div></div></article>
      </div>
    </section>
    <section class="tool-section section-space wrap" id="tools" aria-labelledby="tools-title"><div class="section-intro"><p class="eyebrow">{c['toolsLabel']}</p><h2 id="tools-title">{c['toolsTitle']}</h2><p class="section-lead">{c['toolsText']}</p></div><div class="tool-toolbar"><div class="filter-group" role="group" aria-label="{c['filters']}"><button type="button" data-filter="all" aria-pressed="true">{c['all']}</button><button type="button" data-filter="work" aria-pressed="false">{c['work']}</button><button type="button" data-filter="system" aria-pressed="false">{c['systemFilter']}</button></div><span class="tools-count" aria-live="polite" data-singular="{c['singular']}" data-few="{c['few']}" data-many="{c['many']}">12 {c['many']}</span></div><div class="tools-grid">{cards}</div><p class="section-note">{c['toolsNote']}</p></section>
    <section class="connection wrap" aria-labelledby="connection-title"><div class="connection-copy"><p class="eyebrow">{c['connectLabel']}</p><h2 id="connection-title">{c['connectTitle']}</h2><p>{c['connectText']}</p></div><div class="connection-art" aria-hidden="true"><div class="connection-orbit"></div><div class="connection-computer"><img src="{prefix}assets/fortiq-icon.png" alt="" width="76" height="76"><span>{c['computer']}</span></div><div class="ssh-line"><span>SSH</span></div><div class="connection-server"><div class="server-unit"><i></i><i></i></div><div class="server-unit"><i></i><i></i></div><span>{c['server']}</span></div></div></section>
    <section class="pricing section-space wrap" id="pricing" aria-labelledby="pricing-title"><div class="section-intro"><p class="eyebrow">{c['pricing']}</p><h2 id="pricing-title">{c['pricingTitle']}</h2><p class="section-lead">{c['pricingText']}</p></div><div class="pricing-grid"><article class="price-card" aria-labelledby="intro-title"><h3 id="intro-title">FORTIQ INTRO</h3><p class="price">2 €</p><p class="price-period">{c['introPeriod']}</p><p class="price-full">{c['full']}</p><p class="price-terms">{c['introTerms']}</p></article><article class="price-card price-pro" aria-labelledby="pro-title"><h3 id="pro-title">FORTIQ PRO</h3><p class="price">{c['proPrice']}</p><p class="price-period">{c['proPeriod']}</p><p class="price-full">{c['full']}</p><p class="price-terms">{c['proTerms']}</p></article></div></section>
    <section class="windows section-space wrap" id="windows" aria-labelledby="windows-title"><img src="{prefix}assets/fortiq-icon.png" alt="" width="86" height="86"><p class="product-name">Fortiq {c['windows'].lower().replace('windows', 'Windows')}</p><h2 id="windows-title">{c['windowsTitle']}</h2><p class="section-lead">{c['windowsText']}</p><div class="requirements">{requirements}</div><ol class="start-steps">{steps}</ol></section>
  </main>
  <footer class="footer wrap"><a class="brand" href="#top" aria-label="{c['home']}"><img src="{prefix}assets/fortiq-icon.png" alt="" width="28" height="28"><span>fortiq</span></a><p>{c['footer']}</p><a class="back-top" href="#top">{c['top']} <span aria-hidden="true">↑</span></a></footer>
</body>
</html>
'''
    target = ROOT / 'www' / ('' if lang == 'en' else lang)
    target.mkdir(parents=True, exist_ok=True)
    (target / 'index.html').write_bytes(html.encode('utf-8'))

if __name__ == '__main__':
    for language in COPY:
        render(language)
    print('Generated EN (default), FR and RU static pages.')
