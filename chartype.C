/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  File: chartype.h - character-type data				*/
/*  Version:  1.00gamma				       			*/
/*  LastEdit: 28apr2013							*/
/*									*/
/*  (c) Copyright 2011,2013 Ralf Brown/CMU				*/
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

#include "chartype.h"

/************************************************************************/
/*	Global data							*/
/************************************************************************/

const bool non_word_character_ASCII[] =
   {
      true,  true,  true,  true,  true,  true,  true,  true,  // NUL to ^G
      true,  true,  true,  true,  true,  true,  true,  true,  // ^H to ^O
      true,  true,  true,  true,  true,  true,  true,  true,  // ^P to ^W
      true,  true,  true,  true,  true,  true,  true,  true,  // ^X to ^_
      true,  true,  true,  true,  true,  true,  true,  true,  // SP to '
      true,  true,  true,  true,  true,  false, true,  true,  // ( to /
      false, false, false, false, false, false, false, false, // 0 to 7
      false, false, true,  true,  false, true,  false, true,  // 8 to ?
      true,  false, false, false, false, false, false, false, // @ to G
      false, false, false, false, false, false, false, false, // H to O
      false, false, false, false, false, false, false, false, // P to W
      false, false, false, true,  true,  true,  true,  false, // X to _
      true,  false, false, false, false, false, false, false, // ` to g
      false, false, false, false, false, false, false, false, // h to o
      false, false, false, false, false, false, false, false, // p to w
      false, false, false, true,  true,  true,  true,  true,  // x to DEL
      true,  true,  true,  true,  true,  true,  true,  true,  // 0x80 - 0x87
      true,  true,  true,  true,  true,  true,  true,  true,  // 0x88 - 0x8F
      true,  true,  true,  true,  true,  true,  true,  true,  // 0x90 - 0x97
      true,  true,  true,  true,  true,  true,  true,  true,  // 0x98 - 0x9F
      true,  true,  true,  true,  true,  true,  true,  true,  // 0xA0 - 0xA7
      true,  true,  true,  true,  true,  true,  true,  true,  // 0xA8 - 0xAF
      true,  true,  true,  true,  true,  true,  true,  true,  // 0xB0 - 0xB7
      true,  true,  true,  true,  true,  true,  true,  true,  // 0xB8 - 0xBF
      true,  true,  true,  true,  true,  true,  true,  true,  // 0xC0 - 0xC7
      true,  true,  true,  true,  true,  true,  true,  true,  // 0xC8 - 0xCF
      true,  true,  true,  true,  true,  true,  true,  true,  // 0xD0 - 0xD7
      true,  true,  true,  true,  true,  true,  true,  true,  // 0xD8 - 0xDF
      true,  true,  true,  true,  true,  true,  true,  true,  // 0xE0 - 0xE7
      true,  true,  true,  true,  true,  true,  true,  true,  // 0xE8 - 0xEF
      true,  true,  true,  true,  true,  true,  true,  true,  // 0xF0 - 0xF7
      true,  true,  true,  true,  true,  true,  true,  true   // 0xF8 - 0xFF
   } ;

