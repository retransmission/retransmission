/*
 * Hand-authored libarchive configuration for the macOS (Darwin) Xcode build.
 *
 * The CMake builds (Linux/Windows/macOS-via-CMake) generate their own per-platform
 * config.h through libarchive's feature detection; this file is used ONLY by the
 * Transmission.xcodeproj "archive" target, which compiles libarchive directly and
 * sets GCC_PREPROCESSOR_DEFINITIONS = HAVE_CONFIG_H=1 with this directory on the
 * header search path.
 *
 * It mirrors libarchive's shipped libarchive/config_freebsd.h (a supported,
 * hand-built static config), trimmed to the same minimal feature set the CMake
 * build enables — formats tar/zip/raw, filter gzip via the system zlib — with the
 * FreeBSD ACL/extattr/libmd/bzip2/lzma pieces removed and Darwin struct-stat
 * members added. Everything compression/crypto/xml/acl/xattr-related is left
 * UNDEFINED on purpose; libarchive's optional-library #includes are all HAVE_*
 * guarded, so the format/filter sources still compile and link cleanly.
 *
 * NOTE: do NOT define HAVE_FUTIMENS / HAVE_UTIMENSAT here — archive_platform.h's
 * __APPLE__ block sets those from the deployment target.
 */

#define __LIBARCHIVE_CONFIG_H_INCLUDED 1

/* --- integer types / limits (Darwin has <stdint.h>/<inttypes.h>) --- */
#define HAVE_INTMAX_T 1
#define HAVE_UINTMAX_T 1
#define HAVE_INTTYPES_H 1
#define HAVE_STDINT_H 1
#define HAVE_WCHAR_T 1
#define HAVE_LONG_LONG_INT 1
#define HAVE_UNSIGNED_LONG_LONG 1
#define HAVE_UNSIGNED_LONG_LONG_INT 1
#define HAVE_DECL_INT32_MAX 1
#define HAVE_DECL_INT32_MIN 1
#define HAVE_DECL_INT64_MAX 1
#define HAVE_DECL_INT64_MIN 1
#define HAVE_DECL_INTMAX_MAX 1
#define HAVE_DECL_INTMAX_MIN 1
#define HAVE_DECL_SIZE_MAX 1
#define HAVE_DECL_SSIZE_MAX 1
#define HAVE_DECL_UINT32_MAX 1
#define HAVE_DECL_UINT64_MAX 1
#define HAVE_DECL_UINTMAX_MAX 1

/* --- headers --- */
#define HAVE_CTYPE_H 1
#define HAVE_DIRENT_H 1
#define HAVE_DLFCN_H 1
#define HAVE_ERRNO_H 1
#define HAVE_FCNTL_H 1
#define HAVE_FNMATCH_H 1
#define HAVE_GRP_H 1
#define HAVE_LANGINFO_H 1
#define HAVE_LIMITS_H 1
#define HAVE_LOCALE_H 1
#define HAVE_MEMORY_H 1
#define HAVE_PATHS_H 1
#define HAVE_POLL_H 1
#define HAVE_PTHREAD_H 1
#define HAVE_PWD_H 1
#define HAVE_READPASSPHRASE_H 1
#define HAVE_REGEX_H 1
#define HAVE_SIGNAL_H 1
#define HAVE_SPAWN_H 1
#define HAVE_STDARG_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRINGS_H 1
#define HAVE_STRING_H 1
#define HAVE_SYS_CDEFS_H 1
#define HAVE_SYS_IOCTL_H 1
#define HAVE_SYS_MOUNT_H 1
#define HAVE_SYS_PARAM_H 1
#define HAVE_SYS_POLL_H 1
#define HAVE_SYS_SELECT_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_STATVFS_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_UTSNAME_H 1
#define HAVE_SYS_WAIT_H 1
#define HAVE_TIME_H 1
#define HAVE_UNISTD_H 1
#define HAVE_UTIME_H 1
#define HAVE_WCHAR_H 1
#define HAVE_WCTYPE_H 1

/* --- zlib (the one load-bearing feature: in-process gzip + zip DEFLATE/crc32) --- */
#define HAVE_ZLIB_H 1
#define HAVE_LIBZ 1

