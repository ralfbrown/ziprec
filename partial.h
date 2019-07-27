/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  File: partial.C - reconstruction of partial DEFLATE packet		*/
/*  Version:  1.10beta				       			*/
/*  LastEdit: 2019-07-26						*/
/*									*/
/*  (c) Copyright 2012,2013,2019 Carnegie Mellon University		*/
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

#ifndef __PARTIAL_H_INCLUDED
#define __PARTIAL_H_INCLUDED

#include "framepac/byteorder.h"
#include "framepac/memory.h"
#include "framepac/object.h"

/************************************************************************/
/*	Manifest constants						*/
/************************************************************************/

// the maximum length of the Huffman code for a symbol is set by the
//   file format, which only provides four bits for the bit lengths
//   in the compressed encoding of the Huffman tree
#define MAX_BITLENGTH 15

// define the bitmasks and shifts to pack the data items for a
//   HuffmanChildInfo into 16 bits
// is the info valid?
#define NODEINFO_VALID	        0x8000
// is the child node a leaf?
#define NODEINFO_LEAF 		0x4000
// if not a leaf, the info contains the index of the child node
#define NODEINFO_CHILD_MASK     0x01FF

// maximum symbol value is 285, so we need nine bits
#define NODEINFO_SYMBOL_MASK 	0x01FF
#define NODEINFO_SYMBOL_SHIFT 	0
// maximum extra length bits = 5 (16 for DEFLATE64), distance = 13 (14),
//   so we need five bits to support DEFLATE64
#define NODEINFO_EXTRA_MASK 	0x3E00
#define NODEINFO_EXTRA_SHIFT 	9
#define EXTRA_ISLITERAL (NODEINFO_EXTRA_MASK >> NODEINFO_EXTRA_SHIFT)

// how many extra bits can we have on the length code?
#define MAX_LENGTH_EXTRABITS    5	// 32K-window standard DEFLATE
#define MAX_LENGTH_EXTRABITS64 16	// DEFLATE64

// how many extra bits can we have on the distance code?
#define MAX_DISTANCE_EXTRABITS   13	// 32-window standard DEFLATE
#define MAX_DISTANCE_EXTRABITS64 14	// DEFLATE64

#define LIT_SYMBOLS MAX_LITERAL_CODES	// number of literal/length symbols
#define MAX_LENGTH_SYMBOLS (LIT_SYMBOLS - END_OF_DATA)

#ifdef SUPPORT_DEFLATE64
#  define DIST_SYMBOLS  32		// number of distance symbols
#  define MAX_EXTRABITS 16		// maximum possible extra bits
#  define MAX_EXTENSION (MAX_BITLENGTH + MAX_LENGTH_EXTRABITS64)
#else // !SUPPORT_DEFLATE64
#  define DIST_SYMBOLS  30		// number of distance symbols
#  define MAX_EXTRABITS 13		// maximum possible extra bits
#  define MAX_EXTENSION (MAX_BITLENGTH + MAX_DISTANCE_EXTRABITS)
#endif /* SUPPORT_DEFLATE64 */

// special wildcard value for the symbol
#define NODEINFO_SYMBOL_UNKNOWN (NODEINFO_SYMBOL_MASK)

// define the bit fields used by CodeHypothesis
#define SH_CODE_MASK       0x7FFF
#define SH_ISLITERAL_MASK  0x8000
#define SH_LENGTH_MASK     0x0F0000
#define SH_LENGTH_SHIFT    16
#define SH_EXTRA_MASK      0xF00000
#define SH_EXTRA_SHIFT     20

// definitions for HuffmanTreeHypothesis
#define HYP_NOT_FOUND UINT_MAX
#define CODE_HYP_BUCKET_SIZE 8
#define CODE_HYP_BUCKETS ((LIT_SYMBOLS / CODE_HYP_BUCKET_SIZE) + 1)

/************************************************************************/
/************************************************************************/

#if MAX_BITLENGTH <= 16
typedef uint16_t HuffmanCode ;
#else
typedef uint32_t HuffmanCode ;
#endif

//----------------------------------------------------------------------

