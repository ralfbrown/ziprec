/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  File: index.h - index for unknown back-references (wildcards)	*/
/*  Version:  1.10beta				       			*/
/*  LastEdit: 2019-07-25						*/
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

#ifndef __INDEX_H_INCLUDED
#define __INDEX_H_INCLUDED

#include <cstdint>
#include "dbyte.h"
#include "framepac/smartptr.h"

/************************************************************************/
/*	Types								*/
/************************************************************************/

class WildcardIndex
   {
   public:
      typedef Fr::NewPtr<uint32_t> LocPtr ;
   public:
      WildcardIndex(const DecodedByte *bytes, size_t num_bytes, unsigned max_ref) ;
      ~WildcardIndex() = default ;

      // accessors
      unsigned indexSize() const { return  m_indexsize ; }
      uint32_t location(unsigned wildcard, unsigned index) const
         { return (wildcard < indexSize() && index < m_counts[wildcard]) 
	       ? m_locations[wildcard][index] : UINT32_MAX ; }
      const uint32_t *locations(unsigned wildcard) const { return m_locations[wildcard].get() ; }
      unsigned numLocations(unsigned wildcard) const { return m_counts[wildcard] ; }

   private:
      Fr::NewPtr<uint32_t>  m_counts ;
      Fr::NewPtr<LocPtr>    m_locations ;
      unsigned              m_indexsize ;
   } ;

#endif /* !__INDEX_H_INCLUDED */

// end of file index.h //
