# Архивная матрица разработки — выведена из обращения

**НЕ ИСПОЛЬЗОВАТЬ КАК ПЛАН.** Этот устаревший снимок содержал прямую
IMAP/SMTP-клиентскую интеграцию, самостоятельные Local/Account adapters
и работу Mail без SSH. Они отменены.

**Единая модель FORTIQ:** Flutter — только frontend. Для каждого APP обязателен
Linux CLI-исполнитель; всё предметное взаимодействие идёт через SSH
(exec/PTY/SFTP/forwarding). Почта — только серверный CLI по SSH,
прямой SMTP/IMAP из FORTIQ запрещён.

Действующие документы:
- [SSH_FRONTEND_CONTRACT](../SSH_FRONTEND_CONTRACT.md) — обязательные правила и CLI-матрица
- [DEVELOPMENT_MATRIX](../DEVELOPMENT_MATRIX.md) — актуальные APP, CORE и FLOW
- [ARCHITECTURE](../ARCHITECTURE.md) — контракты
- [PRODUCT_SCOPE](../PRODUCT_SCOPE.md) — продукт

Старое содержимое сохранено в **Git history** только как исторический материал.