class CodeHypothesis
   {
   private:
      Fr::UInt24 m_value ;
   public:
      CodeHypothesis() {}
      ~CodeHypothesis() {}

      // accessors
      unsigned code() const
	 { return m_value.load() & SH_CODE_MASK ; }
      unsigned codeValue() const
	 { return code() >> (MAX_BITLENGTH - length()) ; }
      bool isLiteral() const
	 { return (m_value.load() & SH_ISLITERAL_MASK) != 0 ; }
      unsigned length() const
	 { return (m_value.load() & SH_LENGTH_MASK) >> SH_LENGTH_SHIFT ; }
      unsigned extraBits() const
	 { return (m_value.load() & SH_EXTRA_MASK) >> SH_EXTRA_SHIFT ; }
         // note that to support DEFLATE64, the above result needs to be
         //   passed through a lookup that converts 15 to 16
      uint32_t hashValue() const
	 { return m_value.load() | 0x01000000 ; }

      // modifiers
      void set(HuffmanCode code, unsigned length, unsigned extra)
	 {
	    code <<= (MAX_BITLENGTH - length) ;
	    code |= ((length << SH_LENGTH_SHIFT) & SH_LENGTH_MASK) ;
	    if (extra == EXTRA_ISLITERAL)
	       {
	       code |= SH_ISLITERAL_MASK ;
	       }
	    else
	       code |= ((extra << SH_EXTRA_SHIFT) & SH_EXTRA_MASK) ;
	    m_value.store(code) ;
	 }
      void setCode(unsigned code)
	 {
	    code &= SH_CODE_MASK ;
	    code |= (m_value.load() & (SH_EXTRA_MASK | SH_LENGTH_MASK)) ;
	    if (isLiteral())
	       code |= SH_ISLITERAL_MASK ;
	    m_value.store(code) ;
	 }
      void setLiteral(bool is_lit)
	 { if (is_lit)
	       m_value |= SH_ISLITERAL_MASK ;
	    else
	       m_value &= ~SH_ISLITERAL_MASK ;
	 }
      void setLength(unsigned len)
	 {
	    m_value.store((m_value.load() & ~SH_LENGTH_MASK) | ((len << SH_LENGTH_SHIFT) & SH_LENGTH_MASK)) ;
	 }
      void setExtraBits(unsigned extra)
	 {
	    m_value.store((m_value.load() & ~SH_EXTRA_MASK) | ((extra << SH_EXTRA_SHIFT) & SH_EXTRA_MASK)) ;
         // note that to support DEFLATE64, 'extra' above must first
         //   be passed through a lookup which maps 16 to 15
	 }
   } ;

//----------------------------------------------------------------------

