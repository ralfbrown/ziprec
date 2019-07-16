/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  Version:  1.00beta		User Interface - Curses			*/
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

#ifndef UI_CURSES_H_INCLUDED
#define UI_CURSES_H_INCLUDED

#include "ui_common.h"

/************************************************************************/
/************************************************************************/

class ZiprecUICurses : public ZiprecUICommon
   {
   private:

   public:
      ZiprecUICurses() ;
      virtual ~ZiprecUICurses() ;

      ZiprecUserInterface *instantiate() ;
      virtual bool run(const char *initial_file) ;

      // possible user commands to invoke from the event dispatcher
      virtual bool openFileCommand() ;
      virtual bool saveFileCommand() ;
      virtual bool saveFileAsCommand() ;
      virtual bool revertFileCommand() ;
      virtual bool markCorruption() ;
      virtual bool markCorruptionStart() ;
      virtual bool markCorruptionEnd() ;
      virtual bool setResyncCommand() ;
      virtual bool shiftResyncForward() ;
      virtual bool shiftResyncBackward() ;
      virtual bool exitCommand() ;
   } ;

#endif /* !UI_CURSES_H_INCLUDED */

// end of file ui_curses.h //
