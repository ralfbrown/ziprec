/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  File: sort.C - list sorting and deduplication			*/
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

#include <limits.h>
#include "wordhash.h"
#include "sort.h"

/************************************************************************/
/************************************************************************/

WordList *merge_lists(WordList *list1, WordList *list2, WordCompareFnPtr cmp)
{
   if (!list2)
      return list1 ;
   else if (!list1)
      return list2 ;
   WordList *merged ;
   if (cmp(list1->string(),list2->string()) <= 0)
      {
      merged = list1 ;
      list1 = list1->next() ;
      }
   else
      {
      merged = list2 ;
      list2 = list2->next() ;
      }
   WordList *prev = merged ;
   while (list1 && list2)
      {
      if (cmp(list1->string(),list2->string()) <= 0)
	 {
	 prev->setNext(list1) ;		// glue item onto end of results list
	 prev = list1 ;
	 list1 = list1->next() ;	// advance down list1
	 }
      else
	 {
	 prev->setNext(list2) ;		// glue item onto end of results list
	 prev = list2 ;
	 list2 = list2->next() ;	// advance down list2
	 }
      }
   if (list1)
      prev->setNext(list1) ;
   else
      prev->setNext(list2) ;
   return merged ;
}

//----------------------------------------------------------------------

WordList *sort_words(WordList *words, WordCompareFnPtr cmp)
{
   if (!words || !words->next())	// empty or singleton list?
      return words ;			// if yes, it's already sorted
   WordList* sublists[CHAR_BIT * sizeof(size_t)] ;
   sublists[0] = nullptr ;
   size_t maxbits = 0 ;
   // scan down the given list, creating sorted sublists in powers of two
   while (words)
      {
      // chop the head node off the list
      WordList *sublist = words ;
      words = words->next() ;
      sublist->setNext(nullptr) ;

      // merge the head node with sucessively longer sublists until we
      //   reach a power of two for which there currently is no sublist
      size_t i ;
      for (i = 0 ; i <= maxbits && sublists[i] ; i++)
	 {
	 // longer sublists contain items from earlier in the input, so make them the first arg to merge
	 //   so that the sort remains stable
	 sublist = merge_lists(sublists[i],sublist,cmp) ;
	 sublists[i] = nullptr ;
	 }
      sublists[i] = sublist ;
      if (i > maxbits)
	 maxbits++ ;
      }
   // at this point, we just need to merge together all the remaining
   //   sublists
   WordList *result = sublists[0] ;
   for (size_t i = 1 ; i <= maxbits ; i++)
      {
      if (sublists[i])
	 result = merge_lists(sublists[i],result,cmp) ;
      }
   return result ;
}

//----------------------------------------------------------------------

WordList *merge_duplicates(WordList *words)
{
   if (!words)
      return nullptr ;
   WordList *merged = words ;
   WordList *prev = merged ;
   WordList *next ;
   for (words = words->next() ; words ; words = next)
      {
      next = words->next() ;
      if (*prev->string() == *words->string())
	 {
//cerr << "merging " << *prev->string() << " and " << *words->string() << endl;
	 prev->string()->setFrequency(prev->string()->frequency() + words->string()->frequency()) ;
	 prev->setNext(next) ;
	 words->setNext(nullptr) ;
	 delete words ;
	 }
      else
	 prev = words ;
      }
   return merged ;
}

//----------------------------------------------------------------------

int compare_frequencies(const WordString *w1, const WordString *w2)
{
   uint32_t freq1 = w1->frequency() ;
   uint32_t freq2 = w2->frequency() ;
   if (freq1 > freq2)
      return -1 ;
   else if (freq1 < freq2)
      return +1 ;
   return w1->compareText(w2) ;
}

// end of file sort.C //
