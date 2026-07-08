#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dbus/dbus.h>

/* Defaults - overridable via argv */
static const char *g_target_app    = "app";
static const char *g_target_sender = "sender";
static const char *g_out_path      = NULL; /* NULL = stdout only */

static void write_match(const char *summary, const char *body) {
    time_t now = time(NULL);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));

    /* Always echo to stdout so systemd/journald captures it too */
    printf("[%s] %s: %s\n", timestamp, summary, body);
    fflush(stdout);

    if (g_out_path) {
        FILE *f = fopen(g_out_path, "a");
        if (f) {
            fprintf(f, "[%s] %s: %s\n", timestamp, summary, body);
            fclose(f);
        } else {
            fprintf(stderr, "Warning: could not open %s for append\n", g_out_path);
        }
    }
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

    if (dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_STRING) {
        dbus_message_iter_get_basic(&args, &app_name);
        dbus_message_iter_next(&args);
    }
    if (dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_UINT32) {
        dbus_message_iter_get_basic(&args, &replaces_id);
        dbus_message_iter_next(&args);
    }
    (void)replaces_id;
    if (dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_STRING) {
        dbus_message_iter_get_basic(&args, &app_icon);
        dbus_message_iter_next(&args);
    }
    (void)app_icon;
    if (dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_STRING) {
        dbus_message_iter_get_basic(&args, &summary);
        dbus_message_iter_next(&args);
    }
    if (dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_STRING) {
        dbus_message_iter_get_basic(&args, &body);
    }

    if (app_name && summary && body) {
        if (strcmp(app_name, g_target_app) == 0 && strcmp(summary, g_target_sender) == 0) {
            write_match(summary, body);
        }
    }
}

static void print_usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [--app APP_NAME] [--sender SENDER_NAME] [--out FILE]\n"
        "  --app     D-Bus app_name to match (default: %s)\n"
        "  --sender  Notification summary/title to match (default: %s)\n"
        "  --out     File to append matches to (default: stdout only)\n",
        prog, g_target_app, g_target_sender);
}

static void parse_args(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--app") == 0 && i + 1 < argc) {
            g_target_app = argv[++i];
        } else if (strcmp(argv[i], "--sender") == 0 && i + 1 < argc) {
            g_target_sender = argv[++i];
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

    fprintf(stderr, "notify_logger running (app=%s sender=%s out=%s)\n",
            g_target_app, g_target_sender, g_out_path ? g_out_path : "(stdout only)");

    while (dbus_connection_read_write_dispatch(conn, -1)) {
        DBusMessage *msg = dbus_connection_pop_message(conn);
        if (msg == NULL) continue;
        handle_message(msg);
        dbus_message_unref(msg);
    }

    return 0;
}
