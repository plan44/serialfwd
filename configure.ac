AC_INIT([serialfwd], [0.1], [luz@plan44.ch], [serialfwd], [http://www.plan44.ch/])
AC_PREREQ([2.59])

AC_CONFIG_AUX_DIR(configure_aux)
# AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([1.10 no-define foreign])
AC_CONFIG_HEADERS([config.h])

AC_PROG_CXX

AC_CONFIG_FILES([Makefile])

PKG_PROG_PKG_CONFIG

# Checks for header files.
AC_CHECK_HEADERS([stddef.h stdint.h stdlib.h string.h sys/ioctl.h unistd.h], [], [AC_MSG_ERROR([required system header not found])])

AC_OUTPUT
