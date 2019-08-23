/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  Version:  1.10beta		User Interface - Curses			*/
/*  LastEdit: 2019-08-22						*/
/*									*/
/*  (c) Copyright 2012,2013,2019 Carnegie Mellon University		*/
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

#include <cstring>
#include <unistd.h>
#include "ui_curses.h"
#include "framepac/texttransforms.h"

using namespace Fr ;

/************************************************************************/
/*	Methods for class ZiprecUICurses				*/
/************************************************************************/

ZiprecUICurses::ZiprecUICurses()
{
//FIXME
   return ;
}

//----------------------------------------------------------------------

ZiprecUICurses::~ZiprecUICurses()
{
//FIXME
   return ;
}

//----------------------------------------------------------------------

Owned<ZiprecUserInterface> ZiprecUICurses::instantiate()
{
   return static_cast<ZiprecUserInterface*>(new ZiprecUICurses) ;
}

//----------------------------------------------------------------------

bool ZiprecUICurses::clearScreen()
{
   displayText("\e[2J",4) ;
   return true ;
}

//----------------------------------------------------------------------

bool ZiprecUICurses::clearLine()
{
   displayText("\e[2K",4) ;
   return true ;
}

//----------------------------------------------------------------------

bool ZiprecUICurses::clearToEndOfLine()
{
   displayText("\e[K",3) ;
   return true ;
}

//----------------------------------------------------------------------

bool ZiprecUICurses::homeCursor()
{
   displayText("\e[H",3) ;
   return true ;
}

//----------------------------------------------------------------------

bool ZiprecUICurses::setCursor(unsigned row, unsigned col)
{
   auto ctrl = Fr::aprintf("\e[%u;%uH",row,col) ;
   displayString(ctrl) ;
   return true ;
}

//----------------------------------------------------------------------

bool ZiprecUICurses::displayChar(char c)
{
   return ::write(1,&c,1) == 1 ;
}

//----------------------------------------------------------------------

bool ZiprecUICurses::displayText(const char *buf, unsigned len)
{
   return ::write(1,buf,len) == 1 ;
}

//----------------------------------------------------------------------

bool ZiprecUICurses::displayString(const char *s)
{
   return s ? displayText(s,strlen(s)) : false ;
}

//----------------------------------------------------------------------

bool ZiprecUICurses::run(const char *initial_file)
{
   (void)initial_file ;
//FIXME
   return false ;
}

//----------------------------------------------------------------------

bool ZiprecUICurses::openFileCommand()
{

   return false ; //FIXME
}

//----------------------------------------------------------------------

bool ZiprecUICurses::saveFileCommand()
{

   return false ; //FIXME
}

//----------------------------------------------------------------------

bool ZiprecUICurses::saveFileAsCommand()
{

   return false ; //FIXME
}

//----------------------------------------------------------------------

bool ZiprecUICurses::revertFileCommand()
{

   return false ; //FIXME
}

//----------------------------------------------------------------------

bool ZiprecUICurses::markCorruption()
{

   return false ; //FIXME
}

//----------------------------------------------------------------------

bool ZiprecUICurses::markCorruptionStart()
{

   return false ; //FIXME
}

//----------------------------------------------------------------------

bool ZiprecUICurses::markCorruptionEnd()
{

   return false ; //FIXME
}

//----------------------------------------------------------------------

bool ZiprecUICurses::setResyncCommand()
{

   return false ; //FIXME
}

//----------------------------------------------------------------------

bool ZiprecUICurses::shiftResyncForward()
{

   return false ; //FIXME
}

//----------------------------------------------------------------------

bool ZiprecUICurses::shiftResyncBackward()
{

   return false ; //FIXME
}

//----------------------------------------------------------------------

bool ZiprecUICurses::exitCommand()
{

   return false ; //FIXME
}

// end of file ui_xterm.C //
