/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  File: sort.h - list sorting and deduplication			*/
/*  Version:  1.00gamma				       			*/
/*  LastEdit: 09may2013							*/
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

#ifndef __SORT_H_INCLUDED
#define __SORT_H_INCLUDED

#include "wordhash.h"

/************************************************************************/
/*	Type declartions						*/
/************************************************************************/

typedef int (*WordCompareFnPtr)(const WordString*,const WordString*) ;

/************************************************************************/
/*	Function prototypes						*/
/************************************************************************/

WordList *sort_words(WordList *words, WordCompareFnPtr cmp) ;
WordList *merge_lists(WordList *list1, WordList *list2, WordCompareFnPtr cmp) ;
WordList *merge_duplicates(WordList *words) ;

int compare_frequencies(const WordString *w1, const WordString *w2) ;

#endif /* !__SORT_H_INCLUDED */

// end of file sort.h //
