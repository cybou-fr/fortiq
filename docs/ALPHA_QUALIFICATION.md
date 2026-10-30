# Квалификация alpha 0.1.0-alpha.1: матрица «угроза → контроль → тест → результат»

Статус на 30.10.2026. Рабочий документ (шаг A1 плана подготовки alpha), **не разрешение на выпуск**. Публикация, смена видимости репозитория и подпись — только по решению владельца ([RELEASE](RELEASE.md), [PRODUCTION_READINESS](PRODUCTION_READINESS.md)). Что входит в продукт — [RELEASE_SCOPE_1_0](RELEASE_SCOPE_1_0.md).

Правило документа: в колонке «Результат» стоит только то, что реально запускалось на указанном коммите. Исторические прогоны ([PRODUCTION_READINESS](PRODUCTION_READINESS.md)) здесь **не засчитываются**. «Не запускалось» — это не «пройдено».

## 1. Что именно проверялось

- Коммит: `f57ff9e353b627e93865a9f99755971cef880fa1` (30.10.2026). Относительно `bda856d`, на котором прошёл последний офлайн-гейт, изменены только `README.md`, `docs/ARCHITECTURE.md`, `docs/ROADMAP.md` и добавлен `docs/OPEN_DECLARATIVE_APPS_SDK.md`; код и тесты те же.
- **Офлайн-гейт** `python tool/ci_check.py --build build-msvc …` (Windows, MSVC, Qt 6.12.0): все шаги пройдены; CTest 27/27; QtTest **561 passed, 0 failed, 19 skipped** (каждый пропуск имеет исключение в [`tool/gate_waivers.json`](../tool/gate_waivers.json), область `offline`).
- **Живой гейт** (`tool/prepare_live_fixture.py`, одноразовые WSL-стенды): **на этом коммите не запускался.** Все тесты, помеченные «живой» в матрице, имеют статус «не проверено на текущем коммите».
- Закреплённые зависимости: `python tool/check_pins.py` — libssh2 `1.11.1-git6e7b207` и OpenSSL `3.6.5#2` установлены именно как overlay-порты проекта.
- Лицензии пакета: `python tool/check_third_party.py --stage build-dist/stage/FORTIQ` — OK (запрещённых модулей Qt, неизвестных DLL и отладочных плагинов нет; юридическая проверка уведомлений Chromium остаётся обязательной).

## 2. Фактические бинарные зависимости пакета

Сверено `Get-FileHash` каталога `build-dist/stage/FORTIQ` с `vcpkg/installed/x64-windows/bin`:

| Файл | SHA-256 (начало) | Совпадает с vcpkg | Версия по ресурсам DLL | Версия порта vcpkg |
|---|---|---|---|---|
| `libssh2.dll` | `EBC8FB9290FFABE2` | да | `1.11.2_DEV` (снимок git master) | `1.11.1-git6e7b207` (overlay проекта) |
| `libcrypto-3-x64.dll` | `9E4F3E3D5192B63B` | да | `3.6.5` | `3.6.5` |
| `z.dll` | `3367ADD507FCD30D` | да | `1.3.2` | `1.3.2` |
| `Qt6Core.dll` | — | — | `6.12.0.0` | — |
| `Qt6WebEngineCore.dll` | — | — | `6.140.0.0` | — |

Вывод: в пакете лежат именно закреплённые бинарники, подмены нет. **Не проверено:** известные уязвимости этих версий (нужен разбор актуальных advisories libssh2 — снимок не релизный, OpenSSL 3.6.5, Qt 6.12/Chromium из Qt WebEngine); это шаг A4 и он требует доступа к внешним источникам.

**Пакет устарел.** `build-dist` собран 29.10.2026 в 21:50, до правок редактора, подсказок, слоёв окон, выхода из приложения и терминала. Перед любой проверкой пакета его нужно собрать заново из зафиксированного коммита (`tool/package_windows.py --build build-msvc`). Отчёт `package-report.json` ссылается на старую сборку.

## 3. Матрица прослеживаемости

Статусы: **офлайн ✓** — набор прошёл в офлайн-гейте на текущем коде; **живой —** требует одноразового Linux-стенда, на этом коммите не запускался; **нет теста** — контроль есть в коде, автоматической проверки нет.

