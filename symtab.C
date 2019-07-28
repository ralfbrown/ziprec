/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  File: symtab.h - DEFLATE symbol tables				*/
/*  Version:  1.10beta				       			*/
/*  LastEdit: 2019-07-26						*/
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

#include "inflate.h"
#include "symtab.h"
#include "global.h"

using namespace Fr ;

/************************************************************************/
/*	Manifest constants						*/
/************************************************************************/

/************************************************************************/
/*	Global variables for this module				*/
/************************************************************************/

static Owned<HuffSymbolTable> default_symtable { nullptr } ;

static bool suppress_trace = false ;

bool trace_decomp=false;

/************************************************************************/
/*	Global data for this module					*/
/************************************************************************/

// the offsets for length codes that take additional bits -- will be added
//   to those additional bits
static const unsigned length_code_offset[] = { 0, 11, 19, 35, 67, 131 } ;

// the base values for distance codes; any additional bits as specified by
//   the dist_code_bits[] array will be added to these base values
static const unsigned dist_code_offset[] =
   { 
      1, 2, 3, 4, 5, 7, 9, 13,
      17, 25, 33, 49, 65, 97, 129, 193,
      257, 385, 513, 769, 1025, 1537, 2049, 3073,
      4097, 6145, 8193, 12289, 16385, 24577, 32769, 49153
   } ;

// the number of additional bits to retrieve following a distance code, based
//   on the value of that distance code
static const unsigned dist_code_bits[] = 
   {
      0, 0, 0, 0, 1, 1, 2, 2,
      3, 3, 4, 4, 5, 5, 6, 6,
      7, 7, 8, 8, 9, 9, 10, 10,
      11, 11, 12, 12, 13, 13, 14, 14
   } ;

static const unsigned length_index[] =
   {
      // the order in which the Huffman-encoded bit lengths of the 
      //   dynamic Huffman tree are sent by the encoder
      16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
   } ;

/************************************************************************/
/*	Globals for class SymbolTable					*/
/************************************************************************/

Fr::SmallAlloc* HuffSymbolTable::allocator = Fr::SmallAlloc::create(sizeof(HuffSymbolTable)) ;

/************************************************************************/
/*	Methods for class HuffSymbolTable				*/
/************************************************************************/

HuffSymbolTable::HuffSymbolTable(bool deflate64)
{
   m_lengthtable = nullptr ;
   m_deflate64 = deflate64 ;
   return ;
}

//----------------------------------------------------------------------

HuffSymbolTable::~HuffSymbolTable()
{
   m_lengthtable = nullptr ;
   return ;
}

//----------------------------------------------------------------------

