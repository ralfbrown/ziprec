/****************************** -*- C++ -*- *****************************/
/*                                                                      */
/*	LA-Strings: language-aware text-strings extraction		*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  File:     wildcard.h						*/
/*  Version:  1.30							*/
/*  LastEdit: 2019-07-28						*/
/*                                                                      */
/*  (c) Copyright 2011,2012,2013,2019 Carnegie Mellon University	*/
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

#include <bitset>
#include <climits>
#include "framepac/smartptr.h"

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

class WildcardSet
   {
   public:
      WildcardSet(bool allow_all = false) ;
      WildcardSet(const WildcardSet &) = default ;
      ~WildcardSet() { m_count = 0 ; }
      //WildcardSet &operator = (const WildcardSet &) ;

      // accessors
      unsigned setSize() const { return m_count ; }
      uint8_t firstMember() const ;
      bool contains(uint8_t value) const { return m_values.test(value) ; }
      bool couldBe(const bool *charset) const ;
      bool mustBe(const bool *charset) const ;

      // modifiers
      void cacheSetSize() ;
      void add(uint8_t value) ;
      void addAll() ;
      void remove(uint8_t value) ;
      void removeRange(uint8_t first, uint8_t last) ;
      void removeAll() ;
   private:
      std::bitset<256> m_values ;
      uint16_t	       m_count ;   // cached value of m_values.count()
   };

//----------------------------------------------------------------------

class WildcardCollection
   {
   public:
      WildcardCollection(unsigned max_ref, bool allow_all = false) ;
      WildcardCollection(const WildcardCollection*, bool allow_all_if_empty = false) ;
      ~WildcardCollection() = default ;

      // accessors
      unsigned numSets() const { return m_numsets ; }
      const WildcardSet *set(unsigned wildcard) const
	 { return (wildcard < m_numsets) ? &m_wildcards[wildcard] : nullptr ; }
      WildcardSet *set(unsigned wildcard) { return (wildcard < m_numsets) ? &m_wildcards[wildcard] : nullptr ; }
      unsigned setSize(unsigned wildcard) const { return m_wildcards[wildcard].setSize() ; }
      uint8_t firstMember(unsigned wildcard) const { return m_wildcards[wildcard].firstMember() ; }
      bool contains(unsigned wildcard, uint8_t value) const { return m_wildcards[wildcard].contains(value) ; }
      bool couldBe(class DecodedByte db, const bool *charset) const ;
      bool mustBe(class DecodedByte db, const bool *charset) const ;

      // modifiers
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

   private:
      Fr::NewPtr<WildcardSet> m_wildcards ;
      unsigned	              m_numsets ;
   } ;

#endif /* !WILDCARD_H_INCLUDED */

// end of file wildcard.h //
