/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  File: huffman.C - Huffman-coding classes				*/
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

#include <ctype.h>
#include <cstdlib>
#include <cstdio>
#include <iostream>

using namespace std ;

#include "global.h"
#include "huffman.h"

using namespace Fr ;

/************************************************************************/
/*	Methods for class HuffmanLengthTable				*/
/************************************************************************/

HuffmanLengthTable::HuffmanLengthTable()
{
   std::fill_n(m_counts,MAX_HUFFMAN_LENGTH,0) ;
   return ;
}

//----------------------------------------------------------------------

HuffSymbol HuffmanLengthTable::symbol(const HuffmanLocation &loc) const
{
   return symbol(loc.level(),loc.offset()) ;
}

//----------------------------------------------------------------------

void HuffmanLengthTable::makeDefaultLiterals()
{
   unsigned i ;
   for (i = 0 ; i <= 143 ; i++)
      addSymbol(i,8) ;
   for ( ; i <= 255 ; i++)
      addSymbol(i,9) ;
   for ( ; i <= 279 ; i++)
      addSymbol(i,7) ;
   for ( ; i <= 287 ; i++)
      addSymbol(i,8) ;
   return ;
}

//----------------------------------------------------------------------

void HuffmanLengthTable::makeDefaultDistances()
{
   unsigned i ;
   for (i = 0 ; i <= 31 ; i++)
      addSymbol(i,5) ;
   return ;
}

//----------------------------------------------------------------------

void HuffmanLengthTable::addSymbol(HuffSymbol symbol, unsigned length)
{
   if (length > 0)
      {
      // add the symbol to the list of symbols at the given length
      m_symbols[length * MAX_SAME_LENGTH + m_counts[length]] = symbol ;
      m_counts[length]++ ;
      }
   else
      {
      // just count the number of zero-length values so that we can verify
      //   that we didn't get a table consisting of only zeros
      m_counts[0]++ ;
      }
   return ;
}

//----------------------------------------------------------------------

bool HuffmanLengthTable::advanceLocation(HuffmanLocation* loc) const
{
   assert(loc != nullptr) ;
   unsigned len = loc->level() ;
   assert(len < MAX_HUFFMAN_LENGTH) ;
   if (loc->offset() >= count(len))
      {
      // we've hit the end of the current length, so advance to the next
      //   length which has entries
      while (++len < MAX_HUFFMAN_LENGTH && count(len) == 0)
	 ;
      if (len >= MAX_HUFFMAN_LENGTH)
	 return false ;
      loc->newLevel(len) ;
      }
   return true ;
}

//----------------------------------------------------------------------

void HuffmanLengthTable::dump() const
{
   cerr << "LengthTable: " << count(0) << " zero-length items" << endl ;
   for (size_t i = 1 ; i < MAX_HUFFMAN_LENGTH ; i++)
      {
      unsigned c = count(i) ;
      if (c == 0)
	 continue ;
      cerr << "Length " << i << ":\t" ;
      for (size_t j = 0 ; j < c ; j++)
	 {
	 cerr << symbol(i,j) << ' ' ;
	 }
      cerr << endl ;
      }
   return ;
}

/************************************************************************/
/*	Methods for class HuffmanTree					*/
/************************************************************************/

HuffmanTree::HuffmanTree(unsigned bits, VarBits prefix, HuffmanTree *parent, unsigned parentloc)
{
   m_prefix = prefix ;
   m_bits = bits ;
   size_t entries = childCount() ;
   m_next.allocate(entries) ;
   m_symbols.allocate(entries) ;
   if (m_next)
      {
      std::fill_n(m_next.begin(),entries,nullptr) ;
      }
   if (m_symbols)
      {
      std::fill_n(m_symbols.begin(),entries,INVALID_SYMBOL) ;
      }
   m_parent = parent ;
   m_parentloc = parentloc ;
   return ;
}

//----------------------------------------------------------------------

HuffmanTree::~HuffmanTree()
{
   for (size_t i = 0 ; i < childCount() ; i++)
      {
      delete m_next[i] ;
      }
   m_bits = 0 ;
   return ;
}

//----------------------------------------------------------------------

bool HuffmanTree::nextSymbol(BitPointer &ptr, const BitPointer &str_end, HuffSymbol &symbol) const
{
   if (commonBits() > 0 && m_symbols && ptr.inBounds(str_end,commonBits()))
      {
      unsigned next_bits = ptr.nextBitsReversed(commonBits()) ;
#if DEBUG
      if (verbosity >= VERBOSITY_TREE)
	 {
	 VarBits varbits(commonBits(),next_bits) ;
	 cerr << ' ' << varbits ;
	 }
#endif /* DEBUG */
      HuffSymbol sym = m_symbols[next_bits] ;
      if (sym != INVALID_SYMBOL)
	 {
#if DEBUG
	 if (verbosity >= VERBOSITY_TREE)
	    cerr << " => symbol " << sym << endl ;
#endif
	 symbol = sym ;
	 return true ;
	 }
      else if (m_next && m_next[next_bits])
	 {
	 return m_next[next_bits]->nextSymbol(ptr,str_end,symbol) ;
	 }
      }
#if DEBUG
   if (verbosity >= VERBOSITY_TREE)
      cerr << " => invalid bit string" << endl ;
#endif
   symbol = INVALID_SYMBOL ;
   return false ;
}

