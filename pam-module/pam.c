/*
 * pam.c - PAM (Pluggable Authentication Modules) bindings for ucode
 *
 * НЕ входит в официальный набор модулей ucode (в отличие от uloop/ubus/
 * fs/uci/math) — это самостоятельный модуль, написанный для проекта
 * api-server-ucode-scgi по образцу существующих модулей ucode
 * (digest.c/socket.c/math.c: те же макросы, тот же стиль
 * ошибок через error()).
 *
 * Экспортирует:
 *   pam.authenticate(service, username, password) -> bool | null
 *   pam.error([numeric]) -> str | int | null
 *
 * authenticate() возвращает:
 *   true   — пароль верный, аккаунт в порядке (pam_authenticate() +
 *            pam_acct_mgmt() оба вернули PAM_SUCCESS);
 *   false  — PAM-транзакция прошла, но аутентификация не удалась
 *            (неверный пароль, неизвестный пользователь, просроченный
 *            аккаунт и т.п.) — это ОБЫЧНЫЙ отрицательный результат, не
 *            ошибка; подробности — через pam.error() (не для показа
 *            конечному пользователю API, иначе это канал для
 *            перечисления существующих логинов);
 *   null   — сама PAM-транзакция не смогла даже начаться (не те типы
 *            аргументов, pam_start() не смог инициализироваться и
 *            т.п.) — то есть проблема на инфраструктурном уровне, а не
 *            в введённых логине/пароле.
 *
 * ВАЖНО про права доступа: pam_unix (модуль PAM, который в итоге читает
 * /etc/shadow) требует, чтобы ЗВОНЯЩИЙ процесс имел право на чтение
 * /etc/shadow — то есть либо сам ucode-процесс должен работать от root
 * (тогда баг/переполнение в обработке ОДНОГО запроса — это потенциально
 * RCE от root на всём хосте), либо нужен отдельный маленький
 * setuid-root/setgid-shadow хелпер, который дёргает именно эту функцию
 * и ничего больше. Сам этот модуль такое разделение не делает — это
 * решение уровня деплоя, не библиотеки.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <security/pam_appl.h>

#include "ucode/module.h"

static struct {
	int code;
	char *msg;
} last_error;

__attribute__((format(printf, 2, 3))) static void
set_error(int code, const char *fmt, ...)
{
	va_list ap, ap2;
	int len;

	free(last_error.msg);

	last_error.code = code;
	last_error.msg = NULL;

	if (fmt) {
		va_start(ap, fmt);
		va_copy(ap2, ap);

		len = vsnprintf(NULL, 0, fmt, ap);
		va_end(ap);

		if (len >= 0) {
			last_error.msg = malloc((size_t)len + 1);

			if (last_error.msg)
				vsnprintf(last_error.msg, (size_t)len + 1, fmt, ap2);
		}

		va_end(ap2);
	}
}

#define err_return(code, ...) do { set_error(code, __VA_ARGS__); return NULL; } while (0)
#define ok_return(expr) do { set_error(0, NULL); return (expr); } while (0)

/* Данные, которые "разговорная" функция PAM должна отдать модулям
 * (pam_unix и т.п.) в ответ на приглашение ввести пароль — единственное,
 * что нужно нашему демо-сценарию (без интерактивных вопросов). */
struct pam_userdata {
	const char *password;
};

static int
uc_pam_conv(int num_msg, const struct pam_message **msg,
            struct pam_response **resp, void *appdata_ptr)
{
	struct pam_userdata *ud = appdata_ptr;
	struct pam_response *responses;
	int i;

	if (num_msg <= 0 || num_msg > PAM_MAX_NUM_MSG)
		return PAM_CONV_ERR;

	responses = calloc((size_t)num_msg, sizeof(struct pam_response));

	if (!responses)
		return PAM_BUF_ERR;

