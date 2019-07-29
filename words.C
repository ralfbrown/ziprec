/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  File: words.C - word-segmentation functions				*/
/*  Version:  1.10beta				       			*/
/*  LastEdit: 2019-07-28						*/
/*									*/
/*  (c) Copyright 2011,2013,2019 Carnegie Mellon University		*/
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
#include "chartype.h"
#include "words.h"

/************************************************************************/
/*	Utility functions						*/
/************************************************************************/

bool is_whitespace(uint8_t byte)
{
   if (isascii(byte))
      return isspace(byte) ;
   else
      return byte == 0xA0 ;
}

//----------------------------------------------------------------------

bool is_whitespace(const uint8_t *array, size_t position1, size_t position2)
{
   if (position1 >= position2)
      return false ;
   for (size_t i = position1 ; i < position2 ; i++)
      {
      if (!is_whitespace(array[i]))
	 return false ;
      }
   return true ;
}

//----------------------------------------------------------------------

bool contains_unknown(const uint8_t *array, size_t position1, size_t position2)
{
   if (position1 >= position2)
      return false ;
   for (size_t i = position1 ; i < position2 ; i++)
      {
      if (array[i] == 0x7F)
	 return true ;
      }
   return false ;
}

//----------------------------------------------------------------------

bool is_word_boundary(const uint8_t *array, size_t position, bool utf8)
{
   //FIXME: the following code only works for 8-bit charsets based on
   //  ASCII and partially for UTF-8, not for EBCDIC, UTF-16, UTF-32,
   //  etc.
   uint8_t curr_byte = array[position] ;
   uint8_t prev_byte = position ? array[position-1] : 'a' ;
   // special case for unknown bytes in decompressed data: since we don't know
   //   whether or not they are a boundary, assume they aren't, which will cause
   //   sequences without other boundaries to be agglomorated into one 'word'
   if (curr_byte == 0x7F)
      return false ;
   if (utf8 && curr_byte >= 0x80)
      {
      if (!isascii(prev_byte) || !non_word_character[prev_byte])
	 return false ;
      }

   // special checks for characters that always indicate the start of a word
   if (curr_byte == '<')
      return true ;

   // special checks for characters that we might need to keep attached to
   //   preceding text
   if (prev_byte == '>')
      return true ;
   if (curr_byte == '#' && prev_byte == '&')
      {
      // looks like an HTML character code, so don't split
      return false ;
      }
//   if (curr_byte == '\n' && prev_byte == '\r')
//      return false ;			// keep CR-LF together
   if (curr_byte == '-' && prev_byte == '-')
      return false ;			// keep multiple dashes together
   if (curr_byte == '/' && prev_byte == '<')
      return false ;			// closing *ML tag
   if (curr_byte == '>' && prev_byte == '/')
      return false ;			// self-closing X*ML tag
   if (isascii(curr_byte) && isalpha(curr_byte))
      {
      if (prev_byte == '<')
	 return false ;			// *ML tag
      if (prev_byte == '&')
	 return false ;			// likely HTML character entity
      if (prev_byte == '/' && position > 1 && array[position-2] == '<')
	 return false ;			// closing *ML tag
      if (prev_byte == '\'' && position > 1 && isascii(array[position-2]) &&
	  isalpha(array[position-2]))
	 return false ;			// contraction
      }
   if (isascii(curr_byte) && isdigit(curr_byte))
      {
      if (prev_byte == '.' || prev_byte == ',')
	 return false ;			// don't split on decimal/thousands
      if (prev_byte == '#' && position > 1 && array[position-2] == '&')
	 return false ;			// keep HTML character code together
      }
   if (curr_byte == '\'' && isascii(prev_byte) && isalpha(prev_byte))
      {
      if (isascii(array[position+1]) && isalpha(array[position+1]))
	 return false ;			// contraction
      }
   // now that we've finished the special-case checks, split on any
   //   non-word character
   if (non_word_character[curr_byte])
      return true ;
   if (non_word_character[prev_byte])
      {
      if (!utf8)
	 return true ;			// transition into word
      if (isascii(prev_byte))
	 return true ;
      }
   return false ;
}

// end of file words.C //
