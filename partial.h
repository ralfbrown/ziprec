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

#include "huffman.h"
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

// how many extra bits can we have on the length code?
#define MAX_LENGTH_EXTRABITS    5	// 32K-window standard DEFLATE
#define MAX_LENGTH_EXTRABITS64 16	// DEFLATE64

// how many extra bits can we have on the distance code?
#define MAX_DISTANCE_EXTRABITS   13	// 32K-window standard DEFLATE
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

// definitions for HuffmanTreeHypothesis
#define HYP_NOT_FOUND UINT_MAX
#define CODE_HYP_BUCKET_SIZE 4
#define CODE_HYP_BUCKETS ((LIT_SYMBOLS / CODE_HYP_BUCKET_SIZE) + 1)

/************************************************************************/
/************************************************************************/

typedef uint16_t HuffmanCode ;

//----------------------------------------------------------------------

class HuffmanChildInfo
   {
   public:
      static constexpr unsigned LITERAL = 0x1F ;	// all bits set in m_extra
      static constexpr unsigned UNKNOWN = 0x1FF ;	// all bits set in m_symbol

   public:
      HuffmanChildInfo() : m_valid(0), m_leaf(0), m_extra(0), m_symbol(0) {}
      ~HuffmanChildInfo() = default ;

      // accessors
      bool isValid() const { return m_valid != 0 ; }
      bool isLeaf() const { return m_leaf != 0 ; }
      bool isLiteral() const { return m_extra == LITERAL ; }
      bool isUnknown() const { return m_symbol == UNKNOWN ; }
      unsigned childIndex() const { return m_symbol ; }
      unsigned symbol() const { return m_symbol ; }
      unsigned extraBits() const { return m_extra ; }
      
      static bool isLiteral(unsigned extra) { return extra == LITERAL ; }
      static bool isUnknown(unsigned lit) { return lit == UNKNOWN ; }

      // modifiers
      void markValid() { m_valid = 1 ; }
      void markNonLeaf() { m_leaf = 0 ; }
      void markAsLeaf() { m_leaf = 1 ; }
      void markAsLeaf(unsigned sym) { setSymbol(sym) ; markValid() ; markAsLeaf() ; }
      void setSymbol(unsigned sym)  { m_symbol = sym  ; }
      void setExtraBits(unsigned extra) { m_extra = extra ; }
      void makeLiteral(unsigned sym = UNKNOWN) { m_extra = LITERAL ; markAsLeaf(sym) ; }
      void setChild(uint16_t index) { markNonLeaf() ; setSymbol(index) ; markValid() ; }

   private: // total size = 16 bits
      // is this information valid?
      unsigned short m_valid:1 ;
      // is this a leaf or an internal node in the search tree?
      unsigned short m_leaf:1 ;
      // how many extra bits do we need to consume from the stream
      //   length: up to 5 extra for regular deflate, 16 for DEFLATE64 (this requires 5 bits to store)
      //   distance: up to 13 extra bits for regular deflate, 14 for DEFLATE64
      unsigned short m_extra:5 ;
      // the huffman symbol (0-285) or a literal byte value
      //   store a literal if m_extra has all bits set, which could not be a valid number of extra bits
      unsigned short m_symbol:9 ;
   } ;


//----------------------------------------------------------------------

class CodeHypothesis
   {
   public:
      CodeHypothesis() = default ;
      ~CodeHypothesis() = default ;

      // accessors
      unsigned code() const { return m_value.load() & CODE_MASK ; }
      unsigned codeValue() const { return code() >> (MAX_BITLENGTH - length()) ; }
      bool isLiteral() const
	 { return (m_value.load() & LITERAL_MASK) != 0 ; }
      unsigned length() const { return m_length ; }
      unsigned extraBits() const { return m_extra ; }
         // note that to support DEFLATE64, the above result needs to be
         //   passed through a lookup that converts 15 to 16
      uint32_t hashValue() const { return m_value.load() ^ (m_length << 15) ^ (m_extra << 18) ; }
         // (the above generates a 22-bit hash code)

      // modifiers
      void set(HuffmanCode code, unsigned length, unsigned extra)
	 {
	    m_length = length ;
	    code <<= (MAX_BITLENGTH - length) ;
	    if (HuffmanChildInfo::isLiteral(extra))
	       {
	       code |= LITERAL_MASK ;
	       }
	    else
	       m_extra = extra ;  // note: for DEFLATE64, 16 must be mapped to 15!
	    m_value.store(code) ;
	 }
      void setCode(unsigned code)
	 {
	    code &= CODE_MASK ;
	    if (isLiteral())
	       code |= LITERAL_MASK ;
	    m_value.store(code) ;
	 }
      void setLiteral(bool is_lit)
	 { if (is_lit)
	       m_value |= LITERAL_MASK ;
	    else
	       m_value.store(m_value.load() & ~LITERAL_MASK) ;
	 }
      void setLength(unsigned len) { m_length = len ; }
      void setExtraBits(unsigned extra)
	 {
	    m_extra = extra ; 
         // note that to support DEFLATE64, 'extra' above must first  be passed through a lookup which maps 16 to 15
	 }
   private:
      static constexpr unsigned CODE_MASK = 0x7FFF ;
      static constexpr unsigned LITERAL_MASK = 0x8000 ;
   private:
      Fr::UInt16 m_value ;
      uint8_t    m_length:4 ;
      uint8_t    m_extra:4 ;
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
      void *operator new(size_t) { return allocator.allocate() ; }
      void operator delete(void *blk) { allocator.release(blk) ; }
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
      HuffmanHypothesis* extend(const BitPointer& position, HuffmanCode code, unsigned len,
	 			unsigned symbol = HuffmanChildInfo::UNKNOWN) const ;
      HuffmanHypothesis* extend(const BitPointer& position, HuffmanCode code, unsigned length, unsigned extra,
				bool is_distance) const ;

      // debugging support
      void addLitCode(HuffmanCode code, unsigned length, unsigned extra,
		      unsigned symbol) ;
      void addDistCode(HuffmanCode code, unsigned length, unsigned extra,
		       unsigned symbol) ;
      void dumpLitCodes() const { if (m_litcodes) m_litcodes->dump() ; }
      void dumpDistCodes() const { if (m_distcodes) m_distcodes->dump() ; }
      unsigned generation() const
#ifdef TRACE_GENERATIONS
	 { return m_generation ; }
#else
	 { return 0 ; }
#endif /* TRACE_GENERATIONS */

   protected: // implementation functions for virtual methods
      friend class FramepaC::Object_VMT<HuffmanHypothesis> ;

      // type determination predicates
      // *** copying ***
      // *** destroying ***
      static void free_(Object* obj) { delete static_cast<HuffmanHypothesis*>(obj) ; }
      // *** I/O ***
      // *** standard info functions ***
      // *** standard access functions ***
      // *** comparison functions ***

   private:
      static Fr::Allocator   allocator ;
      static const char s_typename[] ;

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

extern bool search(const BitPointer* s, const BitPointer* e, BitPointer* p_hdr, bool deflate64) ;
extern HuffmanHypothesis *search(const BitPointer* s, const BitPointer* e, const class HuffSymbolTable*) ;
extern void free_hypotheses(class HuffmanHypothesis*) ;

#endif /* !__PARTIAL_H_INCLUDED */

// end of file partial.h //