	for (i = 0; i < num_msg; i++) {
		switch (msg[i]->msg_style) {
		case PAM_PROMPT_ECHO_OFF:
		case PAM_PROMPT_ECHO_ON:
			/* Единственный сценарий, который мы поддерживаем: на
			 * любое приглашение ("Password:" и т.п.) отвечаем тем
			 * паролем, что передали в authenticate(). Настоящий
			 * интерактивный диалог (смена пароля, 2FA-подсказки)
			 * этот модуль не реализует. */
			responses[i].resp = strdup(ud->password ? ud->password : "");

			if (!responses[i].resp) {
				while (i-- > 0)
					free(responses[i].resp);

				free(responses);

				return PAM_BUF_ERR;
			}

			break;

		case PAM_ERROR_MSG:
		case PAM_TEXT_INFO:
		default:
			responses[i].resp = NULL;
			break;
		}

		responses[i].resp_retcode = 0;
	}

	*resp = responses;

	return PAM_SUCCESS;
}

/**
 * Проверяет логин/пароль через PAM-сервис `service` (имя файла в
 * /etc/pam.d/, например "login" или "sshd" — либо отдельный сервис,
 * заведённый специально под этот API, см. README рядом с этим файлом).
 *
 * @function module:pam#authenticate
 *
 * @param {string} service
 * Имя PAM-сервиса (файл в /etc/pam.d/).
 *
 * @param {string} username
 * Имя системного пользователя.
 *
 * @param {string} password
 * Пароль в открытом виде (передаётся PAM исключительно через
 * conversation-callback в памяти процесса, никуда не пишется).
 *
 * @returns {?boolean}
 * `true`/`false` — результат проверки; `null` — транзакция PAM не
 * смогла даже начаться (см. pam.error()).
 */
static uc_value_t *
uc_pam_authenticate(uc_vm_t *vm, size_t nargs)
{
	uc_value_t *service = uc_fn_arg(0);
	uc_value_t *username = uc_fn_arg(1);
	uc_value_t *password = uc_fn_arg(2);
	struct pam_userdata ud;
	struct pam_conv conv;
	pam_handle_t *pamh = NULL;
	int ret, ret2;

	if (ucv_type(service) != UC_STRING ||
	    ucv_type(username) != UC_STRING ||
	    ucv_type(password) != UC_STRING)
		err_return(0, "Expected service, username and password strings");

	ud.password = ucv_string_get(password);

	conv.conv = uc_pam_conv;
	conv.appdata_ptr = &ud;

	ret = pam_start(ucv_string_get(service), ucv_string_get(username), &conv, &pamh);

	if (ret != PAM_SUCCESS)
		err_return(ret, "pam_start(): %s",
		           pamh ? pam_strerror(pamh, ret) : "unable to initialize PAM transaction");

	ret = pam_authenticate(pamh, 0);
	ret2 = (ret == PAM_SUCCESS) ? pam_acct_mgmt(pamh, 0) : ret;

	if (ret != PAM_SUCCESS || ret2 != PAM_SUCCESS) {
		int failed = (ret != PAM_SUCCESS) ? ret : ret2;

		/* Формируем сообщение ПОКА pamh ещё жив — pam_strerror()
		 * читает таблицу, привязанную к транзакции, звать её после
		 * pam_end() уже небезопасно. */
		set_error(failed, "%s", pam_strerror(pamh, failed));
		pam_end(pamh, ret);

		return ucv_boolean_new(false);
	}

	pam_end(pamh, PAM_SUCCESS);

	ok_return(ucv_boolean_new(true));
}

/**
 * Возвращает описание последней ошибки/результата, либо `null` если
 * последний вызов authenticate() завершился успехом (`true`) без
 * ошибок инфраструктуры.
 *
 * @function module:pam#error
 *
 * @param {boolean} [numeric]
 * Если true — вернуть числовой код PAM (`PAM_*` константа) вместо
 * текстового описания.
 *
 * @returns {?(string|number)}
 */
static uc_value_t *
uc_pam_error(uc_vm_t *vm, size_t nargs)
{
	uc_value_t *numeric = uc_fn_arg(0);

	if (last_error.code == 0 && !last_error.msg)
		return NULL;

	if (ucv_is_truish(numeric))
		return ucv_int64_new(last_error.code);

	if (last_error.msg)
		return ucv_string_new(last_error.msg);

	return NULL;
}

static const uc_function_list_t pam_fns[] = {
	{ "authenticate", uc_pam_authenticate },
	{ "error",        uc_pam_error },
};

void uc_module_init(uc_vm_t *vm, uc_value_t *scope)
{
	uc_function_list_register(scope, pam_fns);
}
