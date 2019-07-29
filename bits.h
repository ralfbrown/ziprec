/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  File: bits.h - Bit-field manipulation				*/
/*  Version:  1.10beta				       			*/
/*  LastEdit: 2019-07-28						*/
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

#ifndef __BITS_H_INCLUDED
#define __BITS_H_INCLUDED

#include <cstdlib>
#include <iostream>
#include <stdint.h>
#include "framepac/bits.h"

using namespace std ;

/************************************************************************/
/*	Type definitions						*/
/************************************************************************/

class BitPointer
   {
   public:
      BitPointer() { m_byteptr = nullptr ; m_bitnumber = 0 ; }
      BitPointer(const void *ptr) { m_byteptr = (const uint8_t*)ptr ; m_bitnumber = 0 ; }
      BitPointer(const BitPointer &ptr) { m_byteptr = ptr.m_byteptr ; m_bitnumber = ptr.m_bitnumber ; }
      BitPointer(const BitPointer *ptr) { m_byteptr = ptr->m_byteptr ; m_bitnumber = ptr->m_bitnumber ; }
      ~BitPointer() = default ;

      // accessors
      const uint8_t *bytePointer() const { return m_byteptr ; }
      unsigned bitNumber() const { return m_bitnumber ; }
      uint32_t getBit() const { return ((*m_byteptr) >> m_bitnumber) & 1 ; }
      uint32_t getBits(unsigned num_bits) const ;
      uint32_t getBitsReversed(unsigned num_bits) const ;
      uint32_t nextBit() { uint32_t b = getBit() ; advance(1) ; return b ; }
      uint32_t nextBits(unsigned num_bits) ;
      uint32_t nextBitsReversed(unsigned num_bits) ;
      uint32_t prevBit() { retreat1() ; return getBit() ; }
      uint32_t prevBits(unsigned num_bits) ;
      uint32_t prevBitsReversed(unsigned num_bits) ;

      // manipulators
      void advance(unsigned num_bits)
	 {
	 unsigned bit = m_bitnumber + num_bits ;
	 m_byteptr += (bit / 8) ;
	 m_bitnumber = (uint8_t)(bit & 7) ;
	 }
      void advanceBytes(size_t num_bytes) { m_byteptr += num_bytes ; }
      void advanceToByte()
	 {
	 if (m_bitnumber)
	    { m_byteptr++ ; m_bitnumber = 0 ; }
	 }
      void retreat1()
	 {
	 unsigned bytes = (8 - m_bitnumber) / 8 ;
	 m_bitnumber = (m_bitnumber - 1) & 7 ;
	 m_byteptr -= bytes ;
	 }
      void retreat(unsigned num_bits)
	 {
	 unsigned bytes = (num_bits + 7) / 8 ;
	 m_byteptr -= bytes ;
	 advance(8 * bytes - num_bits) ;
	 }
      void retreatBytes(size_t num_bytes) { m_byteptr -= num_bytes ; }
      void retreatToByte() { m_bitnumber = 0 ; }
      BitPointer &operator = (const BitPointer &old)
	 { m_byteptr = old.m_byteptr ; m_bitnumber = old.m_bitnumber ;
	   return *this ; }
      BitPointer &operator += (unsigned num_bits)
	 { advance(num_bits) ; return *this ; }
      BitPointer &operator -= (unsigned num_bits)
	 { retreat(num_bits) ; return *this ; }
      BitPointer &operator -= (const BitPointer &other)
	 {
	 if (other.m_bitnumber > m_bitnumber)
	    { m_bitnumber += 8 ; m_byteptr-- ; }
	 m_bitnumber -= other.m_bitnumber ;
	 m_byteptr -= (size_t)other.m_byteptr ;
	 return *this ;
	 }
      BitPointer &operator += (const Fr::VarBits &bits) { advance(bits.length()) ; return *this ; }
      BitPointer &operator -= (const Fr::VarBits &bits) { retreat(bits.length()) ; return *this ; }

      // comparison operators
      bool inBounds(const BitPointer &bound, unsigned num_bits) const
	 {
	    BitPointer ptr(*this) ;
	    ptr.advance(num_bits) ;
	    return ptr <= bound ;
	 }
      bool inBounds(const BitPointer &lowbound, 
		    const BitPointer &highbound) const
	 {
	    return *this >= lowbound && *this <= highbound ;
	 }
      bool operator == (const BitPointer &other) const
	 { return (m_byteptr == other.m_byteptr &&
		   m_bitnumber == other.m_bitnumber) ; }
      bool operator != (const BitPointer &other) const
	 { return (m_byteptr != other.m_byteptr ||
		   m_bitnumber != other.m_bitnumber) ; }
      bool operator < (const BitPointer &other) const
	 { return (m_byteptr < other.m_byteptr ||
		   (m_byteptr == other.m_byteptr &&
		    m_bitnumber < other.m_bitnumber)) ; }
      bool operator <= (const BitPointer &other) const
	 { return (m_byteptr < other.m_byteptr ||
		   (m_byteptr == other.m_byteptr &&
		    m_bitnumber <= other.m_bitnumber)) ; }
      bool operator > (const BitPointer &other) const
	 { return (m_byteptr > other.m_byteptr ||
		   (m_byteptr == other.m_byteptr &&
		    m_bitnumber > other.m_bitnumber)) ; }
      bool operator >= (const BitPointer &other) const
	 { return (m_byteptr > other.m_byteptr ||
		   (m_byteptr == other.m_byteptr &&
		    m_bitnumber >= other.m_bitnumber)) ; }
      
      // arithmetic operators
      size_t operator - (const BitPointer &other) const
	 { return m_byteptr - other.m_byteptr ; }

   private:
      const uint8_t *m_byteptr ;
      uint8_t  m_bitnumber ;
   } ;

/************************************************************************/
/************************************************************************/

ostream &operator << (ostream &, const Fr::VarBits &) ;
ostream &operator << (ostream &, const BitPointer &) ;

#endif /* !__BITS_H_INCLUDED */

// end of file bits.h //
