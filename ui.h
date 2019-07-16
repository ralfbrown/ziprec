/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  Version:  1.00gamma		User Interface	       			*/
/*  LastEdit: 26apr2013							*/
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

class ZiprecUserInterface
   {
   private:
      static unsigned s_force_interface ;
   public:
      ZiprecUserInterface() ;
      virtual ~ZiprecUserInterface() ;

      bool loadConfig(const char *cfgfile) ;
      bool selectInterfaceType(const char *iface) ;

      ZiprecUserInterface *instantiate() ;
      virtual bool run(const char *initial_file) ;
   } ;


// end of file ui.h //
