/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  Version:  1.10beta		User Interface	       			*/
/*  LastEdit: 2019-07-25						*/
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

#include <cstdio>
#include <cstring>
#include <iostream>
#include "ui_common.h"
#include "framepac/file.h"

using namespace std ;
using namespace Fr ;

/************************************************************************/
/************************************************************************/

/************************************************************************/
/*	Global variables for class ZiprecUserInterface			*/
/************************************************************************/

unsigned ZiprecUserInterface::s_force_interface = 0 ;

/************************************************************************/
/*	Methods for class ZiprecUICommon				*/
/************************************************************************/

ZiprecUICommon::ZiprecUICommon()
{
   return ;
}

//----------------------------------------------------------------------

ZiprecUICommon::~ZiprecUICommon()
{
   return ;
}

//----------------------------------------------------------------------

bool ZiprecUICommon::run(const char *initial_file)
{
   if (!loadFile(initial_file))
      openFileCommand() ;

   return false ;
}

//----------------------------------------------------------------------

bool ZiprecUICommon::loadFile(const char *filename)
{
   if (!filename || !*filename)
      return false ;

   return false ;
}

//----------------------------------------------------------------------

bool ZiprecUICommon::toStartOfFile()
{

   return true ;
}

//----------------------------------------------------------------------

bool ZiprecUICommon::toEndOfFile()
{

   return true ;
}

//----------------------------------------------------------------------

bool ZiprecUICommon::toFirstUnknownByte()
{

   return true ;
}

//----------------------------------------------------------------------

bool ZiprecUICommon::toLastUnknownByte()
{

   return true ;
}

//----------------------------------------------------------------------

bool ZiprecUICommon::toPreviousUnknownSeq()
{

   return true ;
}

//----------------------------------------------------------------------

bool ZiprecUICommon::toNextUnknownSeq()
{

   return true ;
}

//----------------------------------------------------------------------

bool ZiprecUICommon::cursorUp()
{

   return true ;
}

//----------------------------------------------------------------------

bool ZiprecUICommon::cursorDown()
{

   return true ;
}

//----------------------------------------------------------------------

bool ZiprecUICommon::pageUp()
{

   return true ;
}

//----------------------------------------------------------------------

bool ZiprecUICommon::pageDown()
{

   return true ;
}

//----------------------------------------------------------------------

bool ZiprecUICommon::previousUnknownByte()
{

   return true ;
}

//----------------------------------------------------------------------

bool ZiprecUICommon::nextUnknownByte()
{

   return true ;
}

//----------------------------------------------------------------------

bool ZiprecUICommon::openFileCommand()
{

   return false ;
}

//----------------------------------------------------------------------

bool ZiprecUICommon::saveFileCommand()
{

   return false ;
}

//----------------------------------------------------------------------

bool ZiprecUICommon::saveFileAsCommand()
{

   return false ;
}

//----------------------------------------------------------------------

bool ZiprecUICommon::revertFileCommand()
{

   return false ;
}

//----------------------------------------------------------------------

bool ZiprecUICommon::markCorruption()
{

   return false ; //FIXME
}

//----------------------------------------------------------------------

bool ZiprecUICommon::markCorruptionStart()
{

   return false ; //FIXME
}

//----------------------------------------------------------------------

bool ZiprecUICommon::markCorruptionEnd()
{

   return false ; //FIXME
}

//----------------------------------------------------------------------

bool ZiprecUICommon::setResyncCommand()
{

   return false ; //FIXME
}

//----------------------------------------------------------------------

bool ZiprecUICommon::shiftResyncForward()
{

   return false ; //FIXME
}

//----------------------------------------------------------------------

bool ZiprecUICommon::shiftResyncBackward()
{

   return false ; //FIXME
}

//----------------------------------------------------------------------

bool ZiprecUICommon::exitCommand()
{

   return false ;
}

/************************************************************************/
/*	Methods for class ZiprecUserInterface				*/
/************************************************************************/

ZiprecUserInterface::ZiprecUserInterface()
{

   return ;
}

//----------------------------------------------------------------------

ZiprecUserInterface::~ZiprecUserInterface()
{

   return ;
}

//----------------------------------------------------------------------

bool ZiprecUserInterface::loadConfig(const char *cfgfile)
{
   if (!cfgfile)
      return true ;
   if (!*cfgfile)
      return false ;
   CInputFile fp(cfgfile) ;
   if (!fp)
      return false ;
   bool success = true ;
//FIXME

   return success ;
}

//----------------------------------------------------------------------

bool ZiprecUserInterface::selectInterfaceType(const char *iface)
{
   if (!iface)
      return true ;
   if (!*iface)
      return false ;
   bool success = false ;
   // parse the interface spec and determine which class to generate
   //   in instantiate()
   if (strcasecmp(iface,"xterm") == 0)
      {
//FIXME
      }
   else if (strcasecmp(iface,"Qt") == 0)
      {
      }
   else if (strcasecmp(iface,"curses") == 0)
      {
      }
   return success ;
}

//----------------------------------------------------------------------

ZiprecUserInterface *ZiprecUserInterface::instantiate()
{
   // instantiate the appropriate subclass; check terminal capabilities
   //   to determine which interface to use if none has been forced by
   //   the user
   unsigned interface_type = s_force_interface ;
   if (!interface_type)
      {
//FIXME


      }
   ZiprecUserInterface *ui = 0 ;
   switch (interface_type)
      {
      case 0:
	 cerr << "Your display is not supported by any available user interface"
	      << endl ;
	 break ;
//FIXME
      default:
	 cerr << "missed case in ZiprecUserInterface::instantiate()" << endl ;
	 break ;
      }
   return ui ;
}

//----------------------------------------------------------------------

bool ZiprecUserInterface::run(const char * /*initial_file*/)
{
   return false ;
}

// end of file ui_common.C //
