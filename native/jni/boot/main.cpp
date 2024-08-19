#include <mincrypt/sha.h>
#include <base.hpp>

#include "magiskboot.hpp"
#include "compress.hpp"

using namespace std;

static void print_formats() {
    for (int fmt = GZIP; fmt < LZOP; ++fmt) {
        fprintf(stderr, "%s ", fmt2name[(format_t) fmt]);
    }
}

/**
 * @param arg0
 * Magisk CPIO是一种由Topjohnwu开发的工具，主要用于Android设备上的系统分区修改，尤其是在root权限环境下进行系统级文件的更改。Magisk项目的核心在于它能够实现无痕root，即在bootloader锁定的情况下也能获得root权限，同时还能通过官方的安全更新检查。CPIO是其诸多功能之一，用于在系统中添加、删除或替换文件。

### CPIO简介

CPIO（Copy In / Copy Out）是一种用于文件存档和恢复的标准Unix工具。它可以把文件和目录结构打包成一个流，这流可以保存到磁盘上，也可以通过网络传输，甚至可以被解包并恢复到文件系统中。CPIO格式非常灵活，支持多种不同的文件元数据。

### Magisk CPIO的功能

在Magisk中，CPIO的功能主要体现在以下几个方面：

1. **系统文件修改**：Magisk CPIO允许用户在不完全重新刷写系统的情况下，向系统分区添加、删除或替换文件。这对于修改系统行为、安装模块或修复某些系统问题非常有用。

2. **无痕修改**：Magisk的CPIO修改能够在官方的安全检查中保持未被检测的状态，这是因为Magisk利用了动态挂载技术，使得系统文件看起来未被修改过。

3. **模块化**：Magisk支持模块化设计，用户可以通过安装各种Magisk模块来扩展设备的功能，而这些模块往往依赖于CPIO来实现其功能。

4. **恢复原状**：Magisk的CPIO修改是可逆的，用户可以在不需要的时候轻松恢复系统到修改前的状态，这提供了很高的灵活性和安全性。


 */

static void usage(char *arg0) {
    fprintf(stderr,
R"EOF(MagiskBoot - Boot Image Modification Tool

Usage: %s <action> [args...]

Supported actions:
  unpack [-n] [-h] <bootimg>
    Unpack <bootimg> to its individual components, each component to
    a file with its corresponding file name in the current directory.
    Supported components: kernel, kernel_dtb, ramdisk.cpio, second,
    dtb, extra, and recovery_dtbo.
    By default, each component will be automatically decompressed
    on-the-fly before writing to the output file.
    If '-n' is provided, all decompression operations will be skipped;
    each component will remain untouched, dumped in its original format.
    If '-h' is provided, the boot image header information will be
    dumped to the file 'header', which can be used to modify header
    configurations during repacking.
    Return values:
    0:valid    1:error    2:chromeos

  repack [-n] <origbootimg> [outbootimg]
    Repack boot image components using files from the current directory
    to [outbootimg], or 'new-boot.img' if not specified.
    <origbootimg> is the original boot image used to unpack the components.
    By default, each component will be automatically compressed using its
    corresponding format detected in <origbootimg>. If a component file
    in the current directory is already compressed, then no addition
    compression will be performed for that specific component.
    If '-n' is provided, all compression operations will be skipped.
    If env variable PATCHVBMETAFLAG is set to true, all disable flags in
    the boot image's vbmeta header will be set.

  hexpatch <file> <hexpattern1> <hexpattern2>
    Search <hexpattern1> in <file>, and replace it with <hexpattern2>

  cpio <incpio> [commands...]
    Do cpio commands to <incpio> (modifications are done in-place)
    Each command is a single argument, add quotes for each command.
    Supported commands:
      exists ENTRY
        Return 0 if ENTRY exists, else return 1
      rm [-r] ENTRY
        Remove ENTRY, specify [-r] to remove recursively
      mkdir MODE ENTRY
        Create directory ENTRY in permissions MODE
      ln TARGET ENTRY
        Create a symlink to TARGET with the name ENTRY
      mv SOURCE DEST
        Move SOURCE to DEST
      add MODE ENTRY INFILE
        Add INFILE as ENTRY in permissions MODE; replaces ENTRY if exists
      extract [ENTRY OUT]
        Extract ENTRY to OUT, or extract all entries to current directory
      test
        Test the cpio's status
        Return value is 0 or bitwise or-ed of following values:
        0x1:Magisk    0x2:unsupported    0x4:Sony
      patch
        Apply ramdisk patches
        Configure with env variables: KEEPVERITY KEEPFORCEENCRYPT
      backup ORIG
        Create ramdisk backups from ORIG
      restore
        Restore ramdisk from ramdisk backup stored within incpio
      sha1
        Print stock boot SHA1 if previously backed up in ramdisk

  dtb <file> <action> [args...]
    Do dtb related actions to <file>
    Supported actions:
      print [-f]
        Print all contents of dtb for debugging
        Specify [-f] to only print fstab nodes
      patch
        Search for fstab and remove verity/avb
        Modifications are done directly to the file in-place
        Configure with env variables: KEEPVERITY
      test
        Test the fstab's status
        Return values:
        0:valid    1:error

  split <file>
    Split image.*-dtb into kernel + kernel_dtb

  sha1 <file>
    Print the SHA1 checksum for <file>

  cleanup
    Cleanup the current working directory

  compress[=format] <infile> [outfile]
    Compress <infile> with [format] to [outfile].
    <infile>/[outfile] can be '-' to be STDIN/STDOUT.
    If [format] is not specified, then gzip will be used.
    If [outfile] is not specified, then <infile> will be replaced
    with another file suffixed with a matching file extension.
    Supported formats: )EOF", arg0);

    print_formats();

    fprintf(stderr, R"EOF(

  decompress <infile> [outfile]
    Detect format and decompress <infile> to [outfile].
    <infile>/[outfile] can be '-' to be STDIN/STDOUT.
    If [outfile] is not specified, then <infile> will be replaced
    with another file removing its archive format file extension.
    Supported formats: )EOF");

    print_formats();

    fprintf(stderr, "\n\n");
    exit(1);
}