class HuffmanTreeHypothesis
   {
   protected:
      HuffmanTreeHypothesis(const HuffmanTreeHypothesis *orig) ;
      static CodeHypothesis *newCodeBuffer(unsigned num_codes) ;
      void allocateCodeBuffer() ;
      void releaseCodeBuffer() ;
      void initLeftmost() ;
      void initRightmost() ;
      void computeHashCode() ;
      unsigned augmentTree(HuffmanCode code, unsigned length,
			   unsigned extra,
			   CodeHypothesis *&augmented) const ;
   public:
      void *operator new(size_t) { return allocator->allocate() ; }
      void operator delete(void *blk) { allocator->release(blk) ; }

      HuffmanTreeHypothesis(unsigned max_codes) ;
      HuffmanTreeHypothesis(const HuffmanTreeHypothesis *orig, CodeHypothesis *codes, unsigned num_codes) ;
      ~HuffmanTreeHypothesis() ;

      // utility
      static void initializeCodeAllocators() ;
      static void releaseCodeAllocators() ;
      static HuffmanCode canonicalized(HuffmanCode code, unsigned length)
	 { return (code << (MAX_BITLENGTH - length)) ; }

      // accessors
      bool good() const { return m_codes != nullptr ; }
      HuffmanTreeHypothesis *next() const { return m_next ; }
      HuffmanTreeHypothesis *prev() const { return m_prev ; }
      const HuffmanTreeHypothesis *parent() const { return m_parent ; }
      class TreeDirectory *treeDirectory() const ;
      unsigned symbolCount() const { return m_used ; }
      uint32_t referenceCount() const { return m_refcount ; }
      uint32_t hashCode() const { return m_hashcode ; }
      unsigned minimumBitLength() const { return m_minlength ; }
      unsigned maximumBitLength() const { return m_maxlength ; }
      unsigned maxCodeLength() const ;
      unsigned maxCodes() const { return m_maxcodes ; }
      unsigned requiredLeaves() const ;

      bool sameTree(const HuffmanTreeHypothesis *other) const ;
      bool isEOD(HuffmanCode code, unsigned length) const
	 { return canonicalized(code,length) == m_EOD ; }
      bool isLiteral(unsigned index) const
	 { return m_codes[index].isLiteral() ; }
      unsigned codeLength(unsigned index) const
	 { return m_codes[index].length() ; }
      unsigned canonicalCodeValue(unsigned index) const
	 { return m_codes[index].code() ; }
      HuffmanCode codeValue(unsigned index) const
	 { return (m_codes[index].code()
		   >> (MAX_BITLENGTH - codeLength(index))) ; }
      unsigned extraBits(unsigned index) const
	 { return m_codes[index].extraBits() ; }
      unsigned extrabitPredecessors(unsigned extra) const ;
      unsigned extrabitSuccessors(unsigned extra) const ;
      unsigned findCode(HuffmanCode code, unsigned len) const ;
      unsigned findInsertionPoint(HuffmanCode code, unsigned len,
				  unsigned &prev_extra) const ;

      bool extraBitsAtLimit(unsigned extra) const ;
      bool tooManyLeaves(HuffmanCode code, unsigned length) const ;
      bool consistentWithTree(HuffmanCode code, unsigned length,
			      unsigned extra, bool &present) const ;

      // modifiers
      void addReference() { m_refcount++ ; }
      void removeReference() ;

      void setNext(HuffmanTreeHypothesis *n) { m_next = n ; }
      void setPrev(HuffmanTreeHypothesis *p) { m_prev = p ; }

      void setMinBitLength(unsigned len)
	 { m_minlength = len < 1 ? 1 : (len <= m_maxlength) ? len : m_maxlength ;}
      void setMaxBitLength(unsigned len)  ;
      void updateLeftmost(HuffmanCode code, unsigned length) ;
      void updateRightmost(HuffmanCode code, unsigned length) ;
      void incrExtra(unsigned extra)
	 { if (extra <= MAX_EXTRABITS) m_extra_counts[extra]++ ; }

      // factory
      HuffmanTreeHypothesis *insert(HuffmanCode code,
				    unsigned length, unsigned extra,
				    bool is_EOD = false) const ;

      // debugging support
      void dump() const ;

   private:
      static Fr::SmallAlloc *allocator ;
      static Fr::SmallAlloc *code_allocators[CODE_HYP_BUCKETS+1] ;
      static size_t          code_alloc_used[CODE_HYP_BUCKETS+1] ;

      HuffmanTreeHypothesis *m_next ;
      HuffmanTreeHypothesis *m_prev ;
      const HuffmanTreeHypothesis *m_parent ;
      CodeHypothesis        *m_codes ;
      uint32_t		     m_hashcode ;
      uint32_t		     m_refcount ;
      HuffmanCode	     m_EOD ;
      HuffmanCode	     m_leftmost[MAX_BITLENGTH+2] ;
      HuffmanCode	     m_rightmost[MAX_BITLENGTH+1] ;
      unsigned short	     m_maxcodes ;
      unsigned short	     m_used ;
      uint8_t		     m_minlength ;
      uint8_t		     m_maxlength ;
      uint8_t		     m_min_extra ;
      uint8_t   	     m_extra_counts[MAX_EXTRABITS+1] ;
   } ;

//----------------------------------------------------------------------