/* --- functions --- */
#define HAVE_ARC4RANDOM_BUF 1
#define HAVE_CHFLAGS 1
#define HAVE_CHOWN 1
#define HAVE_CHROOT 1
#define HAVE_CTIME_R 1
#define HAVE_FCHDIR 1
#define HAVE_FCHFLAGS 1
#define HAVE_FCHMOD 1
#define HAVE_FCHOWN 1
#define HAVE_FCNTL 1
#define HAVE_FDOPENDIR 1
#define HAVE_FORK 1
#define HAVE_FSEEKO 1
#define HAVE_FSTAT 1
#define HAVE_FSTATAT 1
#define HAVE_FSTATFS 1
#define HAVE_FSTATVFS 1
#define HAVE_FTRUNCATE 1
#define HAVE_FUTIMES 1
#define HAVE_GETEUID 1
#define HAVE_GETGRGID_R 1
#define HAVE_GETGRNAM_R 1
#define HAVE_GETLINE 1
#define HAVE_GETPID 1
#define HAVE_GETPWNAM_R 1
#define HAVE_GETPWUID_R 1
#define HAVE_GMTIME_R 1
#define HAVE_LCHFLAGS 1
#define HAVE_LCHMOD 1
#define HAVE_LCHOWN 1
#define HAVE_LINK 1
#define HAVE_LINKAT 1
#define HAVE_LOCALTIME_R 1
#define HAVE_LSTAT 1
#define HAVE_LUTIMES 1
#define HAVE_MBRTOWC 1
#define HAVE_MEMMOVE 1
#define HAVE_MEMSET 1
#define HAVE_MKDIR 1
#define HAVE_MKFIFO 1
#define HAVE_MKNOD 1
#define HAVE_MKSTEMP 1
#define HAVE_NL_LANGINFO 1
#define HAVE_OPENAT 1
#define HAVE_PIPE 1
#define HAVE_POLL 1
#define HAVE_POSIX_SPAWNP 1
#define HAVE_READLINK 1
#define HAVE_READLINKAT 1
#define HAVE_READPASSPHRASE 1
#define HAVE_SELECT 1
#define HAVE_SETENV 1
#define HAVE_SETLOCALE 1
#define HAVE_SIGACTION 1
#define HAVE_STATFS 1
#define HAVE_STATVFS 1
#define HAVE_STRCHR 1
#define HAVE_STRDUP 1
#define HAVE_STRERROR 1
#define HAVE_STRERROR_R 1
#define HAVE_DECL_STRERROR_R 1
#define HAVE_STRFTIME 1
#define HAVE_STRNLEN 1
#define HAVE_STRRCHR 1
#define HAVE_SYMLINK 1
#define HAVE_SYSCONF 1
#define HAVE_TCGETATTR 1
#define HAVE_TCSETATTR 1
#define HAVE_TIMEGM 1
#define HAVE_TZSET 1
#define HAVE_UNLINKAT 1
#define HAVE_UNSETENV 1
#define HAVE_UTIME 1
#define HAVE_UTIMES 1
#define HAVE_VFORK 1
#define HAVE_VPRINTF 1
#define HAVE_WCRTOMB 1
#define HAVE_WCSCMP 1
#define HAVE_WCSCPY 1
#define HAVE_WCSLEN 1
#define HAVE_WCTOMB 1
#define HAVE_WMEMCMP 1
#define HAVE_WMEMCPY 1
#define HAVE_WMEMMOVE 1

/* --- Darwin struct members / errno values --- */
#define HAVE_EFTYPE 1
#define HAVE_EILSEQ 1
#define HAVE_STRUCT_STAT_ST_BIRTHTIME 1
#define HAVE_STRUCT_STAT_ST_BIRTHTIMESPEC_TV_NSEC 1
#define HAVE_STRUCT_STAT_ST_BLKSIZE 1
#define HAVE_STRUCT_STAT_ST_FLAGS 1
#define HAVE_STRUCT_STAT_ST_MTIMESPEC_TV_NSEC 1
#define HAVE_STRUCT_STATFS_F_IOSIZE 1
#define HAVE_STRUCT_TM_TM_GMTOFF 1

/*
 * Deliberately left UNDEFINED (keeps the build minimal):
 *   compression : HAVE_BZLIB_H, HAVE_LZMA_H/HAVE_LIBLZMA, HAVE_ZSTD_H, HAVE_LZ4_H,
 *                 HAVE_LZO_*   (only zlib is enabled)
 *   crypto      : HAVE_LIBCRYPTO/OPENSSL_*, NETTLE, MBEDTLS, LIBB2/BLAKE2 and all
 *                 ARCHIVE_CRYPTO_* backends (digest/hmac/cryptor degrade to stubs)
 *   xml (xar)   : HAVE_LIBXML_XMLREADER_H, HAVE_EXPAT_H, HAVE_BSDXML_H
 *   iconv/pcre  : HAVE_ICONV*, HAVE_LIBPCRE*, HAVE_PCRE*POSIX_H
 *   acl/xattr   : ARCHIVE_ACL_DARWIN, HAVE_SYS_ACL_H, ARCHIVE_XATTR_DARWIN,
 *                 HAVE_SYS_XATTR_H, HAVE_COPYFILE_H, HAVE_MEMBERSHIP_H
 *   device nums : MAJOR_IN_MKDEV, MAJOR_IN_SYSMACROS (Darwin: <sys/types.h>)
 *   non-Darwin  : HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC (Linux), HAVE_*EXTATTR* /
 *                 HAVE_GETVFSBYNAME / HAVE_STRUCT_XVFSCONF (FreeBSD), all HAVE_*WIN*,
 *                 STRERROR_R_CHAR_P (Darwin strerror_r is the XSI int flavor)
 */
