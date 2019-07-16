/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  File: utility.C - utility functions					*/
/*  Version:  1.10beta				       			*/
/*  LastEdit: 27jun2019							*/
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

FILE *safely_open_for_write(const char *filename, bool reading_stdin,
			    bool force_overwrite)
{
   if (!filename || !*filename)
      return 0 ;
   if (access(filename,F_OK) == 0 && !force_overwrite)
      {
      // file already exists, so check whether OK to overwrite
      // if we are reading the archive from standard input, we can't ask the
      //   user, so always fail
      if (reading_stdin)
	 return 0 ;
      unsigned namelen = strlen(filename) ;
      char *prompt = new char[namelen + 60] ;
      strcpy(prompt,"File ") ;
      strcat(prompt,filename) ;
      strcat(prompt," exists.  Overwrite (Y/N)? ") ;
      bool allow = ask_yes_no(prompt) ;
      delete [] prompt ;
      if (!allow)
	 return 0 ;
      }
   return fopen(filename,"wb") ;
}

// end of file utility.C //
