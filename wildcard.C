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

void WildcardSet::cacheSetSize()
{
   m_count = m_values.count() ;
   return ;
}

//----------------------------------------------------------------------

uint8_t WildcardSet::firstMember() const
{
   return m_values._Find_first() ;
}

//----------------------------------------------------------------------

void WildcardSet::add(uint8_t value)
{
   m_values.set(value) ;
   return ;
}

//----------------------------------------------------------------------

void WildcardSet::addAll()
{
   m_values.set() ;
   m_count = 256 ;
   return ;
}

//----------------------------------------------------------------------

void WildcardSet::remove(uint8_t value)
{
   m_values.reset(value) ;
   return ;
}

//----------------------------------------------------------------------

void WildcardSet::removeRange(uint8_t first, uint8_t last)
{
   for (size_t i = first ; i <= last ; i++)
      m_values.reset(i) ;
   return ;
}

//----------------------------------------------------------------------

void WildcardSet::removeAll()
{
   m_values.reset() ;
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
