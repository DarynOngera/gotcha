#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <libgen.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <dbus/dbus.h>

/* Defaults - overridable via argv */
static const char *g_target_app    = "";
static const char *g_target_body   = "";
static const char *g_out_path      = "./logs/matches.log";

#define FIELD_BUF_SIZE 4096
#define LINE_BUF_SIZE  (FIELD_BUF_SIZE * 2 + 64)
#define TRUNC_MARK     " [truncated]"

static int escape_field(const char *src, char *dst, size_t dst_size) {
    size_t i = 0;
    int truncated = 0;
    for (const char *p = src; *p != '\0'; p++) {
        const char *repl = NULL;
        switch (*p) {
            case '\\': repl = "\\\\"; break;
            case '\n': repl = "\\n"; break;
            case '\r': repl = "\\r"; break;
            case '\t': repl = "\\t"; break;
        }
        size_t need = repl != NULL ? strlen(repl) : 1;
        if (i + need + 1 > dst_size) {
            truncated = 1;
            break;
        }
        if (repl != NULL) {
            memcpy(dst + i, repl, need);
            i += need;
        } else {
            dst[i++] = *p;
        }
    }
    dst[i] = '\0';
    return truncated;
}

static void write_match(const char *summary, const char *body) {
    time_t now = time(NULL);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));

    char esc_summary[FIELD_BUF_SIZE];
    char esc_body[FIELD_BUF_SIZE];
    int truncated = escape_field(summary, esc_summary, sizeof(esc_summary));
    truncated |= escape_field(body, esc_body, sizeof(esc_body));

    char line[LINE_BUF_SIZE];
    snprintf(line, sizeof(line), "[%s] %s: %s%s\n",
             timestamp, esc_summary, esc_body,
             truncated ? TRUNC_MARK : "");

    printf("%s", line);
    fflush(stdout);

    if (g_out_path != NULL) {
        FILE *f = fopen(g_out_path, "a");
        if (f != NULL) {
            fprintf(f, "%s", line);
            fclose(f);
        } else {
            fprintf(stderr, "Warning: could not open %s for append\n", g_out_path);
        }
    }
}

static int next_arg(DBusMessageIter *args, int expected_type, void *out) {
    if (dbus_message_iter_get_arg_type(args) != expected_type) return 0;
    dbus_message_iter_get_basic(args, out);
    dbus_message_iter_next(args);
    return 1;
}

static void handle_message(DBusMessage *msg) {
    if (!dbus_message_is_method_call(msg, "org.freedesktop.Notifications", "Notify"))
        return;

    DBusMessageIter args;
    const char *app_name = NULL;
    dbus_uint32_t replaces_id = 0;
    const char *app_icon = NULL;
    const char *summary = NULL;
    const char *body = NULL;

    if (!dbus_message_iter_init(msg, &args)) return;

    next_arg(&args, DBUS_TYPE_STRING, &app_name);
    next_arg(&args, DBUS_TYPE_UINT32, &replaces_id);
    (void)replaces_id;
    next_arg(&args, DBUS_TYPE_STRING, &app_icon);
    (void)app_icon;
    next_arg(&args, DBUS_TYPE_STRING, &summary);
    next_arg(&args, DBUS_TYPE_STRING, &body);

    if (app_name != NULL && summary != NULL && body != NULL) {
        if (strcmp(app_name, g_target_app) == 0 && strcmp(body, g_target_body) == 0) {
            write_match(summary, body);
        }
    }
}

static int mkdir_p(const char *path) {
    char *tmp = strdup(path);
    if (tmp == NULL) return -1;
    size_t len = strlen(tmp);
    if (len > 1 && tmp[len - 1] == '/') tmp[len - 1] = '\0';
    for (char *p = tmp + 1; *p != '\0'; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                free(tmp);
                return -1;
            }
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        free(tmp);
        return -1;
    }
    free(tmp);
    return 0;
}

static void ensure_out_dir(void) {
    if (g_out_path == NULL) return;

    char *path_copy = strdup(g_out_path);
    if (path_copy == NULL) {
        fprintf(stderr, "Error: out of memory\n");
        exit(1);
    }
    char *dir = dirname(path_copy);
    struct stat st;
    if (stat(dir, &st) == 0) {
        if (!S_ISDIR(st.st_mode)) {
            fprintf(stderr, "Error: output directory %s exists but is not a directory\n", dir);
            free(path_copy);
            exit(1);
        }
        free(path_copy);
        return;
    }

    char *path_copy2 = strdup(g_out_path);
    if (path_copy2 == NULL) {
        fprintf(stderr, "Error: out of memory\n");
        free(path_copy);
        exit(1);
    }
    char *dir2 = dirname(path_copy2);
    if (mkdir_p(dir2) != 0) {
        fprintf(stderr, "Error: could not create output directory %s: %s\n",
                dir2, strerror(errno));
        free(path_copy);
        free(path_copy2);
        exit(1);
    }
    free(path_copy2);

    if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "Error: could not verify output directory %s\n", dir);
        free(path_copy);
        exit(1);
    }
    free(path_copy);
}

static void print_usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [--app APP_NAME] [--body BODY_TEXT] [--out FILE]\n"
        "  --app     D-Bus app_name to match (default: %s)\n"
        "  --body    Notification body to match (default: %s)\n"
        "  --out     File to append matches to (default: stdout only)\n",
        prog, g_target_app, g_target_body);
}

static void parse_args(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--app") == 0 && i + 1 < argc) {
            g_target_app = argv[++i];
        } else if (strcmp(argv[i], "--body") == 0 && i + 1 < argc) {
            g_target_body = argv[++i];
        } else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            g_out_path = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            exit(0);
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            print_usage(argv[0]);
            exit(1);
        }
    }
}

int main(int argc, char **argv) {
    parse_args(argc, argv);
    ensure_out_dir();

    DBusConnection *conn;
    DBusError err;
    dbus_error_init(&err);

    conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (dbus_error_is_set(&err)) {
        fprintf(stderr, "Connection Error: %s\n", err.message);
        dbus_error_free(&err);
        return 1;
    }
    if (conn == NULL) {
        fprintf(stderr, "Failed to connect to session bus\n");
        return 1;
    }

    /* eavesdrop='true' is required on most modern dbus-daemon/dbus-broker
       policies for a non-owning process to observe Notify calls headed
       to the real notification daemon. Without it the match rule is
       often silently ignored. */
    const char *match_rule =
        "eavesdrop='true',type='method_call',"
        "interface='org.freedesktop.Notifications',member='Notify'";

    dbus_bus_add_match(conn, match_rule, &err);
    dbus_connection_flush(conn);
    if (dbus_error_is_set(&err)) {
        fprintf(stderr, "Match Error: %s\n", err.message);
        dbus_error_free(&err);
        return 1;
    }

    fprintf(stderr, "notify_logger running (app=%s body=%s out=%s)\n",
            g_target_app, g_target_body,
            g_out_path != NULL ? g_out_path : "(stdout only)");

    while (dbus_connection_read_write_dispatch(conn, -1)) {
        DBusMessage *msg = dbus_connection_pop_message(conn);
        if (msg == NULL) continue;
        handle_message(msg);
        dbus_message_unref(msg);
    }

    fprintf(stderr, "D-Bus connection lost; exiting\n");
    return 1;
}