int main(int argc, char *argv[]) {
    cmdline_logging();
    umask(0);

    if (argc < 2)
        usage(argv[0]);

    // Skip '--' for backwards compatibility
    string_view action(argv[1]);
    if (str_starts(action, "--"))
        action = argv[1] + 2;

    if (action == "cleanup") {
        fprintf(stderr, "Cleaning up...\n");
        unlink(HEADER_FILE);
        unlink(KERNEL_FILE);
        unlink(RAMDISK_FILE);
        unlink(SECOND_FILE);
        unlink(KER_DTB_FILE);
        unlink(EXTRA_FILE);
        unlink(RECV_DTBO_FILE);
        unlink(DTB_FILE);
    } else if (argc > 2 && action == "sha1") {
        uint8_t sha1[SHA_DIGEST_SIZE];
        auto m = mmap_data(argv[2]);
        SHA_hash(m.buf, m.sz, sha1);
        for (uint8_t i : sha1)
            printf("%02x", i);
        printf("\n");
    } else if (argc > 2 && action == "split") {
        return split_image_dtb(argv[2]);
    } else if (argc > 2 && action == "unpack") {
        int idx = 2;
        bool nodecomp = false;
        bool hdr = false;
        for (;;) {
            if (idx >= argc)
                usage(argv[0]);
            if (argv[idx][0] != '-')
                break;
            for (char *flag = &argv[idx][1]; *flag; ++flag) {
                if (*flag == 'n')
                    nodecomp = true;
                else if (*flag == 'h')
                    hdr = true;
                else
                    usage(argv[0]);
            }
            ++idx;
        }
        return unpack(argv[idx], nodecomp, hdr);
    } else if (argc > 2 && action == "repack") {
        if (argv[2] == "-n"sv) {
            if (argc == 3)
                usage(argv[0]);
            repack(argv[3], argv[4] ? argv[4] : NEW_BOOT, true);
        } else {
            repack(argv[2], argv[3] ? argv[3] : NEW_BOOT);
        }
    } else if (argc > 2 && action == "decompress") {
        decompress(argv[2], argv[3]);
    } else if (argc > 2 && str_starts(action, "compress")) {
        compress(action[8] == '=' ? &action[9] : "gzip", argv[2], argv[3]);
    } else if (argc > 4 && action == "hexpatch") {
        return hexpatch(argv[2], argv[3], argv[4]);
    } else if (argc > 2 && action == "cpio"sv) {
        if (cpio_commands(argc - 2, argv + 2))
            usage(argv[0]);
    } else if (argc > 3 && action == "dtb") {
        if (dtb_commands(argc - 2, argv + 2))
            usage(argv[0]);
    } else {
        usage(argv[0]);
    }

    return 0;
}
