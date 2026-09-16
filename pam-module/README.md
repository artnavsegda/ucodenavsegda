# pam — модуль ucode для PAM-аутентификации

Небольшой нативный модуль (`.so`-плагин, как `socket`/`digest`/`math`),
через который `lib/auth.ts` проверяет логин/пароль — теперь ЕДИНСТВЕННЫМ
способом, локального списка "аккаунтов" в памяти процесса больше нет
(см. `authLogin()` в `lib/auth.ts`/`lib/auth.uc`) — то есть через PAM, в
конечном счёте по реальным системным учёткам (`/etc/shadow` через
`pam_unix`, или что угодно ещё, что настроено в соответствующем
PAM-сервисе). Без собранного `pam.so` весь `/api/auth/login` перестаёт
работать (`Unable to resolve path for module 'pam'`) — сборка и
настройка сервиса теперь обязательный шаг, см. `DEBIAN-TESTING.md`
(раздел 1.1) и `MACOS-TESTING.md` (раздел 1.3, там же оговорка что
именно на macOS этот модуль живьём не проверялся).

**Это НЕ официальный модуль ucode** (в отличие от `uloop`/`ubus`/`fs`/
`uci`/`math` — см. `TYPESCRIPT.md`). Написан отдельно для этого проекта
по образцу существующих модулей ucode (`digest.c`/`socket.c`/`math.c`
из [openwrt/ucode](https://github.com/openwrt/ucode)) — те же макросы
(`uc_fn_arg`, `uc_function_list_t`, `ucv_string_get` и т.д.), тот же
стиль сообщений об ошибках через `error()`, что и у `socket`.

## API

```ts
pam.authenticate(service: string, username: string, password: string): boolean | null
pam.error(numeric?: boolean): string | number | null
```

- `true` — пароль верный, аккаунт в порядке (`pam_authenticate()` И
  `pam_acct_mgmt()` оба вернули `PAM_SUCCESS`).
- `false` — PAM-транзакция прошла НОРМАЛЬНО, но результат
  отрицательный (неверный пароль, нет такого пользователя, аккаунт
  заблокирован/просрочен) — это обычный, ожидаемый исход, не ошибка.
  Подробности — `pam.error()`, но **не отдавайте это сообщение
  наружу в ответе API** — "нет такого пользователя" против "неверный
  пароль" это готовый канал для перечисления существующих логинов.
- `null` — транзакция не смогла даже начаться (не те типы аргументов,
  `pam_start()` не смог инициализироваться) — то есть проблема
  инфраструктуры, а не введённых данных.

## Собрать

```sh
# заголовки PAM (Debian/Ubuntu: libpam0g-dev, Homebrew: обычно уже
# есть системные — PAM есть в базовой поставке и Linux, и macOS)
apt-get install -y libpam0g-dev   # Debian/Ubuntu

gcc -shared -fPIC -Wall -Wextra \
  -I<путь к заголовкам ucode>/include \
  -I<путь к заголовкам json-c, если pkg-config их не находит> \
  -o pam.so pam.c \
  -lpam

cp pam.so <куда ucode ищет модули>   # см. -L флаг или системный путь
```

**На macOS этой команды НЕ хватит** — там нужен ещё как минимум флаг
`-undefined dynamic_lookup` (без него линковщик Apple падает на
неразрешённых символах `ucv_*`/`uc_vm_*` — они приходят из самого
`ucode` при `dlopen()`, а не линкуются напрямую) и явный `-I` на
`json-c` из Homebrew. Точные команды и разбор каждой ошибки — в
`MACOS-TESTING.md`, раздел 1.3 (собрано по реальной переписке с живой
сборкой на Mac, а не только по документации).

Реально собран и проверен в песочнице именно так — против настоящей
`libpam.so.0` (Ubuntu 24.04, `libpam0g` 1.5.3) и настоящего `ucode`,
собранного для остального проекта (см. `TYPESCRIPT.md`/`DEBIAN-TESTING.md`
про сборку `ucode` из git).

Для интеграции в общую сборку `ucode` (как `socket`/`digest`/`math`)
добавьте в `CMakeLists.txt` самого ucode (по образцу блока
`DIGEST_SUPPORT` — там тоже внешняя библиотека):

```cmake
option(PAM_SUPPORT "PAM plugin support" OFF)

find_library(libpam NAMES pam)

if(PAM_SUPPORT)
  set(LIBRARIES ${LIBRARIES} pam_lib)
  add_library(pam_lib MODULE lib/pam.c)   # положить pam.c в lib/ ucode
  set_target_properties(pam_lib PROPERTIES OUTPUT_NAME pam PREFIX "")
  target_link_options(pam_lib PRIVATE ${UCODE_MODULE_LINK_OPTIONS})
  target_link_libraries(pam_lib ${libpam})
endif()
```

(`find_library`, а не `pkg_check_modules` — у PAM, в отличие от
`libmd`/`libubox`, нет `.pc`-файла ни на Debian, ни где-либо ещё,
проверено на месте: `pkg-config --exists libpam` → отрицательно.)

## Что реально проверено

Собран как настоящий `.so`, загружен настоящим `ucode` (`import * as
pam from 'pam'`), и прогнан live-тестом с НАСТОЯЩИМ Unix-пользователем
(`useradd`/`chpasswd`, PAM-сервис `/etc/pam.d/ucode-api` с
`pam_unix.so`) — сначала как отдельный модуль, потом (после того как
он был вшит в `lib/auth.ts`/`lib/auth.uc`) ещё раз как часть настоящего
запущенного `app.uc`, целиком через настоящие SCGI-запросы
(`/api/auth/login` → токен → `/api/users` с этим токеном →
`/api/auth/logout` → тот же токен уже отклоняется):

| Сценарий | Результат |
|---|---|
| верный пароль | `true`, `error() == null` |
| неверный пароль | `false`, `error() == "Authentication failure"` |
| несуществующий пользователь | `false`, `error() == "User not known to the underlying authentication module"` |
| заблокированный аккаунт (`usermod -L`) + верный пароль | `false`, `error() == "Authentication failure"` |
| не те типы аргументов (число вместо строки) | `null`, `error() == "Expected service, username and password strings"` |
| верный пароль, но у процесса НЕТ прав на `/etc/shadow` (сервер запущен от `nobody`) | `false`, `error() == "Authentication service cannot retrieve authentication info"` — отдельно проверено, чтобы показать: это НЕ "неверный пароль", а именно нехватка привилегий (см. раздел про привилегии ниже) |

Через целый `app.uc` дополнительно проверено: старые демо-креды
`admin`/`admin123` (которых больше нет как локальной записи) теперь
корректно отклоняются как обычный неверный логин/пароль — 401,
`{"error": "Неверный логин или пароль"}`, без утечки в ответ клиенту
того, что там на самом деле сработало (или не сработало) внутри PAM.

## ⚠️ Важная находка при проверке: несуществующий PAM-сервис — это НЕ ошибка

```
authenticate('nonexistent-service-xyz', 'ucodepamtest', 'TestPass123!') => true
```

Опечатка в имени сервиса не приводит к ошибке или `false` — сам PAM
(а не этот модуль) молча подставляет `/etc/pam.d/other`, если файла с
указанным именем нет. На проверенной системе (Ubuntu 24.04)
`/etc/pam.d/other` через `@include common-auth`/`common-account`
делегирует туда же, в `pam_unix` — то есть запрос с ОШИБОЧНЫМ именем
сервиса всё равно успешно проверяет пароль по-настоящему. Это
задокументированное поведение самого PAM, не баг этого модуля, но
из него следует жёсткое практическое правило:

- **Никогда не давайте значению `service` приходить от клиента API**
  (даже случайно, даже как "необязательный" параметр) — всегда
  жёстко прописанная константа в `lib/auth.ts`.
- Заведите ИМЕННО этот сервис в `/etc/pam.d/<ваше-имя>` явно, и
  проверьте (`cat /etc/pam.d/other`), что дефолтный fallback на вашей
  системе не окажется неожиданно более разрешающим, чем вы думаете —
  в хардненых системах `/etc/pam.d/other` часто явно ставят в
  `pam_deny.so`, чтобы опечатка отказывала, а не проходила молча.

## ⚠️ Про привилегии — самое важное для деплоя

`pam_unix` (модуль, который в итоге читает `/etc/shadow`) требует,
чтобы ЗВОНЯЩИЙ процесс имел право читать `/etc/shadow`. У этого
проекта это значит одно из двух:

1. Весь `app.uc` работает от root — тогда любой баг/переполнение в
   обработке ОДНОГО SCGI-запроса (а `handleScgiConnection()` и так уже
   обёрнута в try/catch именно из-за похожих соображений, см. app.ts)
   потенциально становится RCE от root на всём хосте. Для сервиса,
   который уже слушает сеть (пусть и через lighttpd/SCGI) — ощутимо
   больше поверхности атаки, чем нынешняя демо-схема с sha256 в
   памяти.
2. Отдельный маленький setuid-root (или setgid `shadow`) хелпер-бинарник,
   который делает ТОЛЬКО вызов `pam_authenticate`/`pam_acct_mgmt` и
   ничего больше, а сам `app.uc` — обычный непривилегированный
   процесс, который его вызывает (шелл-аут, тот же паттерн, что
   обсуждали для варианта "PAM без нативного модуля"). Тогда весь
   этот `.so`-модуль на самом деле нужно линковать не в основной
   `ucode`, а в отдельный маленький бинарник — усложняет архитектуру,
   но не тянет весь SCGI-сервер под root.

Этот модуль сам по себе такое разделение не делает — какой из двух
путей выбрать, решение уровня деплоя конкретного проекта, не
библиотеки. Для тестового стенда (как в `DEBIAN-TESTING.md`) можно
просто запускать `ucode ./app.uc` через `sudo` — но это ТОЛЬКО для
локальной проверки, не рекомендация для прод-деплоя на роутер.

## Как это реально используется в lib/auth.ts

Это не пример "как можно было бы сделать" — это ровно то, что сейчас в
`src/lib/auth.ts` (и скомпилированном из него `lib/auth.uc`):

```ts
import * as pam from 'pam';

const PAM_SERVICE: str = 'ucode-api'; // константа, никогда не из запроса

export function authLogin(username: any, password: any): str | null {
  if (type(username) != 'string' || type(password) != 'string') return null;

  const result = pam.authenticate(PAM_SERVICE, username, password);
  if (result !== true) {
    const detail = pam.error();
    if (detail) warn(`auth: PAM отказал для "${username}": ${detail}\n`);
    return null; // false ИЛИ null — наружу разница не идёт
  }

  const token = authGenerateToken();
  if (!token) return null;

  sessions[token] = { username: username, issued_at: time() };

  return token;
}
```

Разница с более простым вариантом выше — `pam.error()` логируется
через `warn()` при отказе (виден в stdout/логе демона, не в ответе
API) — это то, что реально пригодилось при отладке (см. таблицу
результатов выше: по этой строке в логе отличили "нет прав на
`/etc/shadow`" от настоящей "неверный пароль").

(остальной код `lib/auth.ts` — генерация токена, TTL сессий,
`authVerify()`/`authExtractToken()` — не поменялся, поменялась только
сама проверка пароля и то, что вместо локального объекта `accounts`
(`admin: sha256('admin123')`) источник правды теперь всегда PAM.)