| # | Угроза | Контроль (где) | Тест | Результат на `f57ff9e` |
|---|---|---|---|---|
| T1 | Подмена ключа сервера / смена хоста | Закрепление отпечатка до аутентификации (`src/ssh/SshSession.cpp`) | `SshIntegrationTest::unknownHostRequiresTrust`, `changedKeyIsRejected` | живой — не проверено |
| T2 | Устаревшие алгоритмы | Отказ на слабых наборах (`src/ssh/SshAlgorithms.cpp`) | `SshIntegrationTest::obsoleteAlgorithmsAreRefused`; офлайн: набор `fortiq_publication` | офлайн ✓ (алгоритмы), живой — не проверено |
| T3 | Смешивание двух серверов | Привязка к серверу и поколению соединения | `SshIntegrationTest::twoHostsNeverMix`, `terminalControllerStopsOnServerChange`, `reliabilityInvariants` | живой — не проверено (нужен второй стенд) |
| T4 | Потеря файла при записи, гонка с внешней правкой | Атомарная публикация с проверкой хеша (`src/ssh/SshEdit.cpp`) | `PublicationTest::atomicReplacementPreservesNameAndMode`, `concurrentEditDuringStaging`, `concurrentNewFileIsNotOverwritten`, `cancelLeavesOriginalAndNoStagingFile`, `publicationFailure`, `stagingFailure` | живой — не проверено |
| T5 | Ложный успех или ложная отмена при обрыве | Исход `unknown`, без автоповтора записи | `PublicationTest::ackLoss`, `readBackMismatchIsUnknown`; офлайн: `TaskManagerTest::cancelRunningIsARequestNotAGuarantee`, `cancellingCanEndAsUnknownOrCancelled`, `queuedCannotSucceedButCanBeUnknown` | TaskManager — офлайн ✓; публикация — живой, не проверено |
| T6 | Небезопасная замена: символьные ссылки, каталоги, права | Отказ на ссылках и каталогах, права проверяются до публикации | `PublicationTest::replaceRefusesSymlinksAndDirectories`, `concurrentPermissionChangeIsNotUndone`; `SshIntegrationTest::symlinksAndCyclesAreContained` | живой — не проверено |
| T7 | Утечка секретов (память, журналы, файлы) | Очистка секретов после использования; секреты не в аргументах команд | `SshIntegrationTest::secretsLeaveNoCopiesInMemory`; офлайн: набор `fortiq_credentials` | офлайн ✓ (хранилище), живой — не проверено. **Сканирование пакета и истории git на секреты не проводилось** |
| T8 | Враждебный сервер: имена файлов, разметка, ответы | Данные не исполняются; ограничения размера и времени | `SshIntegrationTest::maliciousServerCannotCrashTheClient`, `hostileNamesStayData`, `hungServerCommandIsStopped`; офлайн: `fortiq_fuzz_smoke` | офлайн ✓ (fuzz smoke), остальное живой — не проверено |
| T9 | Потеря текста MDok при обрыве | Серверный черновик и экран конфликта | `SshIntegrationTest::mdokServerDraftAndConflict`, `markdownDocumentsRoundTripConflictAndDisconnect` | живой — не проверено |
| T10 | Browser обходит SSH | Шлюз и закрытый профиль Chromium; блок внутренних адресов | `SshIntegrationTest::browserAddressPolicy`, `browserSurvivesDnsRebinding`; `GatewayLiveTest`, `EngineLiveTest` (`noDirectConnectionsWhateverThePageTries`, `theWebRtcGapStillSendsNothing`); офлайн: `fortiq_browser`, `fortiq_gateway`, `fortiq_engine` | офлайн ✓ (логика), живые наборы — не проверено |
| T11 | Mail / Database: инъекции, права | Запрет записи, лимиты, враждебный MIME | `mailSurvivesHostileMime`, `databaseAdversarialCorpus`, `databaseAppIsReadOnlyAndBounded` | офлайн ✓ (`fortiq_mail`, `fortiq_db`), живые — не проверено |
| T12 | Окна, фокус, выход из приложения | Слои оболочки, защита выхода | `ShellLayoutTest` (49 строк + `codeEditor`, `launcherAndQuit`, `stateMessageRetry`) | офлайн ✓ |
| T13 | Нарушена установка / состав пакета | `package_windows.py`, `check_third_party.py` | сам скрипт; чистая установка — вручную | состав ✓ (старый пакет); **чистая установка не проверялась** |

Изменения 30.10.2026, затрагивающие данные и сессии, которые **не покрыты живым прогоном**: `DocumentsController::closeDocument` и режим кода (общий контроллер MDok), терминал больше не задача и не блокирует выход, флаг подтверждённого выхода. Блок «Terminal» в `SshIntegrationTest` обновлён, но не запускался.