//----------------------------------------------------------------------

bool HuffmanTree::addChild(HuffmanTree *child, unsigned offset)
{
   if (offset < childCount())
      {
      m_next[offset] = child ;
      m_symbols[offset] = INVALID_SYMBOL ;
      if (child)
	 {
	 child->setParent(this,offset) ;
	 }
      return true ;
      }
   return false ;
}

//----------------------------------------------------------------------

bool HuffmanTree::addSymbol(HuffSymbol symbol, unsigned offset)
{
   if (offset < childCount())
      {
      if (m_next[offset])
	 {
	 delete m_next[offset] ;
	 m_next[offset] = nullptr ;
	 }
      m_symbols[offset] = symbol ;
      return true ;
      }
   return false ;
}

//----------------------------------------------------------------------

bool HuffmanTree::advanceLocation(HuffmanLocation* loc) const
{
   assert(loc != nullptr) ;
   if (loc->offset() >= childCount())
      {
      // we've hit the end of the current node, so look upwards in the
      //   tree until we reach a node that hasn't yet exhausted its children
      HuffmanTree* tree = parent() ;
      if (!tree)
	 return false ;
      unsigned parentloc = parentLocation() ;
      while (tree && parentloc + 1 >= tree->childCount())
	 {
	 parentloc = tree->parentLocation() ;
	 tree = tree->parent() ;
	 }
      if (!tree)
	 return false ;
      unsigned level = tree->codeLength() ;
      loc->newLevel(level,parentloc+1,tree) ;
      }
   return true ;
}

//----------------------------------------------------------------------

bool HuffmanTree::iterate(HuffmanTreeIterFn *fn, void *user_data) const
{
   if (!fn)
      return false ;
   for (size_t i = 0 ; i < childCount() ; i++)
      {
      VarBits code(prefix(),i,commonBits()) ;
      if (m_symbols && m_symbols[i] != INVALID_SYMBOL)
	 {
	 if (!fn(m_symbols[i],code,user_data))
	    return false ;
	 }
      else if (m_next && m_next[i])
	 {
	 m_next[i]->iterate(fn,user_data) ;
	 }
//      else
//	 return false ;
      }
   return true ;
}

//----------------------------------------------------------------------

void HuffmanTree::dump() const
{
   for (size_t i = 0 ; i < childCount() ; i++)
      {
      VarBits code(prefix(),i,commonBits()) ;
      if (m_symbols && m_symbols[i] != INVALID_SYMBOL)
	 cout << m_symbols[i] << '\t' << code << endl ;
      else if (m_next && m_next[i])
	 {
	 m_next[i]->dump() ;
	 }
//      else
//	 cout << "invalid\t" << code << endl ;
      }
   return ;
}

/************************************************************************/
/*	Methods for class HuffmanLocation				*/
/************************************************************************/

HuffmanLocation::HuffmanLocation()
{
   newLevel(0,0,nullptr) ;
   newTable(nullptr) ;
   return ;
}

//----------------------------------------------------------------------

HuffmanLocation::HuffmanLocation(HuffmanLengthTable *table,
				 unsigned length)
{
   newTable(table) ;
   newLevel(length,0,nullptr) ;
   return ;
}

//----------------------------------------------------------------------

HuffmanLocation::HuffmanLocation(HuffmanTree *tree)
{
   newTable(nullptr) ;
   if (tree)
      newLevel(tree->prefixLength(),0,tree) ;
   else
      newLevel(0,0,nullptr) ;
   return ;
}

//----------------------------------------------------------------------

VarBits HuffmanLocation::currentCode() const
{
   if (m_tree)
      {
      VarBits code(m_tree->prefix(),offset(),m_tree->commonBits()) ;
      return code ;
      }
   else
      {
      VarBits null ;
      return null ;
      }
}

//----------------------------------------------------------------------

bool HuffmanLocation::advance()
{
   incrOffset() ;
   if (m_tree)
      return m_tree->advanceLocation(this) ;
   else if (m_table)
      return m_table->advanceLocation(this) ;
   else
      return false ;
}

//----------------------------------------------------------------------

bool HuffmanLocation::addSymbol(HuffSymbol sym, unsigned length)
{
   assert(m_tree != nullptr) ;
   unsigned codelen = m_tree->codeLength() ;
   if (length < codelen)
      {
      if (verbosity >= VERBOSITY_TREE)
	 fprintf(stderr,
		 " non-monotonic lengths: requested %u but already had %u\n",
		 length, codelen) ;
      return false ;
      }
   else if (length > codelen)
      {
      // the new symbol's length is greater than the current node's total
      //   symbol length, so we need to create a sub-node to accomodate
      //   the extra bits
      auto subtree = new HuffmanTree(length - codelen, currentCode()) ;
      if (subtree)
	 {
	 // drop down into the subtree
	 m_tree->addChild(subtree,offset()) ;
	 newLevel(length,0,subtree) ;
	 }
      else
	 return false ;
      }
   m_tree->addSymbol(sym,offset()) ;
   return true ;
}

// end of file huffman.C //