const bool non_word_character_Latin1[] =
   {
      true,  true,  true,  true,  true,  true,  true,  true,  // NUL to ^G
      true,  true,  true,  true,  true,  true,  true,  true,  // ^H to ^O
      true,  true,  true,  true,  true,  true,  true,  true,  // ^P to ^W
      true,  true,  true,  true,  true,  true,  true,  true,  // ^X to ^_
      true,  true,  true,  true,  true,  true,  true,  true,  // SP to '
      true,  true,  true,  true,  true,  false, true,  true,  // ( to /
      false, false, false, false, false, false, false, false, // 0 to 7
      false, false, true,  true,  false, true,  false, true,  // 8 to ?
      true,  false, false, false, false, false, false, false, // @ to G
      false, false, false, false, false, false, false, false, // H to O
      false, false, false, false, false, false, false, false, // P to W
      false, false, false, true,  true,  true,  true,  false, // X to _
      true,  false, false, false, false, false, false, false, // ` to g
      false, false, false, false, false, false, false, false, // h to o
      false, false, false, false, false, false, false, false, // p to w
      false, false, false, true,  true,  true,  true,  true,  // x to DEL
      true,  true,  true,  true,  true,  true,  true,  true,  // 0x80 - 0x87
      true,  true,  true,  true,  true,  true,  true,  true,  // 0x88 - 0x8F
      true,  true,  true,  true,  true,  true,  true,  true,  // 0x90 - 0x97
      true,  true,  true,  true,  true,  true,  true,  true,  // 0x98 - 0x9F
      true,  true,  true,  true,  true,  true,  true,  true,  // 0xA0 - 0xA7
      true,  true,  true,  true,  true,  true,  true,  true,  // 0xA8 - 0xAF
      true,  true,  true,  true,  true,  true,  true,  true,  // 0xB0 - 0xB7
      true,  true,  true,  true,  true,  true,  true,  true,  // 0xB8 - 0xBF
      false, false, false, false, false, false, false, false, // 0xC0 - 0xC7
      false, false, false, false, false, false, false, false, // 0xC8 - 0xCF
      false, false, false, false, false, false, false, true,  // 0xD0 - 0xD7
      false, false, false, false, false, false, false, false, // 0xD8 - 0xDF
      false, false, false, false, false, false, false, false, // 0xE0 - 0xE7
      false, false, false, false, false, false, false, false, // 0xE8 - 0xEF
      false, false, false, false, false, false, false, true,  // 0xF0 - 0xF7
      false, false, false, false, false, false, false, false  // 0xF8 - 0xFF
   } ;

// generic, covers UTF-8, EUC, etc.
const bool non_word_character_generic[] =
   {
      true,  true,  true,  true,  true,  true,  true,  true,  // NUL to ^G
      true,  true,  true,  true,  true,  true,  true,  true,  // ^H to ^O
      true,  true,  true,  true,  true,  true,  true,  true,  // ^P to ^W
      true,  true,  true,  true,  true,  true,  true,  true,  // ^X to ^_
      true,  true,  true,  true,  true,  true,  true,  true,  // SP to '
      true,  true,  true,  true,  true,  false, true,  true,  // ( to /
      false, false, false, false, false, false, false, false, // 0 to 7
      false, false, true,  true,  false, true,  false, true,  // 8 to ?
      true,  false, false, false, false, false, false, false, // @ to G
      false, false, false, false, false, false, false, false, // H to O
      false, false, false, false, false, false, false, false, // P to W
      false, false, false, true,  true,  true,  true,  false, // X to _
      true,  false, false, false, false, false, false, false, // ` to g
      false, false, false, false, false, false, false, false, // h to o
      false, false, false, false, false, false, false, false, // p to w
      false, false, false, true,  true,  true,  true,  true,  // x to DEL
      true,  true,  true,  true,  true,  true,  true,  true,  // 0x80 - 0x87
      true,  true,  true,  true,  true,  true,  true,  true,  // 0x88 - 0x8F
      true,  true,  true,  true,  true,  true,  true,  true,  // 0x90 - 0x97
      true,  true,  true,  true,  true,  true,  true,  true,  // 0x98 - 0x9F
      true,  true,  true,  true,  true,  true,  true,  true,  // 0xA0 - 0xA7
      true,  true,  true,  true,  true,  true,  true,  true,  // 0xA8 - 0xAF
      true,  true,  true,  true,  true,  true,  true,  true,  // 0xB0 - 0xB7
      true,  true,  true,  true,  true,  true,  true,  true,  // 0xB8 - 0xBF
      true,  true,  true,  true,  true,  true,  true,  true,  // 0xC0 - 0xC7
      true,  true,  true,  true,  true,  true,  true,  true,  // 0xC8 - 0xCF
      true,  true,  true,  true,  true,  true,  true,  true,  // 0xD0 - 0xD7
      true,  true,  true,  true,  true,  true,  true,  true,  // 0xD8 - 0xDF
      true,  true,  true,  true,  true,  true,  true,  true,  // 0xE0 - 0xE7
      true,  true,  true,  true,  true,  true,  true,  true,  // 0xE8 - 0xEF
      true,  true,  true,  true,  true,  true,  true,  true,  // 0xF0 - 0xF7
      true,  true,  true,  true,  true,  true,  true,  true   // 0xF8 - 0xFF
   } ;

const bool *non_word_character = non_word_character_Latin1 ;

// end of file chartype.C //
