# Disks — обзор хранения, 22.10.2026

Первый срез только для чтения выбранного Linux-сервера через SSH. Следующие приложения:
MDok (.md/PDF), затем Mail. Собственного серверного агента нет.

## Возможности

- «Disques» запускается из панели приложений и launcher. Окно использует Glass,
  WindowDragArea и общий порядок окон; на узком экране sidebar становится drawer.
- Без SSH — выбор сервера. Открытие/повторное открытие и смена подключённого источника
  загружают снимок; кнопка обновления читает его заново. Автоматического polling нет.
- «Volumes montés»: источник, точка монтирования, тип FS, ёмкость, использованное и
  доступное место, полоса used/size. Точка монтирования открывается в Files без shell-команды.
- «Périphériques»: диск/раздел, родитель, ёмкость, тип FS, метка, одна точка монтирования,
  read-only флаг при наличии. Неизвестные значения явно обозначены.
- Отдельное соединение libssh2 с pin до аутентификации, рабочий поток; данные доступа
  только из RAM RemoteController. Как остальные текущие адаптеры, open требует SFTP.
- Disconnect/смена поколения очищают данные и отменяют чтение; поздний результат проверяет
  номер запроса и token сервера. Отсутствие CLI, права, неподдерживаемые опции, truncation,
  некорректный JSON — ошибки источника. Если один инструмент доступен, показана частичная
  картина с отдельной ошибкой второго. Пустой успешный массив отличается от ошибки.

## Проверенный CLI-контракт

Выбран существующий **util-linux**: `lsblk` для блоковых устройств, `findmnt` для файловых
систем и места (режим df). Колонки заданы явно, размеры — байты, JSON, `LC_ALL=C`:

```sh
lsblk --json --list --bytes --output NAME,PATH,TYPE,PKNAME,SIZE,FSTYPE,LABEL,MOUNTPOINT,RO
findmnt --json --list --df --bytes --output SOURCE,TARGET,FSTYPE,SIZE,USED,AVAIL,OPTIONS
```

Справочники: [lsblk](https://man7.org/linux/man-pages/man8/lsblk.8.html),
[findmnt](https://man7.org/linux/man-pages/man8/findmnt.8.html).
До выбора фактически проверены оба вызова и JSON на тестовом Debian WSL: util-linux **2.41.5**.
`df` GNU coreutils **9.7** также проверен по версии/контракту, но текстовый parser не добавлен:
findmnt уже даёт ёмкость в JSON. Другие версии Debian/Ubuntu пока не квалифицированы;
неподдерживаемый контракт показывается ошибкой, без выдуманного fallback.

util-linux содержит несколько лицензий; default GPL-2.0-or-later, отдельные файлы — по
своим заголовкам: [upstream licensing](https://github.com/util-linux/util-linux/blob/master/README.licensing).
Локально просмотрен `/usr/share/doc/util-linux/copyright` тестового Debian. FORTIQ вызывает
уже установленные программы по SSH, не включает их исходники/бинарники в клиент.

До 1 МиБ stdout и 512 строк на источник. Размеры сохраняются как qint64, JSON принимает
целые числа и десятичные строки, null означает «недоступно». Имена — PlainText, в команды
не подставляются. Нет суммирования ёмкостей: bind mounts/общие backing devices могут
дублировать пространство. Доступное место может отличаться от size-used из-за резервов FS.

## Ограничения

Нет форматирования, mount/unmount, удаления разделов, SMART/здоровья, du-анализа каталогов,
LVM/RAID-управления, графа связей и поддержки всех контейнерных конфигураций. lsblk может
не видеть тип FS/метку без прав: неизвестное не выдаётся за отсутствие файловой системы.
MOUNTPOINT — только одна точка; все видимые df-монты находятся в отдельной вкладке.
Физическое устройство Android и чистая Windows-машина ещё не квалифицированы.

## Проверки

DiskInfo QtTest проверяет числа/строки/null, точное значение 9007199254740993, Unicode,
метки с HTML как текст, malformed JSON, отрицательные/нечисловые размеры и лимиты.
Живой тест WSL через SSH проверил оба CLI, auto-load контроллера, обновление, disconnect
во время чтения и отбрасывание результата: **3 passed** с init/cleanup, без skipped.
Отпечаток ECDSA проверен непосредственно на стенде; временный ключ после теста отозван и удалён.

Windows CTest: **9/9**; DiskInfo: **5 passed** с init/cleanup; layout: **42 passed**.
На пяти размерах проверены окно без SSH, server chooser, drawer/Back, заполненные
карточки, PlainText-метка и переход Unicode-пути в Files. Адаптивный снимок просмотрен.
Windows MSVC/Qt 6.12 и Android arm64 APK собраны.
Android APK установлен и запущен в FortiqTablet (emulator-5554); SSH/UI на физическом Android не квалифицированы.

## Cancelled SSH scans (2026-10-28)

Disks remains a **read-only** inventory of remote disks and mountpoints (`lsblk` / `findmnt`); it does not format, mount or modify filesystems. A cancelled inventory request now exits the completion callback before accepting partial results: it explicitly clears stale disk/mount rows, reports `cancelled` and updates TaskManager, instead of potentially displaying an empty or partially populated inventory as `ready`. Epoch and SSH connection-token checks still reject obsolete responses. Change committed without running CI, builds or tests, as requested.