class HuffmanHypothesis : public Fr::Object
   {
   public:
//      void *operator new(size_t) { return allocator.allocate() ; }
//      void operator delete(void *blk) { allocator.release(blk) ; }
      HuffmanHypothesis(const BitPointer &pos) ;
      HuffmanHypothesis(const HuffmanHypothesis*, const BitPointer& pos, size_t extension_len) ;
      ~HuffmanHypothesis() ;

      // accessors
      HuffmanHypothesis *next() const { return m_next ; }
      HuffmanHypothesis *dirNext() const { return m_dirnext ; }
      HuffmanHypothesis *dirPrev() const { return m_dirprev ; }
      bool inBackReference() const { return m_in_backref ; }
      size_t bitCount() const { return m_bitcount ; }
      size_t minBitLength() const { return m_litcodes->minimumBitLength() ; }
      size_t maxBitLength() const { return m_litcodes->maximumBitLength() ; }
      size_t minDistanceLength() const
	 { return m_distcodes->minimumBitLength() ; }
      size_t maxDistanceLength() const
	 { return m_distcodes->maximumBitLength() ; }
      const BitPointer *startPosition() const { return &m_startpos ; }
      uint32_t hashCode() const
	 { return m_litcodes->hashCode() ^ m_distcodes->hashCode() ; }
      HuffmanCode lastLiteral() const { return m_lastliteral ; }
      unsigned lastLiteralLength() const { return m_lastlitlength ; }
      unsigned lastLiteralRepeat() const { return m_lastlitcount ; }
      bool excessiveRepeats(HuffmanCode code, unsigned length) const ;
      bool sameTrees(const HuffmanHypothesis *other_hyp) const ;

      bool extraLiteralBitsAtLimit(unsigned extra) const
	 { return m_litcodes->extraBitsAtLimit(extra) ; }
      bool extraDistanceBitsAtLimit(unsigned extra) const
	 { return m_distcodes->extraBitsAtLimit(extra) ; }

      bool consistentLiteral(HuffmanCode code, unsigned length) const ;
      bool consistentMatchLength(HuffmanCode code, unsigned len_bits,
				 unsigned extra_bits) const ;
      bool consistentDistance(HuffmanCode code, unsigned dist_bits,
			      unsigned extra_bits) const ;

      // modifiers
      void setNext(HuffmanHypothesis *nxt) { m_next = nxt ; }
      void setDirNext(HuffmanHypothesis *nxt) { m_dirnext = nxt ; }
      void setDirPrev(HuffmanHypothesis *prv) { m_dirprev = prv ; }
      void inBackReference(bool backref) { m_in_backref = backref ; }
      void setMaxBitLength(size_t maxlen)
	 { if (m_litcodes) m_litcodes->setMaxBitLength(maxlen) ; }
      void updateLastLiteral(HuffmanCode code, unsigned length) ;
      void clearLastLiteral()
	 { m_lastliteral = 0 ; m_lastlitlength = 0 ; m_lastlitcount = 0 ; }

      // factories
      HuffmanHypothesis *extend(const BitPointer &position, HuffmanCode code,
				unsigned len,
				unsigned symbol = NODEINFO_SYMBOL_UNKNOWN) const ;
      HuffmanHypothesis *extend(const BitPointer &position, HuffmanCode code,
				unsigned length, unsigned extra,
				bool is_distance) const ;

      // debugging support
      void addLitCode(HuffmanCode code, unsigned length, unsigned extra,
		      unsigned symbol) ;
      void addDistCode(HuffmanCode code, unsigned length, unsigned extra,
		       unsigned symbol) ;
      void dumpLitCodes() { if (m_litcodes) m_litcodes->dump() ; }
      void dumpDistCodes() { if (m_distcodes) m_distcodes->dump() ; }
      unsigned generation() const
#ifdef TRACE_GENERATIONS
	 { return m_generation ; }
#else
	 { return 0 ; }
#endif /* TRACE_GENERATIONS */

   private:
//      static Fr::Allocator   allocator ;
      HuffmanTreeHypothesis* m_litcodes ;
      HuffmanTreeHypothesis* m_distcodes ;

      HuffmanHypothesis     *m_next ;
      HuffmanHypothesis	    *m_dirnext ;
      HuffmanHypothesis	    *m_dirprev ;
      size_t		     m_bitcount ;
      HuffmanCode	     m_lastliteral ;
      unsigned short	     m_lastlitlength ;
      unsigned short	     m_lastlitcount ;
      BitPointer	     m_startpos ;
      bool		     m_in_backref ;
#ifdef TRACE_GENERATIONS
      unsigned		     m_generation ;
#endif
   } ;


/************************************************************************/
/************************************************************************/

extern bool search(const BitPointer *s, const BitPointer *e,
		   BitPointer *p_hdr, bool deflate64) ;
extern HuffmanHypothesis *search(const BitPointer *s, const BitPointer *e,
				 const class HuffSymbolTable *) ;
extern void free_hypotheses(class HuffmanHypothesis*) ;

#endif /* !__PARTIAL_H_INCLUDED */

// end of file partial.h //
