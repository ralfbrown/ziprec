/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  Version:  1.10beta		User Interface - Raw Xterm		*/
/*  LastEdit: 27jun2019							*/
/*									*/
/*  (c) Copyright 2012,2013,2019 Ralf Brown/CMU				*/
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

#ifndef UI_XTERM_H_INCLUDED
#define UI_XTERM_H_INCLUDED

#include "ui_common.h"
#include <termios.h>
#include "framepac/signal.h"
#include "framepac/smartptr.h"

/************************************************************************/
/************************************************************************/

class ZiprecUIXterm : public ZiprecUICommon
   {
   public:
      typedef Fr::Owned<Fr::SignalHandler> SigHandler ;
   public:
      ZiprecUIXterm() ;
      virtual ~ZiprecUIXterm() ;

      ZiprecUserInterface *instantiate() ;
      virtual bool run(const char *initial_file) ;

      bool clearScreen() ;
      bool clearLine() ;
      bool clearToEndOfLine() ;
      bool homeCursor() ;
      bool setCursor(unsigned row, unsigned column) ;
      bool displayChar(char c) ;
      bool displayText(const char *buf, unsigned len) ;
      bool displayString(const char *s) ;

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
      
   private:
      SigHandler     m_sigint ;
      SigHandler     m_sigill ;
      SigHandler     m_sigfpe ;
      SigHandler     m_sigwinch { nullptr } ;
      SigHandler     m_sighup { nullptr } ;
      SigHandler     m_sigpipe { nullptr } ;
      SigHandler     m_sigbus { nullptr } ;
      SigHandler     m_sigsegv { nullptr } ;
      struct termios m_term_state ;
      unsigned	     m_rows ;
      unsigned	     m_columns ;
      bool	     m_term_state_OK ;
   } ;

#endif /* !UI_XTERM_H_INCLUDED */

// end of file ui_xterm.h //