Owned<HuffSymbolTable> HuffSymbolTable::build(BitPointer& pos, const BitPointer& str_end, bool deflate64)
{
   // blocks with dynamic Huffman tables start with five bits each to
   //   identify the number of literal codes and distance codes, four bits
   //   to identify the number of bit-length codes, then three bits for
   //   each bit-length code
   unsigned num_lit_codes = pos.nextBits(5) + 257 ;
   if (num_lit_codes > 286 && !deflate64)
      return nullptr ; // invalid data!
   unsigned num_dist_codes = pos.nextBits(5) + 1 ;
   if (num_dist_codes > 30 && !deflate64)
      return nullptr ; // invalid data!
   if (num_lit_codes == 257 && num_dist_codes > 1)
      return nullptr ; // can't have distance codes if no length literals!
   INCR_STAT(sane_dynhuff_packet) ;
   unsigned num_len_codes = pos.nextBits(4) + 4 ;
   HuffmanLengthTable bit_lengths ;
#if !defined(NDEBUG)
   if (verbosity > VERBOSITY_TREE)
      fprintf(stderr,
	      "potential packet header says %u literal, %u distance, "
	      "and %u length codes\n",
	      num_lit_codes, num_dist_codes, num_len_codes) ;
#endif /* !NDEBUG */
   unsigned lengths[19] ;
   std::fill_n(lengths,lengthof(lengths),0) ;
   for (size_t i = 0 ; i < num_len_codes ; i++)
      {
      unsigned len = pos.nextBits(3) ;
      lengths[length_index[i]] = len ;
      if (pos > str_end)
	 {
	 if (verbosity >= VERBOSITY_TREE)
	    fprintf(stderr,"Huffman-tree data extended past end of packet\n") ;
	 return nullptr ; // invalid data!
	 }
      }
   for (size_t i = 0 ; i < lengthof(lengths) ; i++)
      {
      bit_lengths.addSymbol(i,lengths[i]) ;
      }
   // convert the bit-length codes into a Huffman tree, then use that tree
   //   to decode the bit lengths of the elements of the literal-codes tree
   HuffSymbolTable bit_tab(false) ;
   bit_tab.setLengthTable(&bit_lengths) ;
   if (!bit_tab.buildHuffmanTree())
      {
      INCR_STAT(invalid_bitlength_tree) ;
      return nullptr ;
      }
   // decode the bit lengths for the literal codes, then the bit lengths for
   //   the distance codes
   HuffmanLengthTable lit_lengths ;
   HuffmanLengthTable dist_lengths ;
   if (!decode_bit_lengths(num_lit_codes, lit_lengths, num_dist_codes, dist_lengths, &bit_tab, pos, str_end))
      {
      INCR_STAT(invalid_bit_lengths) ;
      if (verbosity >= VERBOSITY_TREE)
	 cerr << " :: decode_bit_lengths failed!"<<endl;
      return nullptr ; // invalid Huffman table
      }
   // now convert the two sets of bit lengths into Huffman trees for literal
   //   and distance codes
   auto symtab = new HuffSymbolTable(deflate64) ;
   if (symtab)
      {
      symtab->setLengthTable(&lit_lengths) ;
      symtab->buildHuffmanTree() ;
      symtab->setLengthTable(&dist_lengths) ;
      symtab->buildHuffmanTree(true) ;
      symtab->setLengthTable(nullptr) ; // don't leave dangling pointer
      }
   return symtab ;
}

//----------------------------------------------------------------------

Owned<HuffSymbolTable> HuffSymbolTable::buildDefault(bool deflate64)
{
   Owned<HuffSymbolTable> symtab(deflate64) ;
   symtab->makeDefaultTrees() ;
   return symtab ;
}

//----------------------------------------------------------------------

bool HuffSymbolTable::nextSymbol(BitPointer &pos, const BitPointer &str_end, HuffSymbol &symbol) const
{
   if (m_codetree)
      return m_codetree->nextSymbol(pos,str_end,symbol) ;
   symbol = INVALID_SYMBOL ;
   return false ;
}

//----------------------------------------------------------------------

bool HuffSymbolTable::nextValue(BitPointer &pos, const BitPointer &str_end, HuffSymbol &value) const
{
   value = INVALID_SYMBOL ;
   HuffSymbol symbol ;
   if (!nextSymbol(pos,str_end,symbol))
      return false ;
   if (trace_decomp)
      {
      cerr << ' ' << symbol ;
      if (symbol == END_OF_DATA) trace_decomp = false ;
      }
   if (symbol <= 264)
      {
      // no additional bits needed
      value = symbol ;
      return true ;
      }
   unsigned extra ;
   if (symbol < 285)
      extra = (symbol - 261) >> 2 ;
   else if (symbol == 285)
      extra = (m_deflate64 ? 16 : 0) ;
   else
      return false ;
   if (pos.inBounds(str_end,extra))
      {
      value = symbol ;
      return true ;
      }
   value = INVALID_SYMBOL ;
   return false ;
}

//----------------------------------------------------------------------

