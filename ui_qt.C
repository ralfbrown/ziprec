/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  Version:  1.00beta		User Interface - Qt			*/
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

#include "ui_qt.h"

/************************************************************************/
/************************************************************************/

ZiprecUIQt::ZiprecUIQt()
{

   return ;
}

//----------------------------------------------------------------------

ZiprecUIQt::~ZiprecUIQt()
{

   return ;
}

//----------------------------------------------------------------------

ZiprecUserInterface *ZiprecUIQt::instantiate()
{
   return new ZiprecUIQt ;
}

//----------------------------------------------------------------------

bool ZiprecUIQt::run(const char *initial_file)
{
   (void)initial_file; //FIXME
   return false ; //FIXME
}

//----------------------------------------------------------------------

bool ZiprecUIQt::openFileCommand()
{

   return false ; //FIXME
}

//----------------------------------------------------------------------

bool ZiprecUIQt::saveFileCommand()
{

   return false ; //FIXME
}

//----------------------------------------------------------------------

bool ZiprecUIQt::saveFileAsCommand()
{

   return false ; //FIXME
}

//----------------------------------------------------------------------

bool ZiprecUIQt::revertFileCommand()
{

   return false ; //FIXME
}

//----------------------------------------------------------------------

bool ZiprecUIQt::markCorruption()
{

   return false ; //FIXME
}

//----------------------------------------------------------------------

bool ZiprecUIQt::markCorruptionStart()
{

   return false ; //FIXME
}

//----------------------------------------------------------------------

bool ZiprecUIQt::markCorruptionEnd()
{

   return false ; //FIXME
}

//----------------------------------------------------------------------

bool ZiprecUIQt::setResyncCommand()
{

   return false ; //FIXME
}

//----------------------------------------------------------------------

bool ZiprecUIQt::shiftResyncForward()
{

   return false ; //FIXME
}

//----------------------------------------------------------------------

bool ZiprecUIQt::shiftResyncBackward()
{

   return false ; //FIXME
}

//----------------------------------------------------------------------

bool ZiprecUIQt::exitCommand()
{

   return false ; //FIXME
}

// end of file ui_qt.C //