## 4. Блокеры P0 (на 30.10.2026)

| ID | Блокер | Что нужно для закрытия |
|---|---|---|
| B1 | Живые наборы не прогнаны на текущем коммите | `python tool/prepare_live_fixture.py` на одноразовых стендах; ни одного пропуска вне исключений; лог приложить |
| B2 | Пакет устарел | Пересобрать из зафиксированного коммита, пересчитать SHA-256, повторить `check_third_party.py`, получить новый `package-report.json` |
| B3 | Уязвимости поставляемых компонентов не разобраны | Сверить libssh2 (снимок `6e7b207`), OpenSSL 3.6.5, Qt 6.12 и Chromium (Qt WebEngine) с актуальными advisories; записать результат с датой |
| B4 | Чистая установка не проверена | Владелец: чистая Windows 11 без Qt SDK, чек-лист [PLATFORM_QUALIFICATION](PLATFORM_QUALIFICATION.md) |
| B5 | Секреты в пакете и истории | Сканирование пакета и всей истории git (включая тег `flutter-final`) |
| B6 | Нет CI-свидетельства | `.github/workflows` удалён (`7bbcfb3`); без автоматического запуска повторяемость на чистой машине не доказана |
| B7 | Права на обои, значки, шрифты | Подтверждение прав владельцем ([PRODUCTION_READINESS](PRODUCTION_READINESS.md), G7) |

## 5. Что оставить в alpha, предложение владельцу

Решает владелец. Предложение исходит из принципа «функция без собственного закрытого security-gate не идёт в публичный пакет»:

| Функция | Предложение | Условие включения |
|---|---|---|
| SSH, Files, Terminal, Logs, System/Network/Disks (только чтение), MDok, Éditeur | в alpha | P0 B1–B7 закрыты; Éditeur и режим кода — только после живого прогона блока документов |
| Browser (движок Chromium, Qt WebEngine) | **исключить из первой alpha** | живые `GatewayLiveTest` и `EngineLiveTest` без пропусков + юридическая проверка Chromium; размер пакета падает с ≈400 МБ примерно до 125 МБ ([RELEASE](RELEASE.md)) |
| Mail, Database | исключить или оставить с явным запретом на рабочие данные | живые `mail*` и `database*` без пропусков; пароли почты лежат на сервере в открытом виде ([RELEASE_SCOPE_1_0](RELEASE_SCOPE_1_0.md)) |
| Android, SDK, ИИ | вне alpha | — |

## 6. Go / No-Go

Все пункты обязательны. Нельзя компенсировать провал безопасности качеством интерфейса или долей зелёных тестов.

| Условие | Состояние |
|---|---|
| Сборка из зафиксированного SHA | открыто: SHA зафиксирован, пакет собран на другом коммите |
| Офлайн-гейт на этом SHA | **закрыто** (561 passed, 0 failed, 19 skipped с исключениями) |
| Живые SSH-наборы без пропусков | открыто (B1) |
| Нет известных критических уязвимостей в поставляемых компонентах | открыто (B3) |
| Host-key и секреты подтверждены на этом SHA | открыто (живые T1, T7; B5) |
| Нет незамеченной потери данных | открыто (живые T4–T6, T9) |
| Чистая установка Windows | открыто (B4) |
| Лицензии, SBOM, контрольные суммы | частично: лицензии ✓, SBOM и контрольные суммы для нового пакета — после B2 |
| Known Issues и предупреждение «только тестовый хост» в релизных заметках | открыто |

Вердикт на 30.10.2026: **No-Go.** Закрыт один пункт из девяти. Следующий шаг — B1 (живой гейт), затем B2 и B3.

## 7. Порядок работы

1. B1: живой прогон, отчёт с явными пропусками. Затем добавить в матрицу колонку «команда, лог, SHA».
2. Усилить, а не размножить тесты там, где сценарий слабый: проверить, что `ackLoss`, `concurrentEditDuringStaging` и `twoHostsNeverMix` действительно воспроизводят отказ (например, обратной мутацией контроля).
3. B2 и B3 вместе: пересборка пакета, SBOM (`tool/make_sbom.py`), контрольные суммы, разбор advisories.
4. B5 и B6, затем B4 у владельца.
5. Только после этого: релизные заметки, Known Issues, решение владельца о публикации.
