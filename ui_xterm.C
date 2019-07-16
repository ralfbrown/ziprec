/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  Version:  1.10beta		User Interface	       			*/
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

#include <sys/ioctl.h>
#include <termios.h>

#include "ui_xterm.h"
#include "framepac/texttransforms.h"

using namespace Fr ;

/************************************************************************/
/************************************************************************/

static struct termios original_terminal_state ;
static bool original_terminal_state_init = false ;

/************************************************************************/
/************************************************************************/

static bool init_terminal_modes(FILE *fp, struct termios *term_state,
				bool allow_bg_process = false)
{
   if (!fp || !term_state)
      return false ;
   int fd = fileno(fp) ;
   if (!isatty(fd))
      return false ;
   // get the current attributes
   if (!original_terminal_state_init)
      {
      if (tcgetattr(fd,&original_terminal_state) == 0)
	 original_terminal_state_init = true ;
      }
   if (tcgetattr(fd,term_state))
      return false ;
   // set terminal to raw, no echo, don't translate CR, ignore interrupts
   term_state->c_iflag &= ~(BRKINT | ICRNL | ISTRIP | IXON) ;
   term_state->c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG) ;
   term_state->c_cflag &= +(CSIZE | PARENB) ;
   term_state->c_cflag |= CS8 ;
   if (allow_bg_process)
      {
      // block for at most 0.5 seconds even if no bytes received, so that
      //   a background processing loop can be run
      term_state->c_cc[VMIN] = 0 ;
      term_state->c_cc[VTIME] = 5 ;
      }
   else
      {
      // block indefinitely for input, but return as soon as any bytes
      //   are available
      term_state->c_cc[VMIN] = 1 ;
      term_state->c_cc[VTIME] = 0 ;
      }
   if (tcsetattr(fd,TCSANOW,term_state))
      return false ;
   return true ;
}

//----------------------------------------------------------------------

static bool restore_terminal_modes(FILE *fp, const struct termios *term_state)
{
   if (!fp || !term_state)
      return false ;
   int fd = fileno(fp) ;
   if (tcsetattr(fd,TCSAFLUSH,term_state))
      return false ;
   return true ;
}

//----------------------------------------------------------------------

static void reset_terminal()
{
   if (original_terminal_state_init)
      restore_terminal_modes(stdin,&original_terminal_state) ;
   return ;
}

//----------------------------------------------------------------------

static bool get_window_size(unsigned &rows, unsigned &columns)
{
   struct winsize ws ;
   if (ioctl(STDIN_FILENO, TIOCGWINSZ, (char*)&ws))
      return false ;
   rows = ws.ws_row ;
   columns = ws.ws_col ;
   return true ;
}

//----------------------------------------------------------------------

static void sigint_handler(int)
{
//FIXME
   reset_terminal() ;
   return ;
}

//----------------------------------------------------------------------

static void sigill_handler(int)
{
//FIXME
   reset_terminal() ;
   return ;
}

//----------------------------------------------------------------------

static void sigwinch_handler(int)
{
   //get_window_size() ;
   return ;
}

//----------------------------------------------------------------------

static void sighup_handler(int)
{
//FIXME
   reset_terminal() ;
   return ;
}

//----------------------------------------------------------------------

static void sigpipe_handler(int)
{
//FIXME
   reset_terminal() ;
   return ;
}

//----------------------------------------------------------------------

static void sigbus_handler(int)
{
//FIXME
   reset_terminal() ;
   return ;
}

//----------------------------------------------------------------------

static void sigfpe_handler(int)
{
//FIXME
   reset_terminal() ;
   return ;
}

//----------------------------------------------------------------------

static void sigsegv_handler(int)
{
//FIXME
   reset_terminal() ;
   return ;
}

/************************************************************************/
/*	Methods for class ZiprecUIXterm					*/
/************************************************************************/

