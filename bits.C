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

/************************************************************************/
/*	Manifest Constants						*/
/************************************************************************/

// number of bits for which to use a prepared table to reverse the
//   bits rather than shifting them one-by-one
// the number of reversals during table construction drops by two
//   orders of magnitude between 4 and 5, so we'd use 5 bits except
//   that partial-packet searching uses many longer bit strings that
//   need reversal
#define REVERSE_TABLE_BITS    10

/************************************************************************/
/*	Global variables						*/
/************************************************************************/

#define REVTABLE_SIZE (1<<REVERSE_TABLE_BITS)
static unsigned short bit_reverse_table[(REVERSE_TABLE_BITS+1)*REVTABLE_SIZE] ;
static bool bit_reverse_table_initialized = false ;

/************************************************************************/
/*	Helper functions						*/
/************************************************************************/

uint32_t reverse_bits(uint32_t bits, unsigned num_bits)
{
   uint32_t reversed = 0 ;
   for (size_t i = 0 ; i < num_bits ; i++)
      {
      reversed = (reversed << 1) | (bits & 1) ;
      bits >>= 1 ;
      }
   return reversed ;
}

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

void BitPointer::initBitReversal()
{
   if (!bit_reverse_table_initialized)
      {
      for (size_t bits = 0 ; bits <= REVERSE_TABLE_BITS ; bits++)
	 {
	 for (size_t value = 0 ; value < REVTABLE_SIZE  ; value++)
	    {
	    bit_reverse_table[bits * REVTABLE_SIZE + value]
	       = (unsigned short)reverse_bits(value,bits) & VariableBits::mask(bits) ;
	    }
	 }
      bit_reverse_table_initialized = true ;
      }
   return ;
}

//----------------------------------------------------------------------

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
   uint32_t bits = getBits(num_bits) ;
   if (num_bits <= REVERSE_TABLE_BITS)
      return bit_reverse_table[num_bits * REVTABLE_SIZE + bits] ;
   else
      return reverse_bits(bits,num_bits) ;
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
   if (num_bits <= REVERSE_TABLE_BITS)
      return bit_reverse_table[num_bits * REVTABLE_SIZE + bits] ;
   else
      return reverse_bits(bits,num_bits) ;
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
   if (num_bits <= REVERSE_TABLE_BITS)
      return bit_reverse_table[num_bits * REVTABLE_SIZE + bits] ;
   else
      return reverse_bits(bits,num_bits) ;
}

//----------------------------------------------------------------------

ostream &operator << (ostream &out, const BitPointer &bitptr)
{
   out << '<' << hex << (unsigned long)bitptr.bytePointer() << '.' 
       << dec << bitptr.bitNumber() << '>' << flush ;
   return out ;
}

// end of file bits.C //
