/************************************************************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  Version:  1.00beta		User Interface	       			*/
/*  LastEdit: 04feb2013							*/
/*									*/
/*  (c) Copyright 2012,2013 Ralf Brown/CMU				*/
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

#include <cstdio>
#include <cstdlib>

using namespace std ;

#include "global.h"
#include "ui.h"

/************************************************************************/
/************************************************************************/

static void usage(const char *argv0)
{
   fprintf(stderr,"ZipRecover UI v" VERSION ": interactively improve recovered data\n") ;
   fprintf(stderr,"  Copyright 2012 Ralf Brown/Carnegie Mellon University -- GNU GPLv3\n\n") ;
   fprintf(stderr,"Usage: %s [options] recovery-file\n",argv0) ;
//FIXME
   exit(1) ;
}

//----------------------------------------------------------------------

/************************************************************************/
/************************************************************************/

int main(int argc, char **argv)
{
   const char *argv0 = argv[0] ;
   const char *cfgfile = 0 ;
   const char *iface_type = 0 ;

   while (argc > 1 && argv[1][0] == '-' && argv[1][1] != '\0')
      {
      switch (argv[1][1])
	 {


	 case 'h':
	 default:
	    usage(argv0) ;
	    return 1 ;
	 }
      argv++ ;
      argc-- ;
      }
   const char *filename = argv[1] ;
   ZiprecUserInterface ui_factory ;
   if (ui_factory.loadConfig(cfgfile))
      {
      if (!iface_type || ui_factory.selectInterfaceType(iface_type))
	 {
	 ZiprecUserInterface *ui = ui_factory.instantiate() ;
	 if (ui)
	    ui->run(filename) ;
	 else
	    {
	    }
	 }
      else
	 {

	 }
      }
   else
      {

      }
   return 0 ;
}

// end of file ziprecui.C //
