/****************************** -*- C++ -*- *****************************/
/*									*/
/*	LA-Strings: language-aware text-strings extraction		*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  File: wildcard.C - Wildcard sets and collections			*/
/*  Version:  1.30				       			*/
/*  LastEdit: 2019-07-07						*/
/*									*/
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

#include <algorithm>
#include <cstdint>
#include "wildcard.h"
#include "framepac/utility.h"

/************************************************************************/
/*	Methods for class WildcardSet					*/
/************************************************************************/

WildcardSet::WildcardSet(bool allow_all)
{
   if (allow_all)
      {
      addAll() ;
      }
   else
      {
      removeAll() ;
      }
   return ;
}

//----------------------------------------------------------------------

WildcardSet::WildcardSet(const WildcardSet &orig)
{
   std::copy_n(orig.m_values,lengthof(m_values),m_values) ;
   m_count = orig.setSize() ;
   return ;
}

//----------------------------------------------------------------------

void WildcardSet::cacheSetSize()
{
   m_count = 0 ;
   for (size_t i = 0 ; i < lengthof(m_values) ; i++)
      m_count += Fr::popcount(m_values[i]) ;
   return ;
}

//----------------------------------------------------------------------

uint8_t WildcardSet::firstMember() const
{
   for (size_t i = 0 ; i < 256 ; i += 32)
      {
      if (!m_values[i/32])
	 continue ;
      for (size_t bit = 0 ; bit < 32 ; bit++)
	 {
	 if (contains(i+bit))
	    return (uint8_t)i+bit ;
	 }
      }
   return 0 ;
}

//----------------------------------------------------------------------

#define BITS_PER_ENTRY (CHAR_BIT * sizeof(m_values[0]))

//----------------------------------------------------------------------

bool WildcardSet::couldBe(const bool *charset) const
{
   for (size_t i = 0 ; i <= 0xFF ; i++)
      {
      if (charset[i] && contains(i))
	 return true ;
      }
   return false ;
}

//----------------------------------------------------------------------

bool WildcardSet::mustBe(const bool *charset) const
{
   for (size_t i = 0 ; i <= 0xFF ; i++)
      {
      if (!charset[i] && contains(i))
	 return false ;
      }
   return true ;
}

//----------------------------------------------------------------------

void WildcardSet::add(uint8_t value)
{
   uint32_t mask = 1 << (value % BITS_PER_ENTRY) ;
   m_values[value / BITS_PER_ENTRY] |= mask ;
   return ;
}

//----------------------------------------------------------------------

void WildcardSet::addAll()
{
   std::fill_n(m_values,lengthof(m_values),(uint32_t)~0) ;
   m_count = 256 ;
   return ;
}

//----------------------------------------------------------------------

void WildcardSet::remove(uint8_t value)
{
   uint32_t mask = 1 << (value % BITS_PER_ENTRY) ;
   m_values[value / BITS_PER_ENTRY] &= ~mask ;
   return ;
}

//----------------------------------------------------------------------

void WildcardSet::removeRange(uint8_t first, uint8_t last)
{
   //FIXME: could be done much more efficiently
   for (size_t i = first ; i <= last ; i++)
      remove(i) ;
   return ;
}

//----------------------------------------------------------------------

void WildcardSet::removeAll()
{
   std::fill_n(m_values,lengthof(m_values),0) ;
   m_count = 0 ;
   return ;
}

/************************************************************************/
/*	Methods for class WildcardCollection				*/
/************************************************************************/

WildcardCollection::WildcardCollection(unsigned max_ref, bool allow_all)
   : m_wildcards(max_ref)
{
   if (!m_wildcards)
      {
      m_numsets = 0 ;
      return ;
      }
   else
      {
      m_numsets = max_ref ;
      if (allow_all)
	 {
	 for (unsigned i = 0 ; i < numSets() ; i++)
	    m_wildcards[i].addAll() ;
	 }
      else
	 {
	 for (unsigned i = 0 ; i < numSets() ; i++)
	    m_wildcards[i].removeAll() ;
	 }
      }
   return ;
}

//----------------------------------------------------------------------

WildcardCollection::WildcardCollection(const WildcardCollection* orig, bool allow_all_if_empty)
{
   if (!orig)
      {
      m_numsets = 0 ;
      return  ;
      }
   m_wildcards.allocate(orig->numSets()) ;
   if (!m_wildcards)
      {
      m_numsets = 0 ;
      return ;
      }
   else
      {
      m_numsets = orig->numSets() ;
      copy(orig,allow_all_if_empty) ;
      }
   return ;
}

//----------------------------------------------------------------------

void WildcardCollection::cacheSetSizes()
{
   for (size_t i = 0 ; i < numSets() ; i++)
      m_wildcards[i].cacheSetSize() ;
   return ;
}

//----------------------------------------------------------------------

void WildcardCollection::removeAll()
{
   for (size_t i = 0 ; i < numSets() ; i++)
      m_wildcards[i].removeAll() ;
   return ;
}

//----------------------------------------------------------------------

void WildcardCollection::removeFromAll(uint8_t value)
{
   for (size_t i = 0 ; i < numSets() ; i++)
      m_wildcards[i].remove(value) ;
   return ;
}

//----------------------------------------------------------------------

void WildcardCollection::allowAllIfEmpty()
{
   for (unsigned i = 0 ; i < numSets() ; i++)
      {
      if (m_wildcards[i].setSize() == 0)
	 m_wildcards[i].addAll() ;
      }
   return ;
}

//----------------------------------------------------------------------

void WildcardCollection::copy(const WildcardCollection* source, bool allow_all_if_empty)
{
   if (source)
      {
      std::copy_n(source->m_wildcards.begin(),numSets(),m_wildcards.begin()) ;
      if (allow_all_if_empty)
	 {
	 allowAllIfEmpty() ;
	 }
      }
   return ;
}

// end of file wildcard.C //
