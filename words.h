/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  File: words.h - word-segmentation functions				*/
/*  Version:  1.00gamma				       			*/
/*  LastEdit: 07may2013							*/
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

#ifndef __WORDS_H_INCLUDED
#define __WORDS_H_INCLUDED

#include <cstdlib>
#include <ctype.h>
#include <stdint.h>
#include "chartype.h"
#include "langident/trie.h"
#include "langident/wildcard.h"

/************************************************************************/
/************************************************************************/

bool is_whitespace(uint8_t byte) ;
bool is_whitespace(const uint8_t *array, size_t position) ;
bool is_whitespace(const uint8_t *array,
		   size_t position1, size_t position2) ;
bool is_word_boundary(const uint8_t *array, size_t position, bool utf8=true) ;

bool contains_unknown(const uint8_t *array, size_t position1, size_t position2) ;

#endif /* !__WORDS_H_INCLUDED */

// end of file words.h //
