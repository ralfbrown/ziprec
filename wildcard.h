/****************************** -*- C++ -*- *****************************/
/*                                                                      */
/*	LA-Strings: language-aware text-strings extraction		*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  File:     wildcard.h						*/
/*  Version:  1.21							*/
/*  LastEdit: 09feb2013							*/
/*                                                                      */
/*  (c) Copyright 2011,2012,2013 Ralf Brown/Carnegie Mellon University	*/
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

#ifndef __WILDCARD_H_INCLUDED
#define __WILDCARD_H_INCLUDED

#include <limits.h>

using namespace std ;

/************************************************************************/
/*	Utility macros							*/
/************************************************************************/

#ifndef lengthof
#define lengthof(x) (sizeof(x) / sizeof(x[0]))
#endif

/************************************************************************/
/*	Manifest Constants						*/
/************************************************************************/

/************************************************************************/
/************************************************************************/

inline bool bit_is_set(const void *mem, uint16_t bitnum)
{
#if defined(__386__) && defined(__GNUC__)
   bool result ;
   __asm__ ("btw %w1,(%2)\n\t"
	    "setc %%al"
	    : "=a" (result) : "r" (bitnum), "r" (mem) : "cc") ;
   return result ;
#else
   uint8_t mask = 1 << (bitnum % 8) ;
   return (((const uint8_t*)mem)[bitnum / 8] & mask) != 0 ;
#endif
}

class WildcardSet
   {
   private:
      uint16_t	m_count ;
      uint32_t	m_values[256 / sizeof(uint32_t) / CHAR_BIT] ;
   public:
      WildcardSet(bool allow_all = false) ;
      WildcardSet(const WildcardSet &) ;
      ~WildcardSet() { m_count = 0 ; }
      //WildcardSet &operator = (const WildcardSet &) ;

      // accessors
      unsigned setSize() const { return m_count ; }
      uint8_t firstMember() const ;
      bool contains(uint8_t value) const { return bit_is_set(m_values,value) ; }
      bool couldBe(const bool *charset) const ;
      bool mustBe(const bool *charset) const ;

      // modifiers
      void cacheSetSize() ;
      void add(uint8_t value) ;
      void addAll() ;
      void remove(uint8_t value) ;
      void removeRange(uint8_t first, uint8_t last) ;
      void removeAll() ;
   };

//----------------------------------------------------------------------

class WildcardCollection
   {
   private:
      WildcardSet *m_wildcards ;
      unsigned	   m_numsets ;
   public:
      WildcardCollection(unsigned max_ref, bool allow_all = false) ;
      WildcardCollection(const WildcardCollection *,
			 bool allow_all_if_empty = false) ;
      ~WildcardCollection() {}

      // accessors
      unsigned numSets() const { return m_numsets ; }
      const WildcardSet *set(unsigned wildcard) const
	 { return (wildcard < m_numsets) ? &m_wildcards[wildcard] : 0 ; }
      WildcardSet *set(unsigned wildcard)
	 { return (wildcard < m_numsets) ? &m_wildcards[wildcard] : 0 ; }
      unsigned setSize(unsigned wildcard) const
	 { return m_wildcards[wildcard].setSize() ; }
      uint8_t firstMember(unsigned wildcard) const
	 { return m_wildcards[wildcard].firstMember() ; }
      bool contains(unsigned wildcard, uint8_t value) const
	 { return m_wildcards[wildcard].contains(value) ; }
      bool couldBe(class DecodedByte db, const bool *charset) const ;
      bool mustBe(class DecodedByte db, const bool *charset) const ;

      // modifiers
      void cacheSetSize(unsigned wildcard)
	 { m_wildcards[wildcard].cacheSetSize() ; }
      void cacheSetSizes() ;
      void add(unsigned wildcard, uint8_t value)
	 { if (wildcard < numSets()) m_wildcards[wildcard].add(value) ; }
      void addAll(unsigned wildcard)
	 { if (wildcard < numSets()) m_wildcards[wildcard].addAll() ; }
      void remove(unsigned wildcard, uint8_t value)
	 { if (wildcard < numSets()) m_wildcards[wildcard].remove(value) ; }
      void removeRange(unsigned wildcard, uint8_t first, uint8_t last)
	 { if (wildcard < numSets()) m_wildcards[wildcard].removeRange(first,last) ; }
      void removeAll(unsigned wildcard)
	 { if (wildcard < numSets()) m_wildcards[wildcard].removeAll() ; }
      void removeAll() ;
      void removeFromAll(uint8_t value) ;
      void allowAllIfEmpty() ;
      void copy(const WildcardCollection *source,
		bool allow_all_if_empty = false) ;
   } ;

#endif /* !WILDCARD_H_INCLUDED */

// end of file wildcard.h //
