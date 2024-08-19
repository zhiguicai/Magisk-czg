#include <libgen.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <magisk.hpp>
#include <selinux.hpp>
#include <base.hpp>

using namespace std;

using main_fun = int (*)(int, char *[]);

constexpr const char *applets[] = { "su", "resetprop", "zygisk", nullptr };
static main_fun applet_mains[] = { su_client_main, resetprop_main, zygisk_main, nullptr };

static int call_applet(int argc, char *argv[]) {
    // Applets
    string_view base = basename(argv[0]);  //使用basename函数获取调用程序的基础名称（不包含路径），然后遍历applets数组，寻找匹配的applet。
    for (int i = 0; applets[i]; ++i) {
        if (base == applets[i]) {
            return (*applet_mains[i])(argc, argv);
        }
    }
    fprintf(stderr, "%s: applet not found\n", base.data());
    return 1;
}

/**
 * 这段代码主要是一个多用途的可执行程序入口点，它能够根据执行时的名称和命令行参数，调用不同的子程序或“applets”。
 * @param argc
 * @param argv
 * @return
 * 这段代码主要是一个多用途的可执行程序入口点，它能够根据执行时的名称和命令行参数，调用不同的子程序或“applets”。这在Unix-like系统中是一种常见的设计模式，允许一个二进制文件根据调用方式的不同执行多种功能。下面是对代码的详细解析：


- 首先调用`enable_selinux()`和`cmdline_logging()`，可能用于初始化SELinux安全策略和启动日志记录。
- 初始化`argv0`，通常用于处理执行文件的完整路径。
- 获取程序基础名称并检查是否以`"app_process"`开头，如果是，则直接调用`app_process_main`。
- 使用`umask`函数重置文件创建掩码，确保文件权限的默认设置。
- 检查程序基础名称是否为`"magisk"`、`"magisk32"`或`"magisk64"`：
  - 如果第一个命令行参数不是以连字符`-`开头（即不是选项），则跳过第一个参数并将剩余参数传递给`call_applet`。
  - 否则，直接调用`magisk_main`。

### 参数传递示例

假设这个程序被命名为`magisk`，以下是一些可能的调用方式及其参数传递：

1. 直接调用`magisk`：
   ```
   ./magisk
   ```
   此时`argc`为1，`argv[0]`为`"./magisk"`，将直接调用`magisk_main`。

2. 调用`su` applet：
   ```
   ./magisk su -c 'ls -l'
   ```
   此时`argc`为4，`argv`数组依次为`"./magisk"`, `"su"`, `"-c"`, `'ls -l'`。因为`argv[1]`不是以`-`开头，所以实际传递给`su_client_main`的参数是`argc=3`，`argv[0]="su"`，`argv[1]="-c"`，`argv[2]="'ls -l'"`。

3. 调用`resetprop` applet：
   ```
   ./magisk resetprop
   ```
   此时`argc`为2，`argv[0]`为`"./magisk"`，`argv[1]`为`"resetprop"`。由于`argv[1]`是以连字符开头，实际调用`magisk_main`，不会进入`call_applet`。

这个设计允许用户通过同一可执行文件调用不同的功能，而无需维护多个独立的二进制文件，提高了软件包管理和分发的效率。
 */
int main(int argc, char *argv[]) {
    enable_selinux();
    cmdline_logging();
    init_argv0(argc, argv);

    string_view base = basename(argv[0]);

    // app_process is actually not an applet
    if (str_starts(base, "app_process")) {
        return app_process_main(argc, argv);
    }

    umask(0);
    if (base == "magisk" || base == "magisk32" || base == "magisk64") {
        if (argc > 1 && argv[1][0] != '-') {
            // Calling applet via magisk [applet] args
            --argc;
            ++argv;
        } else {
            return magisk_main(argc, argv);
        }
    }

    return call_applet(argc, argv);
}
