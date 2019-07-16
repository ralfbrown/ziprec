/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  File: symtab.h - DEFLATE symbol tables				*/
/*  Version:  1.10beta				       			*/
/*  LastEdit: 28jun2019							*/
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

#include "framepac/memory.h"
#include "huffman.h"

/************************************************************************/
/************************************************************************/

class HuffSymbolTable
   {
   private:
      static Fr::SmallAlloc* allocator ;
      HuffmanLengthTable *m_lengthtable ;
      HuffmanTree        *m_codetree ;
      HuffmanTree        *m_distancetree ;
      VariableBits	  m_eod ;
      bool		  m_deflate64 ;

   public:
      void *operator new(size_t) { return allocator->allocate() ; }
      void operator delete(void *blk) { allocator->release(blk) ; }
      HuffSymbolTable(bool deflate64) ;
      ~HuffSymbolTable() ;

      // accessors
      void getEOD(VariableBits &eod) const
	 { eod = m_eod ; }
      bool nextSymbol(BitPointer &pos, const BitPointer &str_end,
		      HuffSymbol &symbol) const ;
      bool nextValue(BitPointer &pos, const BitPointer &str_end,
		     HuffSymbol &symbol) const ;
      // skip over the next literal or length/distance pair
      bool advance(BitPointer &pos, const BitPointer &str_end) const ;

      unsigned getLength(unsigned code, BitPointer &pos) const ;
      unsigned getDistance(BitPointer &pos, const BitPointer &str_end) const ;

      // manipulators
      void setEOD(VariableBits &eod) { m_eod = eod ; }
      void makeDefaultTrees() ;
      void setLengthTable(HuffmanLengthTable *lt) { m_lengthtable = lt ; }
      bool buildHuffmanTree(bool build_distance_tree = false) ;

      bool iterateCodeTree(HuffmanTreeIterFn *fn, void *user_data) const ;
      bool iterateDistTree(HuffmanTreeIterFn *fn, void *user_data) const ;

      // debugging support
      void dump() const ;
   } ;

/************************************************************************/
/************************************************************************/

HuffSymbolTable *build_default_symtable(bool deflate64) ;
void clear_default_symbol_table() ;

bool valid_symbol_table_header(BitPointer &pos, bool deflate64) ;
HuffSymbolTable *build_symbol_table(BitPointer &pos, const BitPointer &str_end,
				bool deflate64 = false) ;
void free_symbol_table(HuffSymbolTable *symtab) ;

bool decode_bit_lengths(unsigned lit_count,
			HuffmanLengthTable &lit_lengths,
			unsigned dist_count,
			HuffmanLengthTable &dist_lengths,
			const HuffSymbolTable *bit_tab,
			BitPointer &pos, const BitPointer &str_end) ;

// end of file symtab.h //