ZiprecUIXterm::ZiprecUIXterm()
{
   m_term_state_OK = init_terminal_modes(stdin,&m_term_state) ;
   get_window_size(m_rows,m_columns) ;
   m_sigint = new SignalHandler(SIGINT,sigint_handler) ;
   m_sigill = new SignalHandler(SIGILL,sigill_handler) ;
   m_sigfpe = new SignalHandler(SIGFPE,sigfpe_handler) ;
#ifdef SIGWINCH
   m_sigwinch = new SignalHandler(SIGWINCH,sigwinch_handler) ;
#else
   m_sigwinch = 0 ;
#endif /* SIGWINCH */
#ifdef SIGHUP
   m_sighup = new SignalHandler(SIGHUP,sighup_handler) ;
#else
   m_sighup = 0 ;
#endif
#ifdef SIGPIPE
   m_sigpipe = new SignalHandler(SIGPIPE,sigpipe_handler) ;
#else
   m_sigpipe = 0 ;
#endif /* SIGPIPE */
#ifdef SIGBUS
   m_sigbus = new SignalHandler(SIGBUS,sigbus_handler) ;
#else
   m_sigbus = 0 ;
#endif /* SIGBUS */
#ifdef SIGSEGV
   m_sigsegv = new SignalHandler(SIGSEGV,sigsegv_handler) ;
#else
   m_sigsegv = 0 ;
#endif /* SIGSEGV */
   return ;
}

//----------------------------------------------------------------------

ZiprecUIXterm::~ZiprecUIXterm()
{
   if (m_term_state_OK)
      restore_terminal_modes(stdin,&m_term_state) ;
   delete m_sigill ;
   delete m_sigfpe ;
   delete m_sigwinch ;
   delete m_sighup ;
   delete m_sigpipe ;
   delete m_sigbus ;
   delete m_sigsegv ;
   return ;
}

//----------------------------------------------------------------------

ZiprecUserInterface *ZiprecUIXterm::instantiate()
{
   return new ZiprecUIXterm ;
}

//----------------------------------------------------------------------

bool ZiprecUIXterm::clearScreen()
{
   displayText("\e[2J",4) ;
   return true ;
}

//----------------------------------------------------------------------

bool ZiprecUIXterm::clearLine()
{
   displayText("\e[2K",4) ;
   return true ;
}

//----------------------------------------------------------------------

bool ZiprecUIXterm::clearToEndOfLine()
{
   displayText("\e[K",3) ;
   return true ;
}

//----------------------------------------------------------------------

bool ZiprecUIXterm::homeCursor()
{
   displayText("\e[H",3) ;
   return true ;
}

//----------------------------------------------------------------------

bool ZiprecUIXterm::setCursor(unsigned row, unsigned col)
{
   auto ctrl = aprintf("\e[%u;%uH",row,col) ;
   displayString(ctrl) ;
   return true ;
}

//----------------------------------------------------------------------

bool ZiprecUIXterm::displayChar(char c)
{
   write(1,&c,1) ;
   return true ;
}

//----------------------------------------------------------------------

bool ZiprecUIXterm::displayText(const char *buf, unsigned len)
{
   write(1,buf,len) ;
   return true ;
}

//----------------------------------------------------------------------

bool ZiprecUIXterm::displayString(const char *s)
{
   return s ? displayText(s,strlen(s)) : false ;
}

//----------------------------------------------------------------------

bool ZiprecUIXterm::run(const char *initial_file)
{
   (void)initial_file ;
//FIXME
   return false ;
}

//----------------------------------------------------------------------

bool ZiprecUIXterm::openFileCommand()
{

   return false ; //FIXME
}

//----------------------------------------------------------------------

bool ZiprecUIXterm::saveFileCommand()
{

   return false ; //FIXME
}

//----------------------------------------------------------------------

bool ZiprecUIXterm::saveFileAsCommand()
{

   return false ; //FIXME
}

//----------------------------------------------------------------------

bool ZiprecUIXterm::revertFileCommand()
{

   return false ; //FIXME
}

//----------------------------------------------------------------------

bool ZiprecUIXterm::markCorruption()
{

   return false ; //FIXME
}

//----------------------------------------------------------------------

bool ZiprecUIXterm::markCorruptionStart()
{

   return false ; //FIXME
}

//----------------------------------------------------------------------

bool ZiprecUIXterm::markCorruptionEnd()
{

   return false ; //FIXME
}

//----------------------------------------------------------------------

bool ZiprecUIXterm::setResyncCommand()
{

   return false ; //FIXME
}

//----------------------------------------------------------------------

bool ZiprecUIXterm::shiftResyncForward()
{

   return false ; //FIXME
}

//----------------------------------------------------------------------

bool ZiprecUIXterm::shiftResyncBackward()
{

   return false ; //FIXME
}

//----------------------------------------------------------------------

bool ZiprecUIXterm::exitCommand()
{

   return false ; //FIXME
}

// end of file ui_xterm.C //
