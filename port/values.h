/* values.h - ausente en Windows/macOS modernos; solo lo que usa el codigo original de Doom */
#ifndef DOOM_VALUES_H
#define DOOM_VALUES_H
#include <limits.h>
#ifndef MAXCHAR
#define MAXCHAR  ((char)SCHAR_MAX)
#define MINCHAR  ((char)SCHAR_MIN)
#define MAXSHORT SHRT_MAX
#define MINSHORT SHRT_MIN
#define MAXINT   INT_MAX
#define MININT   INT_MIN
#define MAXLONG  LONG_MAX
#define MINLONG  LONG_MIN
#endif
#endif
