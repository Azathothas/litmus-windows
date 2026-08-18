#!/bin/sh
set -e
printf 'aclocal... '
${ACLOCAL:-aclocal} -I neon/macros -I m4
printf 'autoheader... '
${AUTOHEADER:-autoheader} -Wall
printf 'autoconf... '
${AUTOCONF:-autoconf} -Wall
echo okay.
rm -rf autom4te*.cache
