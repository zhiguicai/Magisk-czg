/*
 * Copyright 2017 - 2021, John Wu (@topjohnwu)
 * Copyright 2015, Pierre-Hugues Husson <phh@phh.me>
 * Copyright 2010, Adam Shanks (@ChainsDD)
 * Copyright 2008, Zinx Verituse (@zinxv)
 */

#include <unistd.h>
#include <getopt.h>
#include <fcntl.h>
#include <pwd.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <magisk.hpp>
#include <base.hpp>
#include <flags.h>

#include "su.hpp"
#include "pts.hpp"

int quit_signals[] = { SIGALRM, SIGABRT, SIGHUP, SIGPIPE, SIGQUIT, SIGTERM, SIGINT, 0 };

static void usage(int status) {
    FILE *stream = (status == EXIT_SUCCESS) ? stdout : stderr;

    fprintf(stream,
    "MagiskSU\n\n"
    "Usage: su [options] [-] [user [argument...]]\n\n"
    "Options:\n"
    "  -c, --command COMMAND         pass COMMAND to the invoked shell\n"
    "  -h, --help                    display this help message and exit\n"
    "  -, -l, --login                pretend the shell to be a login shell\n"
    "  -m, -p,\n"
    "  --preserve-environment        preserve the entire environment\n"
    "  -s, --shell SHELL             use SHELL instead of the default " DEFAULT_SHELL "\n"
    "  -v, --version                 display version number and exit\n"
    "  -V                            display version code and exit\n"
    "  -mm, -M,\n"
    "  --mount-master                force run in the global mount namespace\n\n");
    exit(status);
}

static void sighandler(int sig) {
    restore_stdin();

    // Assume we'll only be called before death
    // See note before sigaction() in set_stdin_raw()
    //
    // Now, close all standard I/O to cause the pumps
    // to exit so we can continue and retrieve the exit
    // code
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    // Put back all the default handlers
    struct sigaction act;

    memset(&act, 0, sizeof(act));
    act.sa_handler = SIG_DFL;
    for (int i = 0; quit_signals[i]; ++i) {
        sigaction(quit_signals[i], &act, nullptr);
    }
}

static void setup_sighandlers(void (*handler)(int)) {
    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_handler = handler;
    for (int i = 0; quit_signals[i]; ++i) {
        sigaction(quit_signals[i], &act, nullptr);
    }
}
/**
 *
 * @param argc
 * @param argv
 * @return
 *
 * 这段代码是C语言编写的一部分程序，看起来像是一个客户端与守护进程通信以执行超级用户权限操作的主函数。这里，我们看到的是一个名为`su_client_main`的函数，它处理命令行参数并设置与守护进程的通信，以请求超级用户权限执行特定命令。以下是代码的详细解释：
### 选项解析
首先，定义了一个`long_opts`数组，其中包含了多个长选项和对应的短选项映射。这些选项允许用户指定不同的行为，例如执行命令、登录、保持环境变量、指定shell等。`getopt_long`函数用于解析命令行参数，将它们映射到上述定义的选项上。
### 参数处理
- 在`getopt_long`调用前，有一段代码用于替换某些命令行参数，如`-cn`替换为`-z`，`-mm`替换为`-M`，以支持`getopt_long`的解析。
- 使用`while`循环遍历所有选项，根据用户的输入更新`su_req`结构体的成员变量。例如，如果用户指定了`-c`选项，则收集从该选项开始到结束的所有参数作为命令；如果指定了`-s`选项，则记录用户指定的shell。
### 客户端与守护进程通信
- 建立与守护进程的连接，使用`connect_daemon`函数，传递`MainRequest::SUPERUSER`作为参数，这表明客户端需要超级用户权限。
- 使用`xwrite`函数将`su_req`结构体发送给守护进程，然后分别发送shell和命令字符串。
- 等待守护进程的确认，如果守护进程拒绝请求（通常是因为权限问题），则立即返回错误码`EACCES`。
### 终端检测与文件描述符转发
- 检查`stdin`、`stdout`和`stderr`是否连接到终端（TTY）。如果是，则在与守护进程通信时需要特殊处理。
- 根据终端是否存在，决定是否需要一个伪终端（PTY）。如果有终端连接，则创建一个PTY，并通过守护进程传递文件描述符。
- 设置信号处理器和异步监控，用于处理窗口大小变化和输入输出流的数据传输。
### 结束处理
- 从守护进程读取退出状态码，并关闭与守护进程的连接。
- 返回从守护进程中接收到的退出状态码，这将作为此函数的返回值。
整个流程展示了如何解析命令行参数、与守护进程通信以及处理终端相关的细节，最终执行一个需要超级用户权限的命令。这在root权限管理、系统管理工具等方面非常常见。
 */