bool HuffSymbolTable::advance(BitPointer &pos, const BitPointer &str_end) const
{
   HuffSymbol symbol ;
   if (!nextSymbol(pos,str_end,symbol))
      {
      return false ;
      }
   if (symbol > END_OF_DATA)
      {
      // this is a length code, so advance over any extra bits and
      //   then get the distance code
      if (symbol == 285)
	 {
	 if (m_deflate64)
	    pos.advance(16) ;
	 }
      else if (symbol >= 265)
	 {
	 pos.advance((symbol - 261) >> 2) ;
	 }
      if (!m_distancetree || !m_distancetree->nextSymbol(pos,str_end,symbol))
	 return false ;
      if (symbol >= 4 && (symbol < 30 || (symbol >= 30 && m_deflate64)))
	 pos.advance(dist_code_bits[symbol]) ;
      return pos <= str_end ;
      }
   return true ;
}

//----------------------------------------------------------------------

unsigned HuffSymbolTable::getLength(unsigned code, BitPointer &pos) const
{
   if (code < 257)
      return LITERAL_LENGTH ;
   else if (code < 265)
      return code - 254 ;
   else if (code < 285)
      {
      code -= 261 ;   // we want one-based after the shift, so sub four less
      unsigned hi = code & 3 ;
      code >>= 2 ;
      unsigned offset = length_code_offset[code] ;
      return offset + ((hi << code) | pos.nextBits(code)) ;
      }
   else if (code == 285)
      {
      if (m_deflate64)
	 return 3 + pos.nextBits(16) ;
      else
	 return 258 ;
      }
   // invalid code!
   return INVALID_LENGTH ;
}

//----------------------------------------------------------------------

unsigned HuffSymbolTable::getDistance(BitPointer& pos, const BitPointer& str_end) const
{
   HuffSymbol code ;
   if (!m_distancetree || !m_distancetree->nextSymbol(pos,str_end,code))
      return INVALID_DISTANCE ;
   if (trace_decomp) { cerr << '/' << code ; }
   if (code < 4)
      return dist_code_offset[code] ;
   else if (code >= 30 && !m_deflate64)
      return INVALID_DISTANCE ;
   else
      return dist_code_offset[code] + pos.nextBits(dist_code_bits[code]) ;
}

//----------------------------------------------------------------------

void HuffSymbolTable::makeDefaultTrees()
{
   if (verbosity >= VERBOSITY_TREE)
      fprintf(stderr,"building default symbol table\n") ;
   HuffmanLengthTable code_lengths ;
   code_lengths.makeDefaultLiterals() ;
   HuffmanLengthTable dist_lengths ;
   dist_lengths.makeDefaultDistances() ;
   setLengthTable(&code_lengths) ;
   suppress_trace = true ;
   buildHuffmanTree() ;
   setLengthTable(&dist_lengths) ;
   buildHuffmanTree(true) ;
   setLengthTable(nullptr) ;
   suppress_trace = false ;
   if (verbosity >= VERBOSITY_TREE)
      fprintf(stderr,"default symbol table built\n") ;
   return ;
}

//----------------------------------------------------------------------

