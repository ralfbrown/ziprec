/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  File: index.C - index for unknown back-references (wildcards)	*/
/*  Version:  1.10beta				       			*/
/*  LastEdit: 27jun2019							*/
/*									*/
/*  (c) Copyright 2011,2012,2013,2019 Ralf Brown/CMU			*/
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

#include "index.h"
#include "framepac/memory.h"

using namespace Fr ;

/************************************************************************/
/*	Methods for class WildcardIndex					*/
/************************************************************************/

WildcardIndex::WildcardIndex(const DecodedByte *bytes, size_t num_bytes,
			     unsigned max_ref)
   : m_counts(max_ref), m_locations(max_ref)
{
   if (!m_counts || !m_locations)
      {
      m_indexsize = 0 ;
      m_counts = nullptr ;
      m_locations = nullptr ;
      return ;
      }
   m_indexsize = max_ref ;
   // scan the given bytes, counting occurrences of each wildcard
   for (size_t i = 0 ; i < num_bytes ; i++)
      {
      if (bytes[i].isReference())
	 {
	 unsigned wild = bytes[i].originalLocation() ;
	 m_counts[wild]++ ;
	 }
      }
   for (size_t i = 0 ; i < indexSize() ; i++)
      {
      m_locations[i] = m_counts[i] ? new uint32_t[m_counts[i]] : nullptr ;
      }
   LocalAlloc<uint32_t,50000> in_use(indexSize()+1,true) ;
   for (size_t i = 0 ; i < num_bytes ; i++)
      {
      if (!bytes[i].isLiteral())
	 {
	 unsigned wild = bytes[i].originalLocation() ;
	 if (wild < indexSize() && in_use[wild] < m_counts[wild])
	    {
	    m_locations[wild][in_use[wild]++] = i ;
	    }
	 }
      }
   return ;
}

//----------------------------------------------------------------------

WildcardIndex::~WildcardIndex()
{
   for (size_t i = 0 ; i < indexSize() ; i++)
      {
      delete[] m_locations[i] ;
      }
   m_indexsize = 0 ;
   return ;
}

// end of file index.C //
