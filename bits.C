/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  File: bits.C - Bit-field manipulation				*/
/*  Version:  1.00gamma				       			*/
/*  LastEdit: 09may2013							*/
/*									*/
/*  (c) Copyright 2011,2012,2013 Ralf Brown/CMU				*/
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

#include <iostream>

#include "global.h"
#include "bits.h"
#include "framepac/bits.h"

using namespace Fr ;

/************************************************************************/
/*	Manifest Constants						*/
/************************************************************************/

/************************************************************************/
/*	Global variables						*/
/************************************************************************/

#define REVTABLE_SIZE (1<<REVERSE_TABLE_BITS)

/************************************************************************/
/*	Helper functions						*/
/************************************************************************/

/************************************************************************/
/*	Methods for class VariableBits					*/
/************************************************************************/

ostream &operator << (ostream &out, const VariableBits &bits)
{
   out << '{' ;
   for (unsigned i = bits.length() ; i > 0 ; i--)
      {
      unsigned bit_num = i - 1 ;
      out << ((bits.value() & (1L << bit_num)) ? '1' : '0') ;
      }
   out << '}' << flush ;
   return out ;
}

/************************************************************************/
/*	Methods for class BitPointer					*/
/************************************************************************/

uint32_t BitPointer::getBits(unsigned num_bits) const
{
   uint32_t bits ;
#ifdef __386__
   // since x86 is little-endian and permits unaligned accesses, just grab
   //   32 bits and mask out what we don't need, saving the overhead of
   //   checking how many bytes of memory to read
   bits = *((uint32_t*)m_byteptr) ;
#else
   if (m_bitnumber + num_bits <= 8)
      {
      // no byte boundary to worry about
      bits = *m_byteptr ;
      }
   else if (m_bitnumber + num_bits <= 16)
      {
      bits = (m_byteptr[1] << 8) | m_byteptr[0] ;
      }
   else if (m_bitnumber + num_bits <= 24)
      {
      bits = (m_byteptr[2] << 16) | get_word(m_byteptr) ;
      }
   else
      {
      bits = get_dword(m_byteptr) ;
      }
#endif
   return (bits >> m_bitnumber) & VariableBits::mask(num_bits) ;
}

//----------------------------------------------------------------------

uint32_t BitPointer::getBitsReversed(unsigned num_bits) const
{
   return BitReverser::reverse(getBits(num_bits),num_bits) ;
}

//----------------------------------------------------------------------

uint32_t BitPointer::nextBits(unsigned num_bits)
{
   uint32_t bits = getBits(num_bits) ;
   advance(num_bits) ;
   return bits ;
}

//----------------------------------------------------------------------

uint32_t BitPointer::nextBitsReversed(unsigned num_bits)
{
   uint32_t bits = getBits(num_bits) ;
   advance(num_bits) ;
   return BitReverser::reverse(bits,num_bits) ;
}

//----------------------------------------------------------------------

uint32_t BitPointer::prevBits(unsigned num_bits)
{
   retreat(num_bits) ;
   return getBits(num_bits) ;
}

//----------------------------------------------------------------------

uint32_t BitPointer::prevBitsReversed(unsigned num_bits)
{
   retreat(num_bits) ;
   uint32_t bits = getBits(num_bits) ;
   return BitReverser::reverse(bits,num_bits) ;
}

//----------------------------------------------------------------------

ostream &operator << (ostream &out, const BitPointer &bitptr)
{
   out << '<' << hex << (unsigned long)bitptr.bytePointer() << '.' 
       << dec << bitptr.bitNumber() << '>' << flush ;
   return out ;
}

// end of file bits.C //