bool HuffSymbolTable::buildHuffmanTree(bool build_distance_tree)
{
   assert(m_lengthtable != nullptr) ;
   unsigned length = 1 ;
   for ( ;
	 length < MAX_HUFFMAN_LENGTH && m_lengthtable->count(length) == 0 ;
	 length++)
      ;
   if (length >= MAX_HUFFMAN_LENGTH)
      {
      if (verbosity >= VERBOSITY_TREE)
	 fprintf(stderr,"Empty Huffman table!\n") ;
      return false ;
      }
   VariableBits prefix ;
   Owned<HuffmanTree> tree_root(length,prefix) ;
   HuffmanLocation table_loc(m_lengthtable,length) ;
   HuffmanLocation tree_loc(tree_root) ;
   bool more_table = true ;
   bool more_tree = true ;
   do {
      HuffSymbol symbol = m_lengthtable->symbol(table_loc) ;
      if (symbol == INVALID_SYMBOL)
	 {
	 if (verbosity >= VERBOSITY_TREE && !suppress_trace)
	    fprintf(stderr,"Huffman tree: encountered invalid bit string\n") ;
	 break ;
	 }
#if 0
      if (verbosity > VERBOSITY_TREE && !suppress_trace)
	 fprintf(stderr,"  next symbol is %u, length %u\n",
		 symbol, table_loc.level()) ;
#endif /* 0 */
      if (!tree_loc.addSymbol(symbol,table_loc.level()))
	 break ;
      if (symbol == END_OF_DATA && !build_distance_tree)
	 {
	 m_eod = tree_loc.currentCode() ;
	 if (verbosity >= VERBOSITY_TREE)
	    {
	    cout << "Huffman tree: end of data symbol is " << m_eod << endl ;
	    }
	 }
      more_table = table_loc.advance() ;
      more_tree = tree_loc.advance() ;
      } while (more_table && more_tree) ;
   if (more_table)
      {
      // the given set of bit lengths does not correspond to a valid
      //   Huffman tree
      if (verbosity > VERBOSITY_TREE)
	 {
	 unsigned excess = 0 ;
	 while (table_loc.advance())
	    excess++ ;
	 fprintf(stderr,"Huffman tree: too many values (%u extra) in length table!\n",
		 excess) ;
	 }
      return false ;
      }
   if (verbosity >= VERBOSITY_TREE)
      {
      if (suppress_trace)
	 cout << "Huffman tree sucessfully built" << endl ;
      else
	 tree_root->dump() ;
      }
   if (build_distance_tree)
      m_distancetree = tree_root ;
   else
      m_codetree = tree_root ;
   return true ;
}

//----------------------------------------------------------------------

bool HuffSymbolTable::iterateCodeTree(HuffmanTreeIterFn *fn, void *udata) const
{
   return m_codetree ? m_codetree->iterate(fn,udata) : false ;
}

//----------------------------------------------------------------------

bool HuffSymbolTable::iterateDistTree(HuffmanTreeIterFn *fn, void *udata) const
{
   return m_distancetree ? m_distancetree->iterate(fn,udata) : false ;
}

//----------------------------------------------------------------------

void HuffSymbolTable::dump() const
{
   if (m_codetree)
      {
      cerr << "SymbolTable -- literal/length tree:" << endl ;
      m_codetree->dump() ;
      }
   if (m_distancetree)
      {
      cerr << "SymbolTable -- distance tree:" << endl ;
      m_distancetree->dump() ;
      }
   return ;
}

/************************************************************************/
/************************************************************************/

