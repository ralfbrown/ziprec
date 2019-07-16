/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  File: utility.h - utility functions					*/
/*  Version:  1.00beta				       			*/
/*  LastEdit: 04feb2013							*/
/*									*/
/*  (c) Copyright 2011,2012,2013 Ralf Brown/CMU				*/
/*      This program is free software; you can redistribute it and/or   */
/*      modify it under the terms of the GNU General Public License as  */
/*      published by the Free Software Foundation, version 3.           */
/*                                                                      */
/*      This program is distributed in the hope that it will be         */
/*      useful, but WITHOUT ANY WARRANTY; without even the implied      */
/*      warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR         */
/*      PURPOSE.  See the GNU General Public License for more details.  */
/*                                                                      */
/*      You should have received a copy of the GNU General Public       */
/*      License (file COPYING) along with this program.  If not, see    */
/*      http://www.gnu.org/licenses/                                    */
/*                                                                      */
/************************************************************************/

#ifndef __UTILITY_H_INCLUDED
#define __UTILITY_H_INCLUDED

#include <cstdio>

bool ask_yes_no(const char *prompt) ;
FILE *safely_open_for_write(const char *filename, bool reading_stdin = false,
			    bool force_overwrite = false) ;


#endif /* !__UTILITY_H_INCLUDED */

// end of file utility.h //
