/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  File: utility.C - utility functions					*/
/*  Version:  1.10beta				       			*/
/*  LastEdit: 2019-07-25						*/
/*									*/
/*  (c) Copyright 2011,2012,2013,2019 Ralf Brown/CMU			*/
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

#include <ctype.h>
#include <cstdio>
#include <cstring>
#include <unistd.h>

using namespace std ;

#include "utility.h"
#include "global.h"

#if defined(__MSDOS__) || defined(__WATCOMC__) || defined(_MSC_VER)
#  include <direct.h> // for mkdir()
#elif defined(unix)
#  include <sys/stat.h>  // for mkdir()
#endif

using namespace Fr ;

/************************************************************************/
/*	Utility functions and macros					*/
/************************************************************************/

bool ask_yes_no(const char *prompt)
{
   char response[5000] ;
   bool said_yes = false ;
   bool valid = false ;
   do {
      fprintf(stderr,"%s: ", prompt) ;
      if (fgets(response,sizeof(response),stdin))
	 {
	 char *ptr = response ;
	 while (*ptr && isspace(*ptr))
	    ptr++ ;
	 char c = toupper(*ptr) ;
	 if (c == 'Y')
	    {
	    said_yes = true ;
	    valid = true ;
	    }
	 else if (c == 'N')
	    {
	    said_yes = false ;
	    valid = true ;
	    }
	 else
	    {
	    fprintf(stderr,"\nPlease answer Yes or No.\n") ;
	    }
	 }
      } while (!valid) ;
   fprintf(stderr,"\n") ;
   return said_yes ;
}

//----------------------------------------------------------------------

CFile safely_open_for_write(const char *filename, bool reading_stdin, bool force_overwrite)
{
   COutputFile f(filename,(force_overwrite?CFile::default_options:CFile::fail_if_exists)|CFile::binary,
      reading_stdin?nullptr:CFile::askOverwrite) ;
   return f ;
}

// end of file utility.C //