bool decode_bit_lengths(unsigned lit_count,
			HuffmanLengthTable &lit_lengths,
			unsigned dist_count,
			HuffmanLengthTable &dist_lengths,
			const HuffSymbolTable *bit_tab,
			BitPointer &pos, const BitPointer &str_end)
{
   HuffmanLengthTable *lengths = &lit_lengths ;
   HuffSymbol prev_length = 0 ;
   unsigned count = lit_count + dist_count ;
   unsigned adj = 0 ;
   for (size_t i = 0 ; i < count ; )
      {
      //decode bit length
      HuffSymbol bit_length ;
      if (!bit_tab->nextSymbol(pos,str_end,bit_length))
	 {
	 return false ;  // ran out of data or invalid symbol
	 }
      if (bit_length > 18)
	 {
#if !defined(NDEBUG)
	 if (verbosity > VERBOSITY_SEARCH)
	    fprintf(stderr,"decode_bit_lengths: invalid length code %u\n",
		    bit_length) ;
#endif /* !NDEBUG */
	 return false ; // invalid data!
	 }
      unsigned copy_count ;
      unsigned len ;
      if (bit_length < 16)
	 {
	 prev_length = len = bit_length ;
	 copy_count = 1 ;
	 }
      else if (bit_length == 16)
	 {
	 if (i == 0)
	    return false ;  // invalid data -- no previous length to copy!
	 copy_count = 3 + pos.nextBits(2) ;
	 len = prev_length ;
	 }
      else if (bit_length == 17)
	 {
	 copy_count = 3 + pos.nextBits(3) ;
	 len = 0 ;
	 }
      else // bit_length == 18
	 {
	 copy_count = 11 + pos.nextBits(7) ;
	 len = 0 ;
	 }
      for ( ; copy_count > 0 && i < count ; copy_count--)
	 {
	 if (i == END_OF_DATA && len == 0)
	    {
	    return false ;
	    }
#if !defined(NDEBUG) && defined(DEBUG)
	 if (verbosity > VERBOSITY_SEARCH)
	    {
	    fprintf(stderr," %u:%u",(unsigned)(i-adj),len) ;
	    if (i % 8 == 7) fprintf(stderr,"\n") ;
	    }
#endif /* !NDEBUG && DEBUG */
	 lengths->addSymbol(i++ - adj,len) ;
	 // the two sets of bit lengths are treated as contiguous, allowing
	 //   copy instructions to span the boundary, so we need to switch
	 //   from code to distance values once we've filled in all the
	 //   code lengths
	 if (i >= lit_count && adj == 0)
	    {
	    adj = lit_count ;
	    lengths = &dist_lengths ;
	    }
	 }
      if (copy_count > 0)
	 {
	 return false ; // invalid data -- too many bit lengths
	 }
      if (lit_lengths.count(0) == lit_count ||
	  (dist_count > 1 && dist_lengths.count(0) == dist_count))
	 {
	 // table is all zeros, which is not allowed
	 return false ;
	 }
      }
#if !defined(NDEBUG)
   if (verbosity > VERBOSITY_SEARCH)
      {
#if DEBUG
      fprintf(stderr,"\n") ;
#endif /* DEBUG */
      fprintf(stderr,"successfully decoded bit lengths\n") ;
      }
#endif /* !NDEBUG */
   return true ;
}

//----------------------------------------------------------------------

bool valid_symbol_table_header(BitPointer &pos, bool deflate64)
{
   unsigned num_lit_codes = pos.nextBits(5) + 257 ;
   if (num_lit_codes > 286 && !deflate64)
      return false ; // invalid data!
   unsigned num_dist_codes = pos.nextBits(5) + 1 ;
   if (num_dist_codes > 30 && !deflate64)
      return false ; // invalid data!
   if (num_lit_codes == 257 && num_dist_codes > 1)
      return false ; // can't have distance codes if no length literals!
   unsigned num_len_codes = pos.nextBits(4) + 4 ;
   HuffmanLengthTable bit_lengths ;
   unsigned lengths[NUM_BIT_LENGTHS] ;
   std::fill_n(lengths,lengthof(lengths),0) ;
   for (size_t i = 0 ; i < num_len_codes ; i++)
      {
      unsigned len = pos.nextBits(3) ;
      lengths[length_index[i]] = len ;
      }
   for (size_t i = 0 ; i < lengthof(lengths) ; i++)
      {
      bit_lengths.addSymbol(i,lengths[i]) ;
      }
   // convert the bit-length codes into a Huffman tree, then use that tree
   //   to decode the bit lengths of the elements of the literal-codes tree
   HuffSymbolTable bit_tab(deflate64) ;
   bit_tab.setLengthTable(&bit_lengths) ;
   if (!bit_tab.buildHuffmanTree())
      {
      return false ;
      }
   // decode the bit lengths for the literal codes, then the bit lengths for
   //   the distance codes
   HuffmanLengthTable lit_lengths ;
   HuffmanLengthTable dist_lengths ;
   BitPointer str_end(pos) ;
   str_end.advance(4000) ; // allow up to 500 bytes for trees
   bool success = decode_bit_lengths(num_lit_codes, lit_lengths, num_dist_codes, dist_lengths, &bit_tab,
				     pos, str_end) ;
   return success ;
}

// end of file symtab.C //
