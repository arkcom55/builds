/* config.h.  Generated from config.h.in by configure.  */
/* config.h.in.  Generated from configure.ac by autoheader.  */

/* remote control system (cli and http) */
#define ENABLE_CONTROL /**/

/* libpcap support for filters */
#define ENABLE_FILTERS /**/

/* OpenBSD's gcc has bounded */
/* #undef HAVE_ATTRIBUTE__BOUNDED__ */

/* OpenBSD's gcc has sentinel */
/* #undef HAVE_ATTRIBUTE__SENTINEL__ */

/* BSD */
/* #undef HAVE_BSD */

/* Define to 1 if you have the `closefrom' function. */
/* #undef HAVE_CLOSEFROM */

/* DARWIN */
/* #undef HAVE_DARWIN */

/* Define to 1 if you have the declaration of `res_init', and to 0 if you
   don't. */
#define HAVE_DECL_RES_INIT 1

/* Define to 1 if you have the <dirent.h> header file. */
#define HAVE_DIRENT_H 1

/* Define to 1 if you have the <ev.h> header file. */
#define HAVE_EV_H 1

/* Define to 1 if you have the <fcntl.h> header file. */
#define HAVE_FCNTL_H 1

/* filters enabled */
#define HAVE_FILTERS /**/

/* FreeBSD */
/* #undef HAVE_FREEBSD */

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* ev_time in -lev */
#define HAVE_LIBEV 1

/* Define to 1 if you have the `resolv' library (-lresolv). */
/* #undef HAVE_LIBRESOLV */

/* sodium_init in -lsodium */
/* #undef HAVE_LIBSODIUM */

/* Linux */
#define HAVE_LINUX 1

/* Define to 1 if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* Define to 1 if you have the <ndir.h> header file. */
/* #undef HAVE_NDIR_H */

/* NetBSD */
/* #undef HAVE_NETBSD */

/* OpenBSD */
/* #undef HAVE_OPENBSD */

/* Define to 1 if you have the <pcap.h> header file. */
#define HAVE_PCAP_H 1

/* Define to 1 if you have the `pledge' function. */
/* #undef HAVE_PLEDGE */

/* Define to 1 if you have the `setproctitle' function. */
/* #undef HAVE_SETPROCTITLE */

/* Define to 1 if you have the `setresgid' function. */
#define HAVE_SETRESGID 1

/* Define to 1 if you have the `setresuid' function. */
#define HAVE_SETRESUID 1

/* Define to 1 if you have the <sodium.h> header file. */
/* #undef HAVE_SODIUM_H */

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have the `strlcat' function. */
/* #undef HAVE_STRLCAT */

/* Define to 1 if you have the `strlcpy' function. */
/* #undef HAVE_STRLCPY */

/* Define to 1 if you have the `strnvis' function. */
/* #undef HAVE_STRNVIS */

/* Define to 1 if you have the `sysconf' function. */
#define HAVE_SYSCONF 1

/* Define to 1 if you have the <sys/dir.h> header file. */
#define HAVE_SYS_DIR_H 1

/* Define to 1 if you have the <sys/ndir.h> header file. */
/* #undef HAVE_SYS_NDIR_H */

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* Define to 1 if you have the <valgrind/valgrind.h> header file. */
/* #undef HAVE_VALGRIND_VALGRIND_H */

/* Define to 1 if you have the `vsnprintf' function. */
#define HAVE_VSNPRINTF 1

/* install example files */
#define INSTALL_EXAMPLES "yes"

/* compiler does not accept __attribute__ on return types */
/* #undef NO_ATTRIBUTE_ON_RETURN_TYPE */

/* Name of package */
#define PACKAGE "mlvpn"

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT "ed@zehome.com"

/* Define to the full name of this package. */
#define PACKAGE_NAME "mlvpn"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "mlvpn master-2263bab"

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME "mlvpn"

/* Define to the home page for this package. */
#define PACKAGE_URL ""

/* Define to the version of this package. */
#define PACKAGE_VERSION "master-2263bab"

/* Define to 1 if you have the ANSI C header files. */
#define STDC_HEADERS 1

/* Directory for systemd generators */
#define SYSTEMDSYSTEMGENERATORDIR "/lib/systemd/system-generators"

/* Directory for systemd service files */
#define SYSTEMDSYSTEMUNITDIR "/lib/systemd/system"

/* Directory for systemd tmpfiles */
#define SYSTEMDTMPFILESDIR "/usr/lib/tmpfiles.d"

/* Enable extensions on AIX 3, Interix.  */
#ifndef _ALL_SOURCE
# define _ALL_SOURCE 1
#endif
/* Enable GNU extensions on systems that have them.  */
#ifndef _GNU_SOURCE
# define _GNU_SOURCE 1
#endif
/* Enable threading extensions on Solaris.  */
#ifndef _POSIX_PTHREAD_SEMANTICS
# define _POSIX_PTHREAD_SEMANTICS 1
#endif
/* Enable extensions on HP NonStop.  */
#ifndef _TANDEM_SOURCE
# define _TANDEM_SOURCE 1
#endif
/* Enable general extensions on Solaris.  */
#ifndef __EXTENSIONS__
# define __EXTENSIONS__ 1
#endif


/* Version number of package */
#define VERSION "master-2263bab"

/* Define to 1 if on MINIX. */
/* #undef _MINIX */

/* Define to 2 if the system does not provide POSIX.1 features except with
   this defined. */
/* #undef _POSIX_1_SOURCE */

/* Define to 1 if you need to in order for `stat' and other things to work. */
/* #undef _POSIX_SOURCE */

/* Enable BSD extensions */
#define __USE_BSD 1
