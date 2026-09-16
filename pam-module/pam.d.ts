// Ручные типы для модуля "pam" (см. pam.c рядом) — это НЕ официальный
// модуль ucode (в отличие от uloop/ubus/fs/uci/math), поэтому ts-ucode
// его не типизирует из коробки, как и "socket"/"digest" в этом же
// проекте (см. src/types/socket.d.ts, src/types/digest.d.ts).
declare module 'pam' {
  // true/false — результат проверки; null — транзакция PAM не смогла
  // даже начаться (не те типы аргументов, pam_start() не смог
  // инициализироваться) — см. подробное описание в pam.c и README.md
  // рядом.
  export function authenticate(service: str, username: str, password: str): bool | null;

  // Без аргумента (или numeric=false) — текстовое описание последней
  // ошибки/отрицательного результата; с numeric=true — числовой код
  // PAM_*. null, если последний вызов authenticate() был успешным.
  export function error(numeric?: true): int | null;
  export function error(numeric?: false): str | null;
}
