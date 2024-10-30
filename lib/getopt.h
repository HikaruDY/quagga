/* Declarations for getopt.
   Copyright (C) 1989,90,91,92,93,94,96,97 Free Software Foundation, Inc.

   NOTE: The canonical source of this file is maintained with the GNU C Library.
   Bugs can be reported to bug-glibc@gnu.org.

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 2, or (at your option) any
   later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307,
   USA.  */

#ifndef _GETOPT_H
#define _GETOPT_H 1

/*
 * The operating system may or may not provide getopt_long(), and if
 * so it may or may not be a version we are willing to use.  Our
 * strategy is to declare getopt here, and then provide code unless
 * the supplied version is adequate.  The difficult case is when a
 * declaration for getopt is provided, as our declaration must match.
 *
 * XXX Arguably this version should be named differently, and the
 * local names defined to refer to the system version when we choose
 * to use the system version.
 */

#ifdef __cplusplus
extern "C" {
#endif

	/* For communication from `getopt' to the caller.
   When `getopt' finds an option that takes an argument,
   the argument value is returned here.
   Also, when `ordering' is RETURN_IN_ORDER,
   each non-option ARGV-element is returned here.  */

	extern char *optarg;

	/* Index in ARGV of the next element to be scanned.
   This is used for communication to and from the caller
   and for communication between successive calls to `getopt'.

   On entry to `getopt', zero means this is the first call; initialize.

   When `getopt' returns -1, this is the index of the first of the
   non-option elements that the caller should itself scan.

   Otherwise, `optind' communicates from one call to the next
   how much of ARGV has been scanned so far.  */

	extern int optind;

	/* Callers store zero here to inhibit the error message `getopt' prints
   for unrecognized options.  */

	extern int opterr;

	/* Set to an option character which was unrecognized.  */

	extern int optopt;

	/* Describe the long-named options requested by the application.
   The LONG_OPTIONS argument to getopt_long or getopt_long_only is a vector
   of `struct option' terminated by an element containing a name which is
   zero.

   The field `has_arg' is:
   no_argument		(or 0) if the option does not take an argument,
   required_argument	(or 1) if the option requires an argument,
   optional_argument 	(or 2) if the option takes an optional argument.

   If the field `flag' is not NULL, it points to a variable that is set
   to the value given in the field `val' when the option is found, but
   left unchanged if the option is not found.

   To have a long-named option do something other than set an `int' to
   a compiled-in constant, such as set a value from `optarg', set the
   option's `flag' field to zero and its `val' field to a nonzero
   value (the equivalent single-letter option character, if there is
   one).  For long options that have a zero `flag' field, `getopt'
   returns the contents of the `val' field.  */

	struct option {
#if defined(__STDC__) && __STDC__
		const char *name;
#else
	char *name;
#endif
		/* has_arg can't be an enum because some compilers complain about
     type mismatches in all the code that assumes it is an int.  */
		int has_arg;
		int *flag;
		int val;
	};

	/* Names for the values of the `has_arg' field of `struct option'.  */

#define no_argument 0
#define required_argument 1
#define optional_argument 2

#if defined(__STDC__) && __STDC__

	#if REALLY_NEED_PLAIN_GETOPT

		/*
 * getopt is defined in POSIX.2.  Assume that if the system defines
 * getopt that it complies with POSIX.2.  If not, an autoconf test
 * should be written to define NONPOSIX_GETOPT_DEFINITION.
 */
		#ifndef NONPOSIX_GETOPT_DEFINITION
	extern int getopt(int argc, char *const *argv, const char *shortopts);
		#else  /* NONPOSIX_GETOPT_DEFINITION */
	extern int getopt(void);
		#endif /* NONPOSIX_GETOPT_DEFINITION */

	#endif

	extern int getopt_long(int argc, char *const *argv, const char *shortopts, const struct option *longopts, int *longind);
	extern int getopt_long_only(int argc, char *const *argv, const char *shortopts, const struct option *longopts, int *longind);

	/* Internal only.  Users should not call this directly.  */
	extern int _getopt_internal(int argc, char *const *argv, const char *shortopts, const struct option *longopts, int *longind, int long_only);
#else /* not __STDC__ */

	#ifdef REALLY_NEED_PLAIN_GETOPT
extern int getopt();
	#endif /* REALLY_NEED_PLAIN_GETOPT */

extern int getopt_long();
extern int getopt_long_only();

extern int _getopt_internal();

#endif /* __STDC__ */

#ifdef __cplusplus
}
#endif

#endif /* getopt.h */