int su_client_main(int argc, char *argv[]) {
    int c;
    struct option long_opts[] = {
            { "command",                required_argument,  nullptr, 'c' },
            { "help",                   no_argument,        nullptr, 'h' },
            { "login",                  no_argument,        nullptr, 'l' },
            { "preserve-environment",   no_argument,        nullptr, 'p' },
            { "shell",                  required_argument,  nullptr, 's' },
            { "version",                no_argument,        nullptr, 'v' },
            { "context",                required_argument,  nullptr, 'z' },
            { "mount-master",           no_argument,        nullptr, 'M' },
            { nullptr, 0, nullptr, 0 },
    };

    su_request su_req;

    for (int i = 0; i < argc; i++) {
        // Replace -cn with -z, -mm with -M for supporting getopt_long
        if (strcmp(argv[i], "-cn") == 0)
            strcpy(argv[i], "-z");
        else if (strcmp(argv[i], "-mm") == 0)
            strcpy(argv[i], "-M");
    }

    while ((c = getopt_long(argc, argv, "c:hlmps:Vvuz:M", long_opts, nullptr)) != -1) {
        switch (c) {
            case 'c':
                for (int i = optind - 1; i < argc; ++i) {
                    if (!su_req.command.empty())
                        su_req.command += ' ';
                    su_req.command += argv[i];
                }
                optind = argc;
                break;
            case 'h':
                usage(EXIT_SUCCESS);
                break;
            case 'l':
                su_req.login = true;
                break;
            case 'm':
            case 'p':
                su_req.keepenv = true;
                break;
            case 's':
                su_req.shell = optarg;
                break;
            case 'V':
                printf("%d\n", MAGISK_VER_CODE);
                exit(EXIT_SUCCESS);
            case 'v':
                printf("%s\n", MAGISK_VERSION ":MAGISKSU");
                exit(EXIT_SUCCESS);
            case 'z':
                // Do nothing, placed here for legacy support :)
                break;
            case 'M':
                su_req.mount_master = true;
                break;
            default:
                /* Bionic getopt_long doesn't terminate its error output by newline */
                fprintf(stderr, "\n");
                usage(2);
        }
    }

    if (optind < argc && strcmp(argv[optind], "-") == 0) {
        su_req.login = true;
        optind++;
    }
    /* username or uid */
    if (optind < argc) {
        struct passwd *pw;
        pw = getpwnam(argv[optind]);
        if (pw)
            su_req.uid = pw->pw_uid;
        else
            su_req.uid = parse_int(argv[optind]);
        optind++;
    }

    int ptmx, fd;

    // Connect to client
    fd = connect_daemon(MainRequest::SUPERUSER);

    // Send su_request
    xwrite(fd, &su_req, sizeof(su_req_base));
    write_string(fd, su_req.shell);
    write_string(fd, su_req.command);

    // Wait for ack from daemon
    if (read_int(fd)) {
        // Fast fail
        fprintf(stderr, "%s\n", strerror(EACCES));
        return EACCES;
    }

    // Determine which one of our streams are attached to a TTY
    int atty = 0;
    if (isatty(STDIN_FILENO))  atty |= ATTY_IN;
    if (isatty(STDOUT_FILENO)) atty |= ATTY_OUT;
    if (isatty(STDERR_FILENO)) atty |= ATTY_ERR;

    // Send stdin
    send_fd(fd, (atty & ATTY_IN) ? -1 : STDIN_FILENO);
    // Send stdout
    send_fd(fd, (atty & ATTY_OUT) ? -1 : STDOUT_FILENO);
    // Send stderr
    send_fd(fd, (atty & ATTY_ERR) ? -1 : STDERR_FILENO);

    if (atty) {
        // We need a PTY. Get one.
        write_int(fd, 1);
        ptmx = recv_fd(fd);
    } else {
        write_int(fd, 0);
    }

    if (atty) {
        setup_sighandlers(sighandler);
        watch_sigwinch_async(STDOUT_FILENO, ptmx);
        pump_stdin_async(ptmx);
        pump_stdout_blocking(ptmx);
    }

    // Get the exit code
    int code = read_int(fd);
    close(fd);

    return code;
}
