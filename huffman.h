/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  File: huffman.h - Huffman-coding classes				*/
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

#ifndef __HUFFMAN_H_INCLUDED
#define __HUFFMAN_H_INCLUDED

#include "bits.h"

/************************************************************************/
/*	Manifest Constants						*/
/************************************************************************/

#define END_OF_DATA	  256
#define MAX_LITERAL_CODES 286
#define INVALID_SYMBOL    0xFFFF

// distance and literal codes can't be more than 15 bits; codes for
//   bitlengths can't be more than 7 bits
#define MAX_HUFFMAN_LENGTH 16
// maximum number of codes with the same number of bits
#define MAX_SAME_LENGTH    240

/************************************************************************/
/*	Type definitions						*/
/************************************************************************/

typedef uint16_t HuffSymbol ;

//----------------------------------------------------------------------

class HuffmanLocation ;

class HuffmanLengthTable
   {
   private:
      unsigned	m_counts[MAX_HUFFMAN_LENGTH] ;
      HuffSymbol	m_symbols[MAX_HUFFMAN_LENGTH * MAX_SAME_LENGTH] ;
   public:
      HuffmanLengthTable() ;
      ~HuffmanLengthTable() {}

      // accessors
      unsigned count(unsigned len) const
	 { return len < MAX_HUFFMAN_LENGTH ? m_counts[len] : 0 ; }
      HuffSymbol symbol(unsigned len, unsigned index) const
	 { return (index < count(len)) 
	       ? (HuffSymbol)m_symbols[len*MAX_SAME_LENGTH + index]
	       : (HuffSymbol)INVALID_SYMBOL ; }
      HuffSymbol symbol(const HuffmanLocation &loc) const ;

      // manipulators
      void makeDefaultLiterals() ;
      void makeDefaultDistances() ;
      void addSymbol(HuffSymbol symbol, unsigned length) ;
      bool advanceLocation(HuffmanLocation *loc) const ;

      // debugging support
      void dump() const ;
   } ;

//----------------------------------------------------------------------

typedef bool HuffmanTreeIterFn(HuffSymbol sym, VariableBits codestring,
			       void *user_data) ;

class HuffmanTree
   {
   private:
      unsigned     m_bits ;	// number of bits covered by this node
      HuffmanTree **m_next ;	// subtree pointers, or NULL for leaves
      HuffSymbol      *m_symbols ;	// symbol values for leaf nodes
      VariableBits m_prefix ;	// all entries in this node share the prefix
      HuffmanTree *m_parent ;
      unsigned     m_parentloc ;// offset within parent's m_next array
   public:
      HuffmanTree(unsigned bits, VariableBits prefix,
		  HuffmanTree *parent = 0, unsigned parentloc = 0) ;
      ~HuffmanTree() ;

      // accessors
      HuffmanTree *parent() const { return m_parent ; }
      unsigned parentLocation() const { return m_parentloc ; }
      unsigned commonBits() const { return m_bits ; }
      unsigned childCount() const
	 { return commonBits() ? 1 << commonBits() : 0 ; }
      VariableBits prefix() const { return m_prefix ; }
      unsigned prefixLength() const { return m_prefix.length() ; }
      unsigned codeLength() const { return commonBits() + prefixLength() ; }
      bool nextSymbol(BitPointer &ptr, const BitPointer &str_end,
		      HuffSymbol &symbol) const ;

      // manipulators
      bool addChild(HuffmanTree *child, unsigned offset) ;
      void setParent(HuffmanTree *parent, unsigned parentloc)
	 { m_parent = parent ; m_parentloc = parentloc ; }

      bool addSymbol(HuffSymbol symbol, unsigned offset) ;
      bool advanceLocation(HuffmanLocation *loc) const ;

      bool iterate(HuffmanTreeIterFn *fn, void *user_data) const ;

      // debugging support
      void dump() const ;
   } ;

//----------------------------------------------------------------------

class HuffmanLocation
   {
   private:
      HuffmanLengthTable *m_table ;
      HuffmanTree        *m_tree ;
      unsigned            m_level ;
      unsigned            m_offset ;
   public:
      HuffmanLocation() ;
      HuffmanLocation(HuffmanLengthTable *table, unsigned length = 0) ;
      HuffmanLocation(HuffmanTree *tree) ;
      ~HuffmanLocation()
	 { m_table = 0 ; m_tree = 0 ; m_level = m_offset = 0 ; }

      // accessors
      unsigned level() const { return m_level ; }
      unsigned offset() const { return m_offset ; }
      HuffmanTree *tree() const { return m_tree ; }
      VariableBits currentCode() const ;

      // manipulators
      bool advance() ;
      void incrOffset() { m_offset++ ; }
      void newLevel(unsigned level)
	 { m_level = level ; m_offset = 0 ; }
      void newLevel(unsigned level, unsigned offset, HuffmanTree *tree)
	 { m_level = level ; m_offset = offset ; m_tree = tree ; }
      void newTree(HuffmanTree *tree) { m_tree = tree ; }
      void newTable(HuffmanLengthTable *table) { m_table = table ; }
      bool addSymbol(HuffSymbol sym, unsigned length) ;
   } ;


#endif /* !__HUFFMAN_H_INCLUDED */

// end of file huffman.h //

