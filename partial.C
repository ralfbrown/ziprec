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

#include <algorithm>
#include <climits>
#include <iomanip>
#include "bits.h"
#include "inflate.h"
#include "partial.h"
#include "symtab.h"
#include "global.h"
#include "framepac/config.h"
#include "framepac/memory.h"
#include "framepac/priqueue.h"
#include "framepac/smartptr.h"
#include "framepac/texttransforms.h"
#include "framepac/timer.h"

using namespace Fr ;

/************************************************************************/
/*	Manifest constants						*/
/************************************************************************/

// conditional compilation options
//#define SUPPORT_DEFLATE64	// not yet fully implemented
//#define TRACE_GENERATIONS

// the maximum number of search nodes to keep in the queue; this determines
//   how much memory the search consumes
//#define MAX_SEARCH 2200000  // approx 1.8GB for HuffmanHypothesis instances
//#define MAX_SEARCH 12000000  // approx 10GB for HuffmanHypothesis instances
#define MAX_SEARCH 42000000  // approx 13GB total memory use

// the maximum number of un-extendable search nodes to keep for final
//   decompression
//#define MAX_LONGEST 5000
#define MAX_LONGEST 100

// uncomment the appropriate definition below for the desired search type
//#define SEARCH_QUEUE_SIZE 0		// best-first via priority queue
//#define SEARCH_QUEUE_SIZE INT_MAX	// depth-first via recursion
//#define SEARCH_QUEUE_SIZE 1		// depth-first, then breadth-first
#define SEARCH_QUEUE_SIZE MAX_EXTENSION // breadth-first via per-len stacks

// length in bits at which to switch from DFS to BFS
#define DFS_TO_BFS_THRESHOLD 128

// length in bits below which not to bother keeping a consistent stream
#define KEEP_NONE_THRESHOLD 1024  // 128 bytes

// length in bits above which to keep all consistent streams found
#define KEEP_ALL_THRESHOLD 16384  // 2048 bytes

// how often do we output a character to show search progress?
#define EXPANSION_REPORT_INTERVAL 1000000	// attempted expansions

// some heuristic constraints to reduce the search space
#define NEEDED_LIT_BITS		6	// fewer than 32 symbols is silly
#define NEEDED_DIST_BITS	3	// real comp uses multiple distances
#define MAX_LITERAL_REPEATS	4	// >=3 should be repl by back-ref
#define MIN_LIT_BITS		3	// unlikely to have any > 1/8 total
#define MIN_DIST_BITS		2	// unlikely to have any > 1/4 total

//==== stuff below this point should not require configuration ====

#define ROOT_NODE 0

// how big is our index for finding duplicate HuffmanTreeHypothesis
//   instances?  Bigger means fewer collisions and thus fewer comparisons,
//   but more memory used.
#define LIT_TREE_DIR_SIZE   (1<<18)
#define DIST_TREE_DIR_SIZE  (1<<16)  // fewer possible distance trees
// how big is our index for finding duplicate HuffmanHypothesis instances?
#define HYPOTHESIS_DIR_SIZE (1<<21)

// definitions for SearchTrie and SearchTrieNode
#define TRIE_BITS     24		// how many bits of hashcode to use
#define BITS_PER_LEVEL 3		// trade off mem vs time
#define TRIE_DEPTH ((TRIE_BITS + BITS_PER_LEVEL - 1) / BITS_PER_LEVEL)
#define TRIE_MASK ((1 << BITS_PER_LEVEL) - 1)

/************************************************************************/
/*	Forward declarations						*/
/************************************************************************/

void print_partial_packet_statistics() ;

/************************************************************************/
/*	Types local to this module					*/
/************************************************************************/

class HuffmanChildInfo
   {
   public:
      HuffmanChildInfo() { m_info = 0 ; }
      ~HuffmanChildInfo() = default ;

      // accessors
      bool isValid() const { return (m_info & NODEINFO_VALID) != 0 ; }
      bool isLeaf() const { return (m_info & NODEINFO_LEAF) != 0 ; }
      bool isLiteral() const
	 { return (m_info & NODEINFO_EXTRA_MASK) == NODEINFO_EXTRA_MASK ; }
      unsigned childIndex() const { return (m_info & NODEINFO_CHILD_MASK) ; }
      unsigned symbol() const
	 { return (m_info & NODEINFO_SYMBOL_MASK) >> NODEINFO_SYMBOL_SHIFT ; }
      unsigned extraBits() const
	 { return (m_info & NODEINFO_EXTRA_MASK) >> NODEINFO_EXTRA_SHIFT ; }

      // modifiers
      void markValid() { m_info |= NODEINFO_VALID ; }
      void markAsLeaf() { m_info |= (NODEINFO_VALID | NODEINFO_LEAF) ; }
      void markAsLeaf(unsigned sym)
	 { m_info |= (NODEINFO_VALID | NODEINFO_LEAF | (sym & NODEINFO_SYMBOL_MASK)) ; }
      void setSymbol(unsigned sym)
	 { m_info &= ~NODEINFO_SYMBOL_MASK ;
           m_info |= ((sym << NODEINFO_SYMBOL_SHIFT) & NODEINFO_SYMBOL_MASK) ;
	 }
      void setExtraBits(unsigned extra)
	 { m_info &= ~NODEINFO_EXTRA_MASK ;
           m_info |= ((extra << NODEINFO_EXTRA_SHIFT) & NODEINFO_EXTRA_MASK) ;
	 }
      void makeLiteral(unsigned sym = NODEINFO_SYMBOL_UNKNOWN)
	 { m_info |= (NODEINFO_VALID | NODEINFO_LEAF | NODEINFO_EXTRA_MASK) ;
	   setSymbol(sym) ; }
      void setChild(uint16_t index)
	 { m_info &= ~(NODEINFO_LEAF | NODEINFO_CHILD_MASK) ;
	   m_info |= (NODEINFO_VALID | (index & NODEINFO_CHILD_MASK)) ;
	 }

   private:
      uint16_t m_info ;
   } ;

//----------------------------------------------------------------------

class HuffmanTreeNode
   {
   private:
      HuffmanChildInfo m_left ;
      HuffmanChildInfo m_right ;
   public:
      HuffmanTreeNode() {}
      ~HuffmanTreeNode() {}

      // accessors
      HuffmanChildInfo leftChild() const { return m_left ; }
      HuffmanChildInfo rightChild() const { return m_right ; }
      HuffmanChildInfo getChild(bool right) const
	 { return right ? rightChild() : leftChild() ; }
      bool leftChildValid() const { return m_left.isValid() ; }
      bool rightChildValid() const { return m_right.isValid() ; }

      bool leftValid() const { return m_left.isValid() ; }
      bool rightValid() const { return m_right.isValid() ; }
      bool leftLeaf() const { return m_left.isLeaf() ; }
      bool rightLeaf() const { return m_right.isLeaf() ; }
      bool leftLiteral() const { return m_left.isLiteral() ; }
      bool rightLiteral() const { return m_right.isLiteral() ; }
      unsigned leftExtraBits() const { return m_left.extraBits() ; }
      unsigned rightExtraBits() const { return m_right.extraBits() ; }
      unsigned leftSymbol() const { return m_left.symbol() ; }
      unsigned rightSymbol() const { return m_right.symbol() ; }

      // modifiers
      void setLeftChild(uint16_t l) { m_left.setChild(l) ; }
      void setRightChild(uint16_t r) { m_right.setChild(r) ; }
      void makeLeftLeaf(unsigned symbol = NODEINFO_SYMBOL_UNKNOWN)
	 { m_left.markAsLeaf(symbol) ; }
      void makeRightLeaf(unsigned symbol = NODEINFO_SYMBOL_UNKNOWN)
	 { m_right.markAsLeaf(symbol) ; }
      void setLeftExtraBits(unsigned extra) { m_left.setExtraBits(extra) ; }
      void setRightExtraBits(unsigned extra) { m_right.setExtraBits(extra) ; }
      void setLeftSymbol(unsigned sym) { m_left.setSymbol(sym) ; }
      void setRightSymbol(unsigned sym) { m_right.setSymbol(sym) ; }
   } ;

//----------------------------------------------------------------------

class PartialHuffmanTreeBase
   {
   private:
      const unsigned   *m_extrabit_successors ;
      const unsigned   *m_extrabit_predecessors ;
      unsigned short	m_mindepth ;
      unsigned short	m_maxdepth ;
      unsigned short    m_max_length_used ;
      unsigned short    m_nodes_used ;
      unsigned short	m_total_nodes ;
      unsigned short    m_min_extra ;

   protected:
      HuffmanCode	m_leftmost[MAX_BITLENGTH+2] ;
      HuffmanCode	m_rightmost[MAX_BITLENGTH+1] ;
      uint8_t		m_extra_counts[MAX_EXTRABITS+1] ;
   private:
      void initLeftmost() ;
      void initRightmost() ;
   public:
      PartialHuffmanTreeBase(const PartialHuffmanTreeBase &orig)
	 { m_extrabit_successors = orig.m_extrabit_successors ;
	   m_extrabit_predecessors = orig.m_extrabit_predecessors ;
	   m_mindepth = orig.m_mindepth ;
	   m_maxdepth = orig.m_maxdepth ;
	   m_max_length_used = orig.m_max_length_used ;
	   m_nodes_used = orig.m_nodes_used ;
	   m_total_nodes = orig.m_total_nodes ;
	   m_min_extra = orig.m_min_extra ;
	   memcpy(m_leftmost,orig.m_leftmost,sizeof(m_leftmost)) ;
	   memcpy(m_rightmost,orig.m_rightmost,sizeof(m_rightmost)) ;
	   memcpy(m_extra_counts,orig.m_extra_counts,sizeof(m_extra_counts)) ;
	 }
      PartialHuffmanTreeBase(unsigned size) ;
      ~PartialHuffmanTreeBase() { m_nodes_used = 0 ; }

      // accessors
      unsigned minimumBitLength() const { return m_mindepth ; }
      unsigned maximumBitLength() const { return m_maxdepth ; }
      unsigned maxCodeLength() const { return m_max_length_used ; }
      unsigned nodesUsed() const { return m_nodes_used ; }
      unsigned maxNodes() const { return m_total_nodes ; }
      unsigned requiredLeaves() const ;
      bool tooManyLeaves(HuffmanCode code, unsigned length) const ;
      bool consistentWithTree(const HuffmanTreeNode *nodes,
			      HuffmanCode code, size_t length,
			      size_t extra, bool &present) const ;

      // modifiers
      void setMinBitLength(unsigned len)
	 { m_mindepth = len < 1 ? 1 : (len <= m_maxdepth) ? len : m_maxdepth ;}
      void setMaxBitLength(unsigned len)  ;
      void updateLeftmost(HuffmanCode code, unsigned length) ;
      void updateRightmost(HuffmanCode code, unsigned length) ;
      void incrExtra(unsigned extra)
	 { if (extra <= MAX_EXTRABITS) m_extra_counts[extra]++ ; }
      unsigned allocateNode()
	 { 
	 if (m_nodes_used >= m_total_nodes) abort() ;
	 return m_nodes_used++ ;
	 }
      bool add(HuffmanTreeNode *nodes, unsigned index, 
	       HuffmanCode code, unsigned length, unsigned extra_bits,
	       unsigned symbol) ;

      // debugging support
      void dump(const HuffmanTreeNode *nodes) const ;
   } ;

//----------------------------------------------------------------------

template <unsigned SZ>
class PartialHuffmanTree : public PartialHuffmanTreeBase
   {
   private:
      HuffmanTreeNode  m_nodes[SZ] ;
      static const uint8_t s_extrabit_limits[MAX_EXTRABITS+1] ;
   public:
      PartialHuffmanTree() : PartialHuffmanTreeBase(SZ) {}
      PartialHuffmanTree(const PartialHuffmanTree &orig)
	 : PartialHuffmanTreeBase(orig)
	 { memcpy(m_nodes,orig.m_nodes,sizeof(m_nodes)) ; }
      ~PartialHuffmanTree() {}

      // accessors
      HuffmanTreeNode *node(unsigned index) const { return &m_nodes[index] ; }
      unsigned indexOf(const HuffmanTreeNode *node)
	 { return node - m_nodes ; }
      bool extraBitsAtLimit(unsigned extra) const
	 { return m_extra_counts[extra] >= s_extrabit_limits[extra] ; }
      bool consistentWithTree(HuffmanCode code, size_t length,
			      size_t extra, bool &present) const
	 { return PartialHuffmanTreeBase::consistentWithTree(m_nodes,code,
							     length,extra,
							     present) ; }

      // modifiers
      bool add(HuffmanCode code, unsigned length, unsigned extra_bits,
	       unsigned symbol = NODEINFO_SYMBOL_UNKNOWN)
	 { return PartialHuffmanTreeBase::add(m_nodes,ROOT_NODE,code,length,
					      extra_bits,symbol) ; }

      // debugging support
      void dump() const { PartialHuffmanTreeBase::dump(m_nodes) ; }
   } ;

//----------------------------------------------------------------------

class TreeDirectory
   {
   public:
      TreeDirectory(unsigned size) : m_entries(size), m_size(size)
	 { std::fill_n(m_entries.begin(),size,nullptr) ; }
      ~TreeDirectory() = default ;

      // accessors
      unsigned itemIndex(const HuffmanTreeHypothesis *hyp) const { return hyp->hashCode() % m_size ; }
      HuffmanTreeHypothesis *findDuplicate(const HuffmanTreeHypothesis *) const ;

      // modifiers
      bool insert(HuffmanTreeHypothesis *hyp) ;
      bool remove(HuffmanTreeHypothesis *hyp) ;

   private:
      NewPtr<HuffmanTreeHypothesis*> m_entries ;
      unsigned		             m_size ;
   } ;

//----------------------------------------------------------------------

class HypothesisDirectory
   {
   private:
      HuffmanHypothesis *m_entries[HYPOTHESIS_DIR_SIZE] ;
   public:
      HypothesisDirectory() { memset(m_entries,'\0',sizeof(m_entries)) ; }
      ~HypothesisDirectory() {}

      // accessors
      unsigned itemIndex(const HuffmanHypothesis *hyp) const
	 { return hyp->hashCode() % HYPOTHESIS_DIR_SIZE ; }
      HuffmanHypothesis *findDuplicate(const HuffmanHypothesis *) const ;

      // modifiers
      bool insert(HuffmanHypothesis *hyp) ;
      bool remove(HuffmanHypothesis *hyp) ;
   } ;

//----------------------------------------------------------------------

class HuffmanInfo : public Object
   {
   public:
//      void *operator new(size_t) { return allocator.allocate() ; }
//      void operator delete(void *blk) { allocator.release(blk) ; }
      HuffmanInfo(const BitPointer &pos)
	 : m_startpos(pos)
	 { m_bitcount = 0 ;
	   clearLastLiteral() ;
	   setNext(nullptr) ;}
      HuffmanInfo(const HuffmanInfo*, const BitPointer& pos, size_t extension_len) ;
      ~HuffmanInfo() {}

      // accessors
      HuffmanInfo *next() const { return m_next ; }
      size_t minBitLength() const { return m_litcodes.minimumBitLength() ; }
      size_t maxBitLength() const { return m_litcodes.maximumBitLength() ; }
      size_t bitCount() const { return m_bitcount ; }
      size_t minDistanceLength() const
	 { return m_distcodes.minimumBitLength() ; }
      size_t maxDistanceLength() const
	 { return m_distcodes.maximumBitLength() ; }
      const BitPointer *startPosition() const { return &m_startpos ; }
      HuffmanCode lastLiteral() const { return m_lastliteral ; }
      unsigned lastLiteralLength() const { return m_lastlitlength ; }
      unsigned lastLiteralRepeat() const { return m_lastlitcount ; }
      bool excessiveRepeats(HuffmanCode code, unsigned length) const ;

      bool extraLiteralBitsAtLimit(unsigned extra) const
	 { return m_litcodes.extraBitsAtLimit(extra) ; }
      bool extraDistanceBitsAtLimit(unsigned extra) const
	 { return m_distcodes.extraBitsAtLimit(extra) ; }

      bool consistentLiteral(HuffmanCode code, unsigned length) const ;
      bool consistentMatchLength(HuffmanCode code, unsigned len_bits, unsigned extra_bits) const ;
      bool consistentDistance(HuffmanCode code, unsigned dist_bits, unsigned extra_bits) const ;

      // modifiers
      void setNext(HuffmanInfo *nxt) { m_next = nxt ; }
      void setMinBitLength(size_t maxlen)
	 { m_litcodes.setMinBitLength(maxlen) ; }
      void setMaxBitLength(size_t maxlen)
	 { m_litcodes.setMaxBitLength(maxlen) ; }
      void setMinDistanceLength(size_t maxlen)
	 { m_distcodes.setMinBitLength(maxlen) ; }
      void setMaxDistanceLength(size_t maxlen)
	 { m_distcodes.setMaxBitLength(maxlen) ; }
      void updateLastLiteral(HuffmanCode code, unsigned length) ;
      void clearLastLiteral() { m_lastliteral = 0 ; m_lastlitlength = 0 ; m_lastlitcount = 0 ; }

      // factories
      HuffmanInfo *extend(const BitPointer &position, HuffmanCode code,
			  unsigned len,
			  unsigned symbol = NODEINFO_SYMBOL_UNKNOWN) const ;
      HuffmanInfo *extend(const BitPointer &position, HuffmanCode code,
			  unsigned matchlen, unsigned matchextra,
			  HuffmanCode distcode, unsigned distlen,
			  unsigned distextra) const ;

   private:
//      static Allocator allocator ;
   public:
      PartialHuffmanTree<LIT_SYMBOLS>  m_litcodes ;
      PartialHuffmanTree<DIST_SYMBOLS> m_distcodes ;

      HuffmanInfo	  *m_next ;
      HuffmanCode	   m_lastliteral ;
      unsigned short	   m_lastlitlength ;
      unsigned short	   m_lastlitcount ;
      BitPointer	   m_startpos ;
      size_t		   m_bitcount ;
   } ;

//----------------------------------------------------------------------

enum HuffmanSearchMode
   {
      SMODE_NOSEARCH,
      SMODE_DEPTHFIRST,
      SMODE_BREADTHFIRST,
      SMODE_DEPTHTHENBREADTH,
      SMODE_BESTFIRST
   } ;

//----------------------------------------------------------------------

class SearchTrieNode
   {
   public:
      void *operator new(size_t) { return allocator->allocate() ; }
      void operator delete(void *blk) { allocator->release(blk) ; }
      SearchTrieNode() { memset(m_children,'\0',sizeof(m_children)) ; }
      ~SearchTrieNode() {}

      // accessors
      bool hasDescendants() const ;
      SearchTrieNode *child(unsigned N) const { return m_children[N] ; }
      HuffmanHypothesis *leaf(unsigned N) const { return m_leaves[N] ; }

      // modifiers
      void setChild(unsigned N, SearchTrieNode *ch) { m_children[N] = ch ; }
      void setLeaf(unsigned N, HuffmanHypothesis *h) { m_leaves[N] = h ; }

   private:
      static SmallAlloc* allocator ;
      union {
	    SearchTrieNode *m_children[1 << BITS_PER_LEVEL] ;
  	    HuffmanHypothesis *m_leaves[1 << BITS_PER_LEVEL] ;
            } ;
   } ;

//----------------------------------------------------------------------

class SearchTrie
   {
   public:
      void *operator new(size_t) { return allocator->allocate() ; }
      void operator delete(void *blk) { allocator->release(blk) ; }
      SearchTrie() { m_trie = nullptr ; m_size = 0 ; }
      ~SearchTrie() ;

      // accessors
      bool nonEmpty() const { return m_trie != nullptr ; }
      size_t size() const { return m_size ; }
      HuffmanHypothesis *find(uint32_t hashcode) const ;
      bool isDuplicate(const HuffmanHypothesis *) const ;

      // modifiers
      void clear() ;
      bool insert(HuffmanHypothesis *) ;
      bool remove(HuffmanHypothesis *) ;

      // conversion
      HuffmanHypothesis *convertToList() ;

   private:
      static SmallAlloc* allocator ;
      SearchTrieNode* m_trie ;
      size_t	      m_size ;
   } ;

//----------------------------------------------------------------------

class HuffmanSearchQueue
   {
   public:
      HuffmanSearchQueue(size_t qsize, unsigned num_stacks = 0, bool allow_impl_shift = false) ;
      ~HuffmanSearchQueue() ;

      // accessors
      HuffmanSearchMode searchMode() const { return m_searchmode ; }
      HypothesisDirectory *directory() const { return m_directory.get() ; }
      size_t shiftCount() const { return m_shiftcount ; }
      bool implicitShifts() const { return m_implicitshift ; }
      bool more() const ;
      bool full() const
	 { return m_numstacks > 0 && m_queuesize >= m_maxqueue ; }
      uint64_t totalAdditions() const { return m_additions ; }
      uint64_t duplicatesSkipped() const { return m_dups_skipped ; }
      size_t queueSize() const { return m_queuesize ; }
      size_t maxSize() const { return m_maxqueue ; }
      bool duplicate(const HuffmanHypothesis *hyp) const ;

      // modifiers
      bool shift() ; // shift stacks down to eliminate leading empty stacks
      void shift(size_t count) ;  // shift stacks by specified amount
      bool conditionalShift()
	 { return (m_numstacks>0 && m_stacks[0] == nullptr) ? shift() : more() ; }
      bool trim(size_t size, bool permanent = false) ;
      bool push(HuffmanHypothesis *hyp) ;
      HuffmanHypothesis *pop() ;
      HuffmanHypothesis *popAll() ;
   protected:
      void clearStack(unsigned which) ;

   private:
      Owned<BoundedPriorityQueue> m_queue ;
      Owned<HypothesisDirectory>  m_directory ;
      NewPtr<HuffmanHypothesis*>  m_stacks ;
      uint64_t		   m_additions ;
      uint64_t		   m_dups_skipped ;
      size_t		   m_numstacks ;
      size_t		   m_queuesize ;
      size_t		   m_maxqueue ;
      size_t		   m_shiftcount ;
      HuffmanSearchMode    m_searchmode ;
      bool		   m_implicitshift ;
  } ;

/************************************************************************/
/*	Global variables for this module				*/
/************************************************************************/

SmallAlloc* HuffmanTreeHypothesis::allocator = SmallAlloc::create(sizeof(HuffmanTreeHypothesis)) ;
SmallAlloc* HuffmanTreeHypothesis::code_allocators[] = { nullptr } ;
size_t HuffmanTreeHypothesis::code_alloc_used[] = { 0 } ;

//Fr::Allocator HuffmanHypothesis::allocator("HuffmanHyp",
//					 sizeof(HuffmanHypothesis)) ;

//Fr::Allocator HuffmanInfo::allocator("HuffmanInfo",sizeof(HuffmanInfo)) ;

SmallAlloc* SearchTrieNode::allocator = SmallAlloc::create(sizeof(SearchTrieNode)) ;
SmallAlloc* SearchTrie::allocator = SmallAlloc::create(sizeof(SearchTrie)) ;

Owned<TreeDirectory> lit_tree_directory { nullptr } ;
Owned<TreeDirectory> dist_tree_directory { nullptr } ;

STATISTIC(total_expansions)
STATISTIC(search_additions)
STATISTIC(search_dups)
STATISTIC(queue_full)
STATISTIC(longest_additions)
STATISTIC(tree_insertions)
STATISTIC(tree_present)
STATISTIC(tree_conflict)
STATISTIC(tree_duplicates)

/************************************************************************/
/*	Global data for this module					*/
/************************************************************************/

// the maximum symbols with a given number of extra bits which are allowed
//   in the literal/length tree
template <>
const uint8_t PartialHuffmanTree<LIT_SYMBOLS>::s_extrabit_limits[] =
   { 9, 4, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0
#ifdef SUPPORT_DEFLATE64
     , 0, 0, 1
#endif
   } ;

// the maximum symbols with a given number of extra bits which are allowed
//   in the distance tree
template <>
const uint8_t PartialHuffmanTree<DIST_SYMBOLS>::s_extrabit_limits[] =
   { 4, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2
#ifdef SUPPORT_DEFLATE64
     , 0, 0, 0
#endif
   } ;

// the maximum symbols with a given number of extra bits which are allowed
//   in the literal/length tree
static const uint8_t extrabit_lit_limits[] =
   { 9, 4, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 } ;
// the maximum symbols with a given number of extra bits which are allowed
//   in the distance tree
static const uint8_t extrabit_dist_limits[] =
   { 4, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
#ifdef SUPPORT_DEFLATE64
     2, 0, 0 } ;
#else
     2, 0, 0 } ;
#endif

// the maximum number of symbols following one with a given number of extra
//   bits
static const unsigned extrabit_lit_successors[] =
   { 28, 20, 16, 12,  8,  4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, LIT_SYMBOLS-1 } ;
static const unsigned extrabit_dist_successors[] =
   { 29, 25, 23, 21, 19, 17, 15, 13, 11,  9, 7, 5, 3, 1, 0, 0,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0 } ;
#ifdef DEFLATE64
static const unsigned extrabit_dist_successors64[] =
   { 31, 27, 25, 23, 21, 19, 17, 15, 13, 11, 9, 7, 5, 3, 1, 0, 
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0 } ;
#endif

// the maximum number of symbols preceding one with a given number of extra
//   bits
static const unsigned extrabit_lit_predecessors[] =
   { 285, 268, 272, 276, 280, 284, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
       0,  0,    0,   0,   0,   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, END_OF_DATA } ;
static const unsigned extrabit_dist_predecessors[] =
   { 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31, 0,
     0, 0, 0, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 0 } ;

// values corresponding to strings of N set bits
static const HuffmanCode all_ones[] =
   { 
      0x0000, 0x0001, 0x0003, 0x0007, 0x000F, 0x001F, 0x003F, 0x007F,
      0x00FF, 0x01FF, 0x03FF, 0x07FF, 0x0FFF, 0x1FFF, 0x3FFF, 0x7FFF,
      0xFFFF 
   } ;

// values corresponding to the bits used by a canonicalized Huffman code
//   of length N
static const HuffmanCode code_mask[] =
   { 0,      0x4000, 0x6000, 0x7000, 0x7800, 0x7C00, 0x7E00, 0x7F00,
     0x7F80, 0x7FC0, 0x7FE0, 0x7FF0, 0x7FF8, 0x7FFC, 0x7FFE, 0x7FFF } ;

// the order in which to explore the possible end-of-data symbols
// EOD lengths of 1 through 6 imply a tiny tree and thus a small,
//   probably uninteresting, symbol set, so we'll skip them entirely
static const unsigned eod_lengths[] =
   {
      15, 0/*!!!*/, 14, 15, 13, 12, 11, 10, 9, 8, 7, 0 
   } ;

/************************************************************************/
/*	Helper functions						*/
/************************************************************************/

void free_hypotheses(HuffmanHypothesis *hyp)
{
   while (hyp)
      {
      HuffmanHypothesis *next = hyp->next() ;
      delete hyp ;
      hyp = next ;
      }
   return ;
}

//----------------------------------------------------------------------

const char *binary(HuffmanCode code, unsigned length)
{
   static char string[MAX_BITLENGTH+2] ;
   for (size_t i = 0 ; i < length ; i++)
      {
      string[i] = (code & (1 << (length -i - 1))) ? '1' : '0' ;
      }
   string[length] = '\0' ;
   return string ;
}

/************************************************************************/
/*	Methods for class SearchTrieNode				*/
/************************************************************************/

bool SearchTrieNode::hasDescendants() const
{
   for (unsigned i = 0 ; i < lengthof(m_children) ; i++)
      {
      if (child(i))
	 return true ;
      }
   return false ;
}

/************************************************************************/
/*	Methods for class SearchTrie					*/
/************************************************************************/

SearchTrie::~SearchTrie()
{
   clear() ;
   return ;
}

//----------------------------------------------------------------------

HuffmanHypothesis *SearchTrie::find(uint32_t hashcode) const
{
   SearchTrieNode *node = m_trie ;
   for (size_t i = TRIE_DEPTH-1 ; i > 0 && node ; i--)
      {
      unsigned index = (hashcode >> (i * BITS_PER_LEVEL)) & TRIE_MASK ;
      node = node->child(index) ;
      }
   return node ? node->leaf(hashcode & TRIE_MASK) : nullptr ;
}

//----------------------------------------------------------------------

bool SearchTrie::isDuplicate(const HuffmanHypothesis *hyp) const
{
   if (!hyp)
      return false ;
   HuffmanHypothesis *cand_dup = find(hyp->hashCode()) ;
   for ( ; cand_dup ; cand_dup = cand_dup->next())
      {
      if (hyp->bitCount() == cand_dup->bitCount() &&
	  hyp->sameTrees(cand_dup))
	 return true ;
      }
   return false ;
}

//----------------------------------------------------------------------

void SearchTrie::clear()
{
   HuffmanHypothesis *hyps = convertToList() ;
   free_hypotheses(hyps) ;
   return ;
}

//----------------------------------------------------------------------

bool SearchTrie::insert(HuffmanHypothesis *hyp)
{
   if (!hyp)
      return false ;
   if (!m_trie)
      {
      m_trie = new SearchTrieNode ;
      if (!m_trie)
	 return false ;
      }
   uint32_t hashcode = hyp->hashCode() ;
   SearchTrieNode *node = m_trie ;
   for (size_t i = TRIE_DEPTH - 1 ; i > 0 ; i--)
      {
      unsigned index = (hashcode >> (i * BITS_PER_LEVEL)) & TRIE_MASK ;
      SearchTrieNode *child = node->child(index) ;
      if (!child)
	 {
	 child = new SearchTrieNode ;
	 node->setChild(index,child) ;
	 }
      node = child ;
      }
   unsigned index = hashcode & TRIE_MASK ;
   HuffmanHypothesis *cand_dup = node->leaf(index) ;
   for ( ; cand_dup ; cand_dup = cand_dup->next())
      {
      if (hyp->bitCount() == cand_dup->bitCount() &&
	  hyp->sameTrees(cand_dup))
	 {
	 // hypothesis already exists in trie
	 return false ;
	 }
      }
   hyp->setNext(node->leaf(index)) ;
   node->setLeaf(index,hyp) ;
   m_size++ ;
   return true ;
}

//----------------------------------------------------------------------

bool SearchTrie::remove(HuffmanHypothesis *hyp)
{
   if (!hyp)
      return false ;
   SearchTrieNode *path[TRIE_DEPTH] ;
   unsigned path_index[TRIE_DEPTH] ;
   // descend down the tree to find the node pointing at the hypothesis
   uint32_t hashcode = hyp->hashCode() ;
   SearchTrieNode *node = m_trie ;
   for (size_t i = TRIE_DEPTH - 1 ; i > 0 ; i--)
      {
      unsigned index = (hashcode >> (i * BITS_PER_LEVEL)) & TRIE_MASK ;
      path[i] = node ;
      path_index[i] = index ;
      SearchTrieNode *child = node->child(index) ;
      if (!child)
	 {
	 child = new SearchTrieNode ;
	 node->setChild(index,child) ;
	 }
      node = child ;
      }
   // try to find the hypothesis in the list hanging off the leaf node
   unsigned index = hashcode & TRIE_MASK ;
   path[0] = node ;
   path_index[0] = index ;
   HuffmanHypothesis *prev = nullptr ;
   HuffmanHypothesis *cand = node->leaf(index) ;
   for ( ; cand ; cand = cand->next())
      {
      if (hyp->bitCount() == cand->bitCount() &&
	  hyp->sameTrees(cand))
	 {
	 // we've found a match, so unlink it from the list
	 if (prev)
	    prev->setNext(cand->next()) ;
	 else
	    node->setLeaf(index,cand->next()) ;
	 cand->setNext(nullptr) ;
	 delete cand ;
	 m_size-- ;
	 if (!node->leaf(index) && !node->hasDescendants())
	    {
	    // we removed the last hypothesis at this leaf, so check
	    //   whether the node has any descendants.  If no descendants,
	    //   we can delete the node, which may allow us to remove its
	    //   parent, etc.
	    for (size_t i = 1 ; i < TRIE_DEPTH ; i++)
	       {
	       if (!path[i]->hasDescendants())
		  {
		  if (i == TRIE_DEPTH - 1)
		     {
		     delete m_trie ;
		     m_trie = nullptr ;
		     }
		  else
		     {
		     path[i+1]->setChild(path_index[i+1],nullptr) ;
		     delete path[i] ;
		     }
		  }
	       }
	    }
	 return true ;
	 }
      prev = cand ;
      }
   return false ;
}

//----------------------------------------------------------------------

static void traverse(SearchTrieNode* trie, HuffmanHypothesis*& hyps, unsigned depth)
{
   if (depth < TRIE_DEPTH-1)
      {
      for (unsigned i = 0 ; i < (1 << BITS_PER_LEVEL) ; i++)
	 {
	 SearchTrieNode *t = trie->child(i) ;
	 if (t)
	    traverse(t,hyps,depth+1) ;
	 }
      }
   else
      {
      for (unsigned i = 0 ; i < (1 << BITS_PER_LEVEL) ; i++)
	 {
	 HuffmanHypothesis *hyp = trie->leaf(i) ;
	 if (hyp)
	    {
	    hyp->setNext(hyps) ;
	    hyps = hyp ;
	    }
	 }
      }
   delete trie ;
   return ;
}

//----------------------------------------------------------------------

HuffmanHypothesis *SearchTrie::convertToList()
{
   HuffmanHypothesis *hyps = nullptr ;
   if (m_trie)
      traverse(m_trie,hyps,0) ;
   m_size = 0 ;
   m_trie = nullptr ;
   return hyps ;
}

/************************************************************************/
/*	Methods for class HuffmanSearchQueue				*/
/************************************************************************/

HuffmanSearchQueue::HuffmanSearchQueue(size_t qsize, unsigned max_stacks, bool allow_implicit_shift)
{
   m_additions = 0 ;
   m_dups_skipped = 0 ;
   m_numstacks = 0 ;
   m_queuesize = 0 ;
   m_maxqueue = qsize ;
   m_shiftcount = 0 ;
   m_searchmode = SMODE_NOSEARCH ;
   m_implicitshift = allow_implicit_shift ;
   if (!allow_implicit_shift)
      m_directory.reinit() ;
   if (max_stacks == 1)
      {
      m_searchmode = SMODE_DEPTHTHENBREADTH ;
      max_stacks = MAX_EXTENSION + 1 ;
      m_shiftcount = DFS_TO_BFS_THRESHOLD ;
      }
   if (max_stacks >= INT_MAX)
      {
      m_searchmode = SMODE_DEPTHFIRST ;
      m_shiftcount = (size_t)~0 ;
      }
   else if (max_stacks > 0)
      {
      // use a series of stacks for a pure breadth-first search, where
      //   the index of the stack is how many additional bits are covered
      //   relative to the current stack[0] from which we are popping
      m_stacks.allocate(max_stacks+1) ;
      if (m_stacks)
	 {
	 std::fill_n(m_stacks.begin(),max_stacks+1,nullptr) ;
	 m_numstacks = max_stacks ;
	 if (m_searchmode == SMODE_NOSEARCH)
	    m_searchmode = SMODE_BREADTHFIRST ;
	 }
      }
   if (m_numstacks == 0)
      {
      // use a priority queue instead of per-length stacks
      // sort in descending order for best-first/breadth-first search,
      //  in ascending order for pseudo-depthfirst search
      m_queue.reinit(m_maxqueue) ;
      if (m_queue && m_searchmode == SMODE_NOSEARCH)
	 m_searchmode = SMODE_BESTFIRST ;
      }
   return ;
}

//----------------------------------------------------------------------

HuffmanSearchQueue::~HuffmanSearchQueue()
{
   for (size_t i = 0 ; i <= m_numstacks ; i++)
      {
      clearStack(i) ;
      }
   m_queuesize = 0 ;
   m_numstacks = 0 ;
   return ;
}

//----------------------------------------------------------------------

bool HuffmanSearchQueue::more() const
{
   return m_queuesize > 0 ;
}

//----------------------------------------------------------------------

void HuffmanSearchQueue::clearStack(unsigned which)
{
   HuffmanHypothesis *stack = m_stacks[which] ;
   m_stacks[which] = nullptr ;
   while (stack)
      {
      HuffmanHypothesis *next = stack->next() ;
      delete stack  ;
      stack = next ;
      m_queuesize-- ;
      }
   return ;
}

//----------------------------------------------------------------------

void HuffmanSearchQueue::shift(size_t count)
{
   if (count == 0)
      return ;
   size_t to_clear = (count > m_numstacks) ? m_numstacks : count ;
   // clear the stacks which will be shifted out
   for (size_t i = 0 ; i < to_clear ; i++)
      {
      clearStack(i) ;
      }
   // shift any remaining stacks down
   for (size_t i = 0 ; i + count <= m_numstacks ; i++)
      {
      m_stacks[i] = m_stacks[i+count] ;
      }
   // clear the vacated stacks
   for (size_t i = m_numstacks - to_clear ; i <= m_numstacks ; i++)
      {
      m_stacks[i] = nullptr ;
      }
   m_shiftcount += count ;
   return ;
}

//----------------------------------------------------------------------

bool HuffmanSearchQueue::shift()
{
   if (m_queue)
      {
      // nothing to do if using a priority queue, so just indicate whether
      //   there's more in the queue
      return more() ;
      }
   else if (m_numstacks > 0)
      {
      unsigned shiftcount = 1 ;
      // scan for a non-empty stack
      while (shiftcount <= m_numstacks && m_stacks[shiftcount] == nullptr)
	 {
	 shiftcount++ ;
	 }
      if (shiftcount > m_numstacks)
	 {
	 if (more()) cerr << "empty queue??" << endl ;
	 shiftcount = 1 ;
	 }
      shift(shiftcount) ;
      return more() ;
      }
   return false ;
}

//----------------------------------------------------------------------

bool HuffmanSearchQueue::duplicate(const HuffmanHypothesis *hyp) const
{
   return (directory() && directory()->findDuplicate(hyp) != nullptr) ;
}

//----------------------------------------------------------------------

bool HuffmanSearchQueue::trim(size_t size, bool permanent)
{
   bool trimmed = false ;
   while (size < queueSize())
      {
      // reduce contents until the requested size is reached
      HuffmanHypothesis *popped = pop() ;
      if (popped)
	 {
	 trimmed = true ;
	 delete popped ;
	 }
      }
   if (size < maxSize() && permanent)
      {
      m_maxqueue = size ;
      trimmed = true ;
      }
   return trimmed ;
}

//----------------------------------------------------------------------

bool HuffmanSearchQueue::push(HuffmanHypothesis *hyp)
{
   if (!hyp)
      return false ;
   m_additions++ ;
   if (duplicate(hyp))
      {
      m_dups_skipped++ ;
      delete hyp ;
      return false ;
      }
   bool added = false ;
   if (m_queue)
      {
      added = m_queue->push(hyp,-hyp->bitCount()) ;
      }
   else if (m_numstacks > 0)
      {
      unsigned extension = hyp->bitCount() - shiftCount() ;
      if (extension > m_numstacks && implicitShifts())
	 {
	 shift(extension - m_numstacks) ;
	 extension -= m_numstacks ;
	 }
      if (queueSize() >= m_maxqueue)
	 {
	 // pop a non-longest item to make space
	 HuffmanHypothesis *popped = pop() ;
	 for (size_t i = 1 ; !popped && i <= m_numstacks ; i++)
	    {
	    shift(1) ;
	    popped = pop() ;
	    }
	 delete popped ;
	 }
      if (extension <= m_numstacks && queueSize() < m_maxqueue)
	 {
	 hyp->setNext(m_stacks[extension]) ;
	 m_stacks[extension] = hyp ;
	 added = true ;
	 }
      }
   if (added)
      {
      m_queuesize++ ;
      if (directory())
	 directory()->insert(hyp) ;
      }
   else
      {
      m_dups_skipped++ ;
      delete hyp ;
      }
   return added ;
}

//----------------------------------------------------------------------

HuffmanHypothesis *HuffmanSearchQueue::pop()
{
   HuffmanHypothesis *hyp = nullptr ;
   if (m_queue)
      {
      hyp = (HuffmanHypothesis*)m_queue->pop() ;
      if (directory())
	 directory()->remove(hyp) ;
      }
   else if (m_numstacks > 0)
      {
      hyp = m_stacks[0] ;
      if (hyp)
	 {
	 m_stacks[0] = hyp->next() ;
	 if (directory())
	    directory()->remove(hyp) ;
	 }
      }
   if (hyp)
      m_queuesize-- ;
   return hyp ;
}

//----------------------------------------------------------------------

HuffmanHypothesis *HuffmanSearchQueue::popAll()
{
   HuffmanHypothesis *all_hyp = nullptr ;
   if (m_queue)
      {
      while (m_queue->size() > 0)
	 {
	 HuffmanHypothesis *hyp = (HuffmanHypothesis*)m_queue->pop() ;
	 hyp->setNext(all_hyp) ;
	 all_hyp = hyp ;
	 if (directory())
	    directory()->remove(hyp) ;
	 m_queuesize-- ;
	 }
      }
   else if (m_numstacks > 0)
      {
      for (size_t st = 0 ; st <= m_numstacks ; st++)
	 {
	 while (m_stacks[st])
	    {
	    HuffmanHypothesis *hyp = m_stacks[st] ;
	    m_stacks[st] = hyp->next() ;
	    hyp->setNext(all_hyp) ;
	    all_hyp = hyp ;
	    if (directory())
	       directory()->remove(hyp) ;
	    m_queuesize-- ;
	    }
	 }
      }
   return all_hyp ;
}

/************************************************************************/
/*	Methods for class TreeDirectory					*/
/************************************************************************/

HuffmanTreeHypothesis* TreeDirectory::findDuplicate(const HuffmanTreeHypothesis* hyp) const
{
   unsigned bucket = itemIndex(hyp) ;
   for (HuffmanTreeHypothesis *dup = m_entries[bucket] ; dup ; dup = dup->next())
      {
      if (dup->sameTree(hyp))
	 return dup ;
      }
   // if we get here, there is no duplicate in the directory
   return nullptr ;
}

//----------------------------------------------------------------------

bool TreeDirectory::insert(HuffmanTreeHypothesis *hyp)
{
   unsigned bucket = itemIndex(hyp) ;
   HuffmanTreeHypothesis *next = m_entries[bucket] ;
   hyp->setPrev(nullptr) ;
   hyp->setNext(next) ;
   m_entries[bucket] = hyp ;
   if (next)
      next->setPrev(hyp) ;
   return true ;
}

//----------------------------------------------------------------------

bool TreeDirectory::remove(HuffmanTreeHypothesis *hyp)
{
   HuffmanTreeHypothesis *next = hyp->next() ;
   HuffmanTreeHypothesis *prev = hyp->prev() ;
   hyp->setNext(nullptr) ;
   hyp->setPrev(nullptr) ;
   if (next)
      next->setPrev(prev) ;
   if (prev)
      prev->setNext(next) ;
   else
      {
      unsigned bucket = itemIndex(hyp) ;
      if (m_entries[bucket] == hyp)
	 m_entries[bucket] = next ;
      }
   return true ;
}

/************************************************************************/
/*	Methods for class HypothesisDirectory				*/
/************************************************************************/

HuffmanHypothesis* HypothesisDirectory::findDuplicate(const HuffmanHypothesis* hyp) const
{
   unsigned bucket = itemIndex(hyp) ;
   size_t bitcount = hyp->bitCount() ;
   for (HuffmanHypothesis *dup = m_entries[bucket] ; dup ; dup = dup->dirNext())
      {
      if (dup->bitCount() == bitcount && dup->sameTrees(hyp))
	 return dup ;
      }
   // if we get here, there is no duplicate in the directory
   return nullptr ;
}

//----------------------------------------------------------------------

bool HypothesisDirectory::insert(HuffmanHypothesis* hyp)
{
   unsigned bucket = itemIndex(hyp) ;
   hyp->setDirPrev(nullptr) ;
   auto next = m_entries[bucket] ;
   m_entries[bucket] = hyp ;
   hyp->setDirNext(next) ;
   if (next)
      next->setDirPrev(hyp) ;
   return true ;
}

//----------------------------------------------------------------------

bool HypothesisDirectory::remove(HuffmanHypothesis* hyp)
{
   auto next = hyp->dirNext() ;
   auto prev = hyp->dirPrev() ;
   hyp->setDirNext(nullptr) ;
   hyp->setDirPrev(nullptr) ;
   if (next)
      next->setDirPrev(prev) ;
   if (prev)
      prev->setDirNext(next) ;
   else
      {
      unsigned bucket = itemIndex(hyp) ;
      if (m_entries[bucket] == hyp)
	 m_entries[bucket] = next ;
      }
   return true ;
}

/************************************************************************/
/*	Methods for class HuffmanTreeHypothesis				*/
/************************************************************************/

void HuffmanTreeHypothesis::initializeCodeAllocators()
{
   unsigned increment = CODE_HYP_BUCKET_SIZE * sizeof(CodeHypothesis) ;
   for (unsigned i = 1 ; i <= CODE_HYP_BUCKETS ; i++)
      {
      code_allocators[i] = SmallAlloc::create(i*increment) ;
      code_alloc_used[i] = 0 ;
      }
   return ;
}

//----------------------------------------------------------------------

void HuffmanTreeHypothesis::releaseCodeAllocators()
{
   for (unsigned i = 1 ; i <= CODE_HYP_BUCKETS ; i++)
      {
      code_allocators[i]->free() ;
      code_alloc_used[i] = 0 ;
      }
   return ;
}

//----------------------------------------------------------------------

HuffmanTreeHypothesis::HuffmanTreeHypothesis(unsigned max_codes)
{
   m_next = nullptr ;
   m_prev = nullptr ;
   m_parent = nullptr ;
   m_refcount = 1 ;
   m_EOD = (1 << MAX_BITLENGTH) ;
   m_maxcodes = max_codes ;
   m_used = 0 ;
   m_minlength = (maxCodes() == LIT_SYMBOLS) ? MIN_LIT_BITS : MIN_DIST_BITS  ;
   m_maxlength = MAX_BITLENGTH ;
   m_min_extra = (maxCodes() == DIST_SYMBOLS) ? 0 : 1 ;
   initLeftmost() ;
   initRightmost() ;
   memset(m_extra_counts,'\0',sizeof(m_extra_counts)) ;
   allocateCodeBuffer() ;
   computeHashCode() ;
   return ;
}

//----------------------------------------------------------------------

HuffmanTreeHypothesis::HuffmanTreeHypothesis(const HuffmanTreeHypothesis *orig)
{
   assert(orig != nullptr) ;
   m_next = nullptr ;
   m_prev = nullptr ;
   m_parent = orig ;
   m_hashcode = 0 ;
   m_refcount = 1 ;
   m_EOD = orig->m_EOD ;
   m_maxcodes = orig->m_maxcodes ;
   m_used = orig->m_used + 1 ;
   m_minlength = orig->m_minlength ;
   m_maxlength = orig->m_maxlength ;
   memcpy(m_leftmost,orig->m_leftmost,sizeof(m_leftmost)) ;
   memcpy(m_rightmost,orig->m_rightmost,sizeof(m_rightmost)) ;
   memcpy(m_extra_counts,orig->m_extra_counts,sizeof(m_extra_counts)) ;
   allocateCodeBuffer() ;
   if (m_codes)
      {
      std::copy(orig->m_codes,orig->m_codes + orig->m_used, m_codes) ;
      }
   return ;
}

//----------------------------------------------------------------------

HuffmanTreeHypothesis::HuffmanTreeHypothesis(const HuffmanTreeHypothesis *orig, CodeHypothesis *codes,
   					     unsigned num_codes)
{
   assert(orig != nullptr) ;
   m_next = nullptr ;
   m_prev = nullptr ;
   m_parent = orig ;
   m_hashcode = 0 ;
   m_refcount = 1 ;
   m_EOD = orig->m_EOD ;
   m_maxcodes = orig->m_maxcodes ;
   m_codes = codes ;
   m_used = num_codes ;
   m_minlength = orig->m_minlength ;
   m_maxlength = orig->m_maxlength ;
   // re-initialize leftmost, rightmost, and extra_counts from the
   //   given tree
   //FIXME: can be done more efficiently
   initLeftmost() ;
   initRightmost() ;
   memset(m_extra_counts,'\0',sizeof(m_extra_counts)) ;
   for (size_t i = 0 ; i < num_codes ; i++)
      {
      HuffmanCode code = codeValue(i) ;
      unsigned length = codeLength(i) ;
      unsigned extra = extraBits(i) ;
      updateLeftmost(code,length) ;
      updateRightmost(code,length) ;
      incrExtra(extra) ;
      }
   return ;
}

//----------------------------------------------------------------------

HuffmanTreeHypothesis::~HuffmanTreeHypothesis()
{
   releaseCodeBuffer() ;
   return ;
}

//----------------------------------------------------------------------

CodeHypothesis *HuffmanTreeHypothesis::newCodeBuffer(unsigned num_codes)
{
   // figure out which allocator to use
   unsigned bucket = (num_codes + CODE_HYP_BUCKET_SIZE - 1) / CODE_HYP_BUCKET_SIZE ;
   CodeHypothesis *codes ;
   if (code_allocators[bucket])
      {
      codes = (CodeHypothesis*)code_allocators[bucket]->allocate() ;
      code_alloc_used[bucket]++ ;
      }
   else
      {
      // just in case we get a weird size, we'll fall back to regular malloc()
      codes = new CodeHypothesis[num_codes] ;
      }
   return codes ;
}

//----------------------------------------------------------------------

void HuffmanTreeHypothesis::allocateCodeBuffer()
{
   m_codes = newCodeBuffer(symbolCount()) ;
   return ;
}

//----------------------------------------------------------------------

void HuffmanTreeHypothesis::releaseCodeBuffer()
{
   // figure out which allocator to use
   unsigned bucket = (symbolCount() + CODE_HYP_BUCKET_SIZE - 1) / CODE_HYP_BUCKET_SIZE ;
   if (code_allocators[bucket])
      {
      code_allocators[bucket]->release(m_codes) ;
      if (--code_alloc_used[bucket] == 0)
	 {
	 // release the memory for this size of allocation back to the
	 //   global pool so that we can re-use it during the search
	 code_allocators[bucket]->reclaim() ;
	 }
      }
   else
      {
      // just in case we get a weird size, we'll fall back to regular malloc()
      delete[] m_codes ;
      }
   m_codes = nullptr ;
   return ;
}

//----------------------------------------------------------------------

void HuffmanTreeHypothesis::initLeftmost()
{
   for (size_t i = 1 ; i <= MAX_BITLENGTH+1 ; i++)
      {
      m_leftmost[i] = (all_ones[i] & ~1);
      }
   m_leftmost[0] = maximumBitLength() + 1 ;
   return ;
}

//----------------------------------------------------------------------

void HuffmanTreeHypothesis::initRightmost()
{
   std::fill_n(m_rightmost,MAX_BITLENGTH+1,0) ;
   return ;
}

//----------------------------------------------------------------------

void HuffmanTreeHypothesis::computeHashCode()
{
   m_hashcode = m_EOD ;
   for (unsigned i = 0 ; i < symbolCount() ; i++)
      {
      // rotate previous value left eleven bits
      m_hashcode = (m_hashcode << 11) | (m_hashcode >> 21) ;
      // mix in the value of the current Huffman code in the tree
      m_hashcode ^= (m_codes[i].hashValue() * (3 + m_codes[i].extraBits())) ;
      }
   return ;
}

//----------------------------------------------------------------------

TreeDirectory *HuffmanTreeHypothesis::treeDirectory() const
{
   return ((maxCodes() == DIST_SYMBOLS) ? dist_tree_directory : lit_tree_directory) ; 
}

//----------------------------------------------------------------------

unsigned HuffmanTreeHypothesis::maxCodeLength() const
{
   if (symbolCount() > 0)
      return codeLength(symbolCount() - 1) ;
   else
      return 0 ;
}

//----------------------------------------------------------------------

unsigned HuffmanTreeHypothesis::requiredLeaves() const
{
   unsigned maxlen = maxCodeLength() ;
   unsigned prev = m_leftmost[maxlen] ;
   unsigned required = (1 << maxlen) - prev ;
   for (size_t i = maxlen - 1 ; i > 0 ; i--)
      {
      unsigned curr = m_leftmost[i] ;
      required += ((prev >> 1) - curr) ;
      prev = curr ;
      }
   return required ;
}

//----------------------------------------------------------------------

bool HuffmanTreeHypothesis::sameTree(const HuffmanTreeHypothesis *other)
const
{
   // are we the same instance?
   if (this == other)
      return true ;
   // check the hash codes; if they differ, the trees can't be the same
   if (!other || other->hashCode() != hashCode())
      return false ;
   // if the hash codes are the same, we might have a collision, so go
   //   and actually compare the trees
   if (other->symbolCount() != symbolCount())
      return false ;
   unsigned len = symbolCount() * sizeof(m_codes[0]) ;
   return memcmp(m_codes,other->m_codes,len) == 0 ;
}

//----------------------------------------------------------------------

unsigned HuffmanTreeHypothesis::extrabitPredecessors(unsigned extra) const
{
   return (maxCodes() == DIST_SYMBOLS
	   ? extrabit_dist_predecessors[extra]
	   : extrabit_lit_predecessors[extra]) ;
}

//----------------------------------------------------------------------

unsigned HuffmanTreeHypothesis::extrabitSuccessors(unsigned extra) const
{
   return (maxCodes() == DIST_SYMBOLS
	   ? extrabit_dist_successors[extra]
	   : extrabit_lit_successors[extra]) ;
}

//----------------------------------------------------------------------

unsigned HuffmanTreeHypothesis::augmentTree(HuffmanCode code,
					    unsigned length,
					    unsigned extra,
					    CodeHypothesis *&augmented)
const
{
   LocalAlloc<CodeHypothesis> new_tree(maxCodes()+1) ;
   unsigned num_codes = symbolCount() ;
   unsigned prev_extra ;
   unsigned inspoint = findInsertionPoint(code,length,prev_extra) ;
   if (inspoint == HYP_NOT_FOUND)
      {
      if (prev_extra != extra)
	 {
	 num_codes = 0 ;		// conflict! don't expand the search
	 }
      }
   else
      {
      // since we maintained the tree as complete as possible with each
      //   prior insertion, the only point where we may need to augment
      //   the new tree is just before and just after the code being
      //   inserted
      unsigned num_inserted = 0 ;
      // start by building the portion of the tree to the left of the new
      //   code
      if (inspoint == 0)
	 {
	 //  if we're at the left edge and there can't be any shorter
	 //   codes, and there isn't any ambiguity about
	 //   literal/nonliteral codes, we can flesh out all codes
	 //   from 0 up to the new one
	 if (length == minimumBitLength() && extra == EXTRA_ISLITERAL)
	    {
	    for (size_t i = 0 ; i < code ; i++)
	       {
	       new_tree[i].set(i,length,extra) ;
	       num_inserted++ ;
	       }
	    }
	 }
      else
	 {
	 // copy the tree up to the insertion point
	 std::copy_n(m_codes,inspoint,new_tree.begin()) ;
	 // add in any codes that we now know for certain given the last
	 //   code copied and the new one being inserted
	 if (new_tree[inspoint-1].length() < length)
	    {
	    // when the previous known code is shorter, the only thing
	    //   we can infer is that the xxx0 sibling of an xxx1 code
	    //   is of the same length; if xxx1 is a literal code, we
	    //   thus know that xxx0 must also be a literal
	    if ((code & 1) != 0 && extra == EXTRA_ISLITERAL)
	       {
	       new_tree[inspoint+num_inserted].set(code & ~1,length,extra) ;
	       num_inserted++ ;
	       }
	    }
	 else
	    {
	    // if the previous code of the same length has the same
	    //   number of extra bits (or both are literals), we can
	    //   fill in any missing codes between the prior code and
	    //   the new code
	    if ((new_tree[inspoint-1].extraBits() == extra) ||
		(new_tree[inspoint-1].isLiteral()
		 && extra == EXTRA_ISLITERAL))
	       {
	       HuffmanCode pred = new_tree[inspoint-1].codeValue() + 1 ;
	       unsigned additional = code - pred ;
	       if ((extra != EXTRA_ISLITERAL &&
		   additional >= m_extra_counts[extra]) ||
		   num_codes + num_inserted + additional > maxCodes())
		  {
		  // the augmented tree will have either too many leaves
		  //   total, or too many with the given # of extra bits
		  num_codes = num_inserted = 0 ;
		  }
	       else
		  {
		  for (HuffmanCode c = pred ; c < code ; c++)
		     {
		     new_tree[inspoint+num_inserted].set(c,length,extra) ;
		     num_inserted++ ;
		     }
		  }
	       }
	    }
	 }
      // insert the new code
      new_tree[inspoint+num_inserted].set(code,length,extra) ;
      num_inserted++ ;
      // build the portion of the tree to the right of the new code
      if (inspoint < symbolCount())
	 {
	 // add in any codes that we now know for certain given the
	 //   new code and the next code to be copied
	 if (m_codes[inspoint].length() == length)
	    {
	    // if the successor code has the same number of extra bits
	    //   (or both are literals), we can fill in the missing
	    //   codes between the two
	    if ((m_codes[inspoint].extraBits() == extra) ||
		(m_codes[inspoint].isLiteral()
		 && extra == EXTRA_ISLITERAL))
	       {
	       HuffmanCode succ = m_codes[inspoint].codeValue() ;
	       unsigned additional = succ - code - 1 ;
	       if ((extra != EXTRA_ISLITERAL &&
		   additional >= m_extra_counts[extra]) ||
		   num_codes + num_inserted + additional > maxCodes())
		  {
		  // the augmented tree will have either too many leaves
		  //   total, or too many with the given # of extra bits
		  num_codes = num_inserted = 0 ;
		  }
	       else
		  {
		  for (HuffmanCode c = code + 1 ; c < succ ; c++)
		     {
		     new_tree[inspoint+num_inserted].set(c,length,extra) ;
		     num_inserted++ ;
		     }
		  }
	       }
	    }
	 else
	    {
	    //TODO: if the length of the successor is one greater, we
	    //  can try to scan for the point between new and
	    //  successor code at which switching to the greater
	    //  length would force the tree to be too large, but that
	    //  will be helpful in a sufficiently small percentage of
	    //  the time to not make it worth the effort right now
	    }
	 // and finally copy the remainder of the original tree
	 for (size_t i = inspoint ; i < symbolCount() ; i++)
	    {
	    new_tree[i+num_inserted] = m_codes[i] ;
	    }
	 }
      else
	 {
	 // TODO: the only thing we could infer at the right edge is
	 //   if the code is of the maximum length, is a non-literal,
	 //   and the number of possible successors exactly equals the
	 //   number of remaining non-literal codes with the same or
	 //   more extra bits; a rare occurrence not worth checking for
	 }
      num_codes += num_inserted ;
      }
   if (num_codes > symbolCount())
      {
      augmented = newCodeBuffer(num_codes) ;
      if (augmented)
	 {
	 std::copy(new_tree.begin(),new_tree.begin()+num_codes,augmented) ;
	 }
      else
	 num_codes = 0 ;
      }
   else if (num_codes == symbolCount())
      {
      augmented = m_codes ;
      }
   else
      {
      augmented = nullptr ;
      num_codes = 0 ;
      }
   return num_codes ;
}

//----------------------------------------------------------------------

unsigned HuffmanTreeHypothesis::findCode(HuffmanCode code,
					 unsigned length) const
{
   unsigned hi = symbolCount() ;
   unsigned lo = 0 ;
   HuffmanCode canon = canonicalized(code,length) ;
   while (hi > lo)
      {
      unsigned mid = (hi + lo) / 2 ;
      HuffmanCode midvalue = canonicalCodeValue(mid) ;
      if (canon < midvalue)
	 hi = mid ;
      else if (canon > midvalue)
	 lo = mid + 1 ;
      else
	 return mid ;			// we found the value
      }
   // if we get to this point, the item is not yet in the table, and
   //   'lo' points at its successor
   return lo ;
}

//----------------------------------------------------------------------

unsigned HuffmanTreeHypothesis::findInsertionPoint(HuffmanCode code,
						   unsigned length,
						   unsigned &extra) const
{
   extra = 0 ;
   unsigned hi = symbolCount() ;
   unsigned lo = 0 ;
   HuffmanCode canon = canonicalized(code,length) ;
   while (hi > lo)
      {
      unsigned mid = (hi + lo) / 2 ;
      HuffmanCode midvalue = canonicalCodeValue(mid) ;
      if (canon < midvalue)
	 hi = mid ;
      else if (canon > midvalue)
	 lo = mid + 1 ;
      else
	 {
	 extra = isLiteral(mid) ? EXTRA_ISLITERAL : extraBits(mid) ;
	 return HYP_NOT_FOUND ;  // already present, don't insert
	 }
      }
   // if we get to this point, the item is not yet in the table, and
   //  'lo' is the position at which to insert
   return lo ;
}

//----------------------------------------------------------------------

bool HuffmanTreeHypothesis::extraBitsAtLimit(unsigned extra) const
{
   if (extra == EXTRA_ISLITERAL)
      return false ;
   if (maxCodes() == DIST_SYMBOLS)
      {
      return m_extra_counts[extra] >= extrabit_dist_limits[extra] ;
      }
   else
      {
      return m_extra_counts[extra] >= extrabit_lit_limits[extra] ;
      }
}

//----------------------------------------------------------------------

bool HuffmanTreeHypothesis::tooManyLeaves(HuffmanCode code, unsigned length)
const
{
   if (code >= m_leftmost[length])
      return m_leftmost[0] > maxCodes() ;
   if ((1 << length) - code + length > maxCodes())
      return true ;
   unsigned maxlen = length ;
   if (maxCodeLength() >= length)
      maxlen = maxCodeLength() ;
   unsigned prev = m_leftmost[maxlen] ;
   if (length >= maxlen)
      {
      if (code < prev)
	 prev = code ;
//      code >>= 1 ;
      }
   unsigned required = (1 << maxlen) - prev ;
   for (size_t i = maxlen - 1 ; i > 0 ; i--)
      {
      unsigned curr = m_leftmost[i] ;
      if (i < length)
	 {
	 code >>= 1 ;
	 if (code < curr)
	    curr = (code & ~1) ;
	 } 
      required += ((prev >> 1) - curr) ;
      prev = curr ;
      }
   return required > maxCodes() ;
}

//----------------------------------------------------------------------

bool HuffmanTreeHypothesis::consistentWithTree(HuffmanCode code,
					       unsigned length,
					       unsigned extra,
					       bool &present) const
{
   present = false ;
   if (length < minimumBitLength() || length > maximumBitLength())
      return false ;
   HuffmanCode canon = canonicalized(code,length) ;
   if (canon == m_EOD)
      return false ;		// EOD may only occur at end of stream
   // Ensure that we don't violate the proper shape of the tree.  The
   //   given code must follow the right-most known leaf of the
   //   next-shorter length and precede the left-most required node of
   //   the next-greater length.
   if ((code << 1) >= m_leftmost[length+1])
      return false ;
   if (length > minimumBitLength() && (code >> 1) <= m_rightmost[length-1])
      return false ;
   // a quick check of ordering: the minimum number of lower symbols of
   //   the same length is the difference between the current code and the
   //   left-most known code of the given length; if that would require
   //   too many predecessor symbols according to the number of extra
   //   bits, the code is not consistent with the tree
   if (code > m_leftmost[length] &&
       (unsigned)(code - m_leftmost[length]) > extrabitPredecessors(extra))
      return false ;
   // the required number of right siblings with the same
   //   length can't be greater than the number of
   //   possible length symbols greater than the current
   //   one
   if (length == maximumBitLength())
      {
      if (((unsigned)((1 << length) - code)) > extrabitSuccessors(extra))
	 return false ;
      }
   else if (code < m_rightmost[length] &&
	    (unsigned)(m_rightmost[length] - code)
	    > extrabitSuccessors(extra))
      return false ;
   // ensure that adding the code won't require the tree
   //   to grow too large
   if (tooManyLeaves(code,length))
      return false ;
   unsigned index = findCode(code,length) ;
   if (index < symbolCount())
      {
      // check whether the code is already present
      if (canonicalCodeValue(index) == (canon & code_mask[length]))
	 {
	 unsigned prev_length = codeLength(index) ;
	 if (prev_length > length)
	    return false ;		// we're a prefix
	 if (prev_length == length)
	    {
//	    if (canon > m_EOD && maximumBitLength() > 9 && maximumBitLength() < MAX_BITLENGTH)
//	       return false ;		// we're an already-seen singleton
	    if (isLiteral(index))
	       {
	       if (extra != EXTRA_ISLITERAL)
		  return false ;
	       }
	    else // if (!isLiteral(index))
	       {
	       if (extraBits(index) != extra)
		  return false ;
	       }
	    // the code is present and matches on extra bits, so it is
	    //   consistent with the tree
	    present = true ;
	    return true ;
	    }
	 }
      // the code is not yet present, so check for consistency; the index
      //   returned by findCode() is what will become its right sibling
      // verify ordering with respect to right sibling
      // we must be no longer than the sibling
      unsigned siblen = codeLength(index) ;
      if (length > siblen)
	 return false ;
      // if we're the same length, check that literals precede length/dist
      //   codes; if both are length/dist codes, ensure proper ordering
      //   of extra bits
      if (siblen == length)
	 {
	 bool siblit = isLiteral(index) ;
	 if (siblit && extra != EXTRA_ISLITERAL)
	    return false ;
	 if (!siblit && extra != EXTRA_ISLITERAL && extra >= m_min_extra)
	    {
	    unsigned sibextra = extraBits(index) ;
	    if (sibextra >= m_min_extra && sibextra < extra)
	       return false ;
	    }
	 }
      }
   if (index > 0)
      {
      // verify ordering with respect to left sibling
      // we must be at least as long as the sibling
      unsigned siblen = codeLength(index-1) ;
      if (siblen > length)
	 return false ;
      // if we're the same length, check that literals precede length/dist
      //   codes; if both are length/dist codes, ensure proper ordering
      //   of extra bits
      if (siblen == length)
	 {
	 bool siblit = isLiteral(index-1) ;
	 if (!siblit && extra == EXTRA_ISLITERAL)
	    return false ;
	 if (!siblit && extra != EXTRA_ISLITERAL && extra >= m_min_extra)
	    {
	    unsigned sibextra = extraBits(index-1) ;
	    if (sibextra >= m_min_extra && sibextra > extra)
	       return false ;
	    }
	 }
      }
   return true ;
}

//----------------------------------------------------------------------

void HuffmanTreeHypothesis::removeReference()
{
   if (m_refcount > 0)
      {
      m_refcount-- ;
      if (m_refcount == 0)
	 {
	 TreeDirectory *dir = treeDirectory() ;
	 if (dir)
	    dir->remove(this) ;
	 delete this ;
	 }
      }
   return ;
}

//----------------------------------------------------------------------

void HuffmanTreeHypothesis::setMaxBitLength(unsigned length)
{
   m_maxlength = (length <= MAX_BITLENGTH) ? length : MAX_BITLENGTH ;
   m_leftmost[maximumBitLength()+1] = all_ones[MAX_BITLENGTH+1] ;
   m_rightmost[maximumBitLength()] = all_ones[maximumBitLength()] ;
   m_leftmost[0] = requiredLeaves() ;
   return ;
}

//----------------------------------------------------------------------

void HuffmanTreeHypothesis::updateLeftmost(HuffmanCode code,
					   unsigned length)
{
   if (code < m_leftmost[length])
      {
      m_leftmost[length] = code & ~1 ;
      for (size_t i = length - 1 ; i > 0 ; i--)
	 {
	 code = (code >> 1) & ~1 ;
	 if (code < m_leftmost[i])
	    m_leftmost[i] = code ;
	 }
      m_leftmost[0] = requiredLeaves() ;
      }
   return ;
}

//----------------------------------------------------------------------

void HuffmanTreeHypothesis::updateRightmost(HuffmanCode code,
					    unsigned length)
{
   if (code > m_rightmost[length])
      {
      m_rightmost[length] = code ;
      }
   return ;
}

//----------------------------------------------------------------------

HuffmanTreeHypothesis *HuffmanTreeHypothesis::insert(HuffmanCode code, unsigned length,
						     unsigned extra, bool is_EOD) const
{
   INCR_STAT(tree_insertions) ;
   CodeHypothesis* new_codetree ;
   unsigned new_size = augmentTree(code,length,extra,new_codetree) ;
   if (new_size == symbolCount())
      {
      INCR_STAT(tree_present) ;
      return (HuffmanTreeHypothesis*)this ;
      }
   else if (new_size == 0)
      {
      INCR_STAT(tree_conflict) ;
      return nullptr ;
      }
   Owned<HuffmanTreeHypothesis> new_hyp(this,new_codetree,new_size) ;
   if (new_hyp)
      {
      // check whether we've created a duplicate tree
      new_hyp->computeHashCode() ;
      TreeDirectory *dir = treeDirectory() ;
      auto dup = dir->findDuplicate(new_hyp) ;
      if (dup)
	 {
	 new_hyp->removeReference() ;
	 dup->addReference() ;
	 INCR_STAT(tree_duplicates) ;
	 return dup ;
	 }
      dir->insert(new_hyp) ;
      // update information about the tree
      new_hyp->updateLeftmost(code,length) ;
      new_hyp->updateRightmost(code,length) ;
      new_hyp->incrExtra(extra) ;
      if (code == 0)
	 new_hyp->setMinBitLength(length) ;
      if (code == all_ones[length])
	 new_hyp->setMaxBitLength(length) ;
      if (is_EOD)
	 new_hyp->m_EOD = canonicalized(code,length) ;
      }
   return new_hyp.move() ;
}

//----------------------------------------------------------------------

void HuffmanTreeHypothesis::dump() const
{
   for (unsigned i = 0 ; i < symbolCount() ; i++)
      {
      cerr << binary(codeValue(i),codeLength(i)) ;
      if (!isLiteral(i))
	 cerr << '+' << extraBits(i) ;
      if (isEOD(codeValue(i),codeLength(i)))
	 cerr << " = " << END_OF_DATA ;
      cerr << endl ;
      }
   return ;
}

/************************************************************************/
/*	Methods for class HuffmanHypothesis				*/
/************************************************************************/

HuffmanHypothesis::HuffmanHypothesis(const BitPointer& pos)
   : m_startpos(pos)
{
   m_bitcount = 0 ;
   m_in_backref = false ;
   m_litcodes = new HuffmanTreeHypothesis(LIT_SYMBOLS) ;
   m_distcodes = new HuffmanTreeHypothesis(DIST_SYMBOLS) ;
   clearLastLiteral() ;
   setNext(nullptr) ;
   setDirNext(nullptr) ;
   setDirPrev(nullptr) ;
#ifdef TRACE_GENERATIONS
   m_generation = 0 ;
#endif /* TRACE_GENERATIONS */
   return ;
}

//----------------------------------------------------------------------

HuffmanHypothesis::HuffmanHypothesis(const HuffmanHypothesis* orig, const BitPointer& pos,
				     size_t extension_len)
   : m_startpos(pos)
{
   m_in_backref = false ;
   m_litcodes = orig->m_litcodes ;
   m_distcodes = orig->m_distcodes ;
   m_litcodes->addReference() ;
   m_distcodes->addReference() ;
   m_lastliteral = orig->lastLiteral() ;
   m_lastlitlength = orig->lastLiteralLength() ;
   m_lastlitcount = orig->lastLiteralRepeat() ;
   m_bitcount = orig->bitCount() + extension_len ;
#ifdef TRACE_GENERATIONS
   m_generation = orig->generation() + 1 ;
#endif /* TRACE_GENERATIONS */
   return ;
}

//----------------------------------------------------------------------

HuffmanHypothesis::~HuffmanHypothesis()
{
   if (m_litcodes)
      m_litcodes->removeReference() ;
   if (m_distcodes)
      m_distcodes->removeReference() ;
   m_bitcount = 0 ;
   return ;
}

//----------------------------------------------------------------------

bool HuffmanHypothesis::consistentLiteral(HuffmanCode code, unsigned length) const
{
   bool present ;
   return m_litcodes->consistentWithTree(code,length,EXTRA_ISLITERAL, present) ;
}

//----------------------------------------------------------------------

bool HuffmanHypothesis::consistentMatchLength(HuffmanCode code, unsigned len_bits, unsigned extra_bits ) const
{
   bool present ;
   bool consistent = m_litcodes->consistentWithTree(code,len_bits,extra_bits,present) ;
   if (consistent && !present && extraLiteralBitsAtLimit(extra_bits))
      return false ;
   return consistent ;
}

//----------------------------------------------------------------------

bool HuffmanHypothesis::consistentDistance(HuffmanCode code, unsigned dist_bits, unsigned extra_bits) const
{
   bool present ;
   bool consistent = m_distcodes->consistentWithTree(code,dist_bits,extra_bits,present) ;
   if (consistent && !present && extraDistanceBitsAtLimit(extra_bits))
      return false ;
   return consistent ;
}

//----------------------------------------------------------------------

void HuffmanHypothesis::updateLastLiteral(HuffmanCode code, unsigned length)
{
   if (length == lastLiteralLength() && code == lastLiteral())
      {
      m_lastlitcount++ ;
      }
   else
      {
      m_lastliteral = code ;
      m_lastlitlength = length ;
      m_lastlitcount = 1 ;
      }
   return ;
}

//----------------------------------------------------------------------

bool HuffmanHypothesis::excessiveRepeats(HuffmanCode code, unsigned length) const
{
   if (length == lastLiteralLength() && code == lastLiteral())
      return lastLiteralRepeat() >= MAX_LITERAL_REPEATS ;
   return false ;
}

//----------------------------------------------------------------------

bool HuffmanHypothesis::sameTrees(const HuffmanHypothesis* other_hyp) const
{
   if (inBackReference() == other_hyp->inBackReference() &&
       m_litcodes && m_litcodes->sameTree(other_hyp->m_litcodes) &&
       m_distcodes && m_distcodes->sameTree(other_hyp->m_distcodes))
      return true ;
   return false ;
}

//----------------------------------------------------------------------

HuffmanHypothesis *HuffmanHypothesis::extend(const BitPointer& position, HuffmanCode code,
				   	     unsigned len, unsigned symbol) const
{
   if (code == all_ones[len] &&
       (len < m_litcodes->maxCodeLength() || len < NEEDED_LIT_BITS))
      return nullptr ;
   if (verbosity >= VERBOSITY_TREE)
      {
      cerr << "extend " << m_bitcount << ": code=" << binary(code,len) ;
      if (symbol == END_OF_DATA)
	 cerr << " EOD" ;
      cerr << endl ;
      }
   Owned<HuffmanHypothesis> new_hyp(this,position,len) ;
   if (new_hyp)
      {
      new_hyp->updateLastLiteral(code,len) ;
      new_hyp->m_litcodes = m_litcodes->insert(code,len,EXTRA_ISLITERAL, symbol == END_OF_DATA) ;
      if (new_hyp->m_litcodes != m_litcodes)
	 m_litcodes->removeReference() ;
      if (!new_hyp->m_litcodes)
	 {
	 // should never happen
	 return nullptr ;
	 }
      }
   return new_hyp.move() ; //FIXME
}

//----------------------------------------------------------------------

HuffmanHypothesis *
HuffmanHypothesis::extend(const BitPointer &position,
			  HuffmanCode code, unsigned length,
			  unsigned extra, bool is_distance) const
{
   if (code == all_ones[length])
      {
      if (is_distance)
	 {
	 if (length < m_distcodes->maxCodeLength() || length < NEEDED_DIST_BITS)
	    return nullptr ;
	 }
      else
	 {
	 if (length < m_litcodes->maxCodeLength() || length < NEEDED_LIT_BITS)
	    return nullptr ;
	 }
      }
   if (verbosity >= VERBOSITY_TREE)
      {
      cerr << "extend " << m_bitcount << ": "
	   << (is_distance?"dist":"match") << "code="
	   << binary(code,length) << "+" << extra << endl ;
      }
   size_t extension = length + extra ;
   Owned<HuffmanHypothesis> new_hyp(this,position,extension) ;
   if (new_hyp)
      {
      new_hyp->clearLastLiteral() ;
      if (is_distance)
	 {
	 new_hyp->m_distcodes
	    = m_distcodes->insert(code,length,extra) ;
	 if (new_hyp->m_distcodes != m_distcodes)
	    m_distcodes->removeReference() ;
	 new_hyp->inBackReference(true) ;
	 }
      else
	 {
	 new_hyp->m_litcodes
	    = m_litcodes->insert(code,length,extra) ;
	 if (new_hyp->m_litcodes != m_litcodes)
	    m_litcodes->removeReference() ;
	 }
      if (!new_hyp->m_litcodes || !new_hyp->m_distcodes)
	 {
	 // should never happen
	 return nullptr ;
	 }
      }
   return new_hyp.move() ; //FIXME
}

//----------------------------------------------------------------------

void HuffmanHypothesis::addLitCode(HuffmanCode code, unsigned length, unsigned extra, unsigned symbol)
{
   if (!m_litcodes)
      return ;
   bool is_EOD = (symbol == END_OF_DATA) ;
   HuffmanTreeHypothesis *new_lit
      = m_litcodes->insert(code,length,extra,is_EOD) ;
   if (new_lit != m_litcodes)
      {
      m_litcodes->removeReference() ;
      }
   m_litcodes = new_lit ;
   return ;
}

//----------------------------------------------------------------------

void HuffmanHypothesis::addDistCode(HuffmanCode code, unsigned length, unsigned extra, unsigned symbol)
{
   if (!m_distcodes)
      return ;
   bool is_EOD = (symbol == END_OF_DATA) ;
   auto new_dist = m_distcodes->insert(code,length,extra,is_EOD) ;
   if (new_dist != m_distcodes)
      {
      m_distcodes->removeReference() ;
      }
   m_distcodes = new_dist ;
   return ;
}

/************************************************************************/
/*	Methods for class PartialHuffmanTreeBase			*/
/************************************************************************/

PartialHuffmanTreeBase::PartialHuffmanTreeBase(unsigned size)
{
   m_mindepth = (maxNodes() == LIT_SYMBOLS) ? MIN_LIT_BITS : MIN_DIST_BITS ;
   m_maxdepth = MAX_BITLENGTH ;
   m_max_length_used = 0 ;
   m_nodes_used = 1 ;  // root always gets pre-allocated
   m_total_nodes = size ;
   m_min_extra = (maxNodes() == DIST_SYMBOLS) ? 0 : 1 ;
   m_extrabit_successors = ((maxNodes() == DIST_SYMBOLS)
			    ? extrabit_dist_successors
			    : extrabit_lit_successors) ;
   m_extrabit_predecessors = ((maxNodes() == DIST_SYMBOLS)
			      ? extrabit_dist_predecessors
			      : extrabit_lit_predecessors) ;
   initLeftmost() ;
   initRightmost() ;
   memset(m_extra_counts,'\0',sizeof(m_extra_counts)) ;
   return ;
}

//----------------------------------------------------------------------

void PartialHuffmanTreeBase::initLeftmost()
{
   for (size_t i = 1 ; i <= MAX_BITLENGTH+1 ; i++)
      {
      m_leftmost[i] = (all_ones[i] & ~1);
      }
   m_leftmost[0] = maximumBitLength() + 1 ;
   return ;
}

//----------------------------------------------------------------------

void PartialHuffmanTreeBase::initRightmost()
{
   for (size_t i = 0 ; i <= MAX_BITLENGTH ; i++)
      {
      m_rightmost[i] = 0 ;
      }
   return ;
}

//----------------------------------------------------------------------

void PartialHuffmanTreeBase::setMaxBitLength(unsigned len)
{ 
   m_maxdepth = (len <= MAX_BITLENGTH) ? len : MAX_BITLENGTH ;
   m_leftmost[maximumBitLength()+1] = all_ones[MAX_BITLENGTH+1] ;
   m_rightmost[maximumBitLength()] = all_ones[maximumBitLength()] ;
   return ;
}

//----------------------------------------------------------------------

void PartialHuffmanTreeBase::updateLeftmost(HuffmanCode code,
					    unsigned length)
{
   if (code < m_leftmost[length])
      {
      m_leftmost[length] = code & ~1 ;
      for (size_t i = length - 1 ; i > 0 ; i--)
	 {
	 code = (code >> 1) & ~1 ;
	 if (code < m_leftmost[i])
	    m_leftmost[i] = code ;
	 }
      m_leftmost[0] = requiredLeaves() ;
      }
   return ;
}

//----------------------------------------------------------------------

void PartialHuffmanTreeBase::updateRightmost(HuffmanCode code,
					     unsigned length)
{
   if (code > m_rightmost[length])
      {
      m_rightmost[length] = code ;
      }
   return ;
}

//----------------------------------------------------------------------

unsigned PartialHuffmanTreeBase::requiredLeaves() const
{
   unsigned prev = m_leftmost[maximumBitLength()] ;
   unsigned required = (1 << maximumBitLength()) - prev ;
   for (size_t i = maximumBitLength() - 1 ; i > 0 ; i--)
      {
      unsigned curr = m_leftmost[i] ;
      required += ((prev >> 1) - curr) ;
      prev = curr ;
      }
   return required ;
}

//----------------------------------------------------------------------

bool PartialHuffmanTreeBase::tooManyLeaves(HuffmanCode code,
					   unsigned length) const
{
//HuffmanCode orig_code = code ;
   if (code >= m_leftmost[length])
      return m_leftmost[0] > maxNodes() ;
   if ((1 << length) - code + length > maxNodes())
      return true ;
//return false;
   unsigned maxlen = length ;
   if (maxCodeLength() >= length)
      maxlen = maxCodeLength() ;
   unsigned prev = m_leftmost[maxlen] ;
   if (length >= maxlen)
      {
      if (code < prev)
	 prev = code ;
      code >>= 1 ;
      }
   unsigned required = (1 << maxlen) - prev ;
   for (size_t i = maxlen - 1 ; i > 0 ; i--)
      {
      unsigned curr = m_leftmost[i] ;
      if (i < length)
	 {
	 if (code < curr)
	    curr = (code & ~1) ;
	 code >>= 1 ;
	 } 
      required += ((prev >> 1) - curr) ;
      prev = curr ;
      }
//cerr<<"required("<<binary(orig_code,length)<<","<<maxlen<<") -> "<<required<<endl;
   return required > maxNodes() ;
}

//----------------------------------------------------------------------

static unsigned predecessor_length(const HuffmanTreeNode *nodes,
				   unsigned index, unsigned length,
				   HuffmanChildInfo &pred)
{
   // the predecessor of the current HuffmanCode is the right-most leaf child of the
   //   given node's left child
   HuffmanChildInfo childinfo = nodes[index].leftChild() ;
   if (childinfo.isValid())
      {
      length++ ;
      if (!childinfo.isLeaf())
	 {
	 index = childinfo.childIndex() ;
	 while (index != 0)
	    {
	    length++ ;
	    // check the right child
	    childinfo = nodes[index].rightChild() ;
	    if (childinfo.isValid())
	       {
	       // if it's a leaf, we're done
	       if (childinfo.isLeaf())
		  break ;
	       // otherwise, descend into the child
	       index = childinfo.childIndex() ;
	       }
	    else
	       {
	       // check the left child
	       childinfo = nodes[index].leftChild() ;
	       if (childinfo.isValid())
		  {
		  // if it's a leaf, we're done
		  if (childinfo.isLeaf())
		     break ;
		  // otherwise, descend into the child
		  index = childinfo.childIndex() ;
		  }
	       else
		  {
		  // should never happen: only the root node of an empty
		  //   partial tree will ever have both children invalid
		  length = 1 ;
		  break ;
		  }
	       }
	    }
	 }
      }
   pred = childinfo ;
   return length ;
}

//----------------------------------------------------------------------

static unsigned successor_length(const HuffmanTreeNode *nodes,
				 unsigned index, unsigned length,
				 unsigned max_length,
				 HuffmanChildInfo &succ)
{
   // the successor of the current HuffmanCode is the left-most leaf child of the
   //   given node's right child
   HuffmanChildInfo childinfo = nodes[index].rightChild() ;
   if (!childinfo.isValid())
      length = max_length ;
   else
      {
      length++ ;
      if (!childinfo.isLeaf())
	 {
	 index = childinfo.childIndex() ;
	 while (index != 0)
	    {
	    length++ ;
	    // check the left child
	    childinfo = nodes[index].leftChild() ;
	    if (childinfo.isValid())
	       {
	       // if it's a leaf, we're done
	       if (childinfo.isLeaf())
		  break ;
	       // otherwise, descend into the child
	       index = childinfo.childIndex() ;
	       }
	    else
	       {
	       // check the right child
	       childinfo = nodes[index].rightChild() ;
	       if (childinfo.isValid())
		  {
		  // if it's a leaf, we're done
		  if (childinfo.isLeaf())
		     break ;
		  // otherwise, descend into the child
		  index = childinfo.childIndex() ;
		  }
	       else
		  {
		  // should never happen: only the root node of an empty
		  //   partial tree will ever have both children invalid
		  length = max_length ;
		  break ;
		  }
	       }
	    }
	 }
      }
   succ = childinfo ;
   return length ;
}

//----------------------------------------------------------------------

bool PartialHuffmanTreeBase::consistentWithTree(const HuffmanTreeNode *nodes,
						HuffmanCode code,
						size_t length,
						size_t extra,
						bool &present) const
{
   present = false ;
   if (length < minimumBitLength() || length > maximumBitLength())
      return false ;
   // Ensure that we don't violate the proper shape of the tree.  The
   //   given code must follow the right-most known leaf of the
   //   next-shorter length and precede the left-most required node of
   //   the next-greater length.
   if ((code << 1) >= m_leftmost[length+1])
      return false ;
   if (length > minimumBitLength() && (code >> 1) < m_rightmost[length-1])
      return false ;
   // a quick check of ordering: the minimum number of lower symbols of
   //   the same length is the difference between the current code and the
   //   left-most known code of the given length; if that would require
   //   too many predecessor symbols according to the number of extra
   //   bits, the code is not consistent with the tree
   if (code > m_leftmost[length] &&
       (unsigned)(code - m_leftmost[length]) > m_extrabit_predecessors[extra])
      return false ;
   // the required number of right siblings with the same
   //   length can't be greater than the number of
   //   possible length symbols greater than the current
   //   one
   if (length == maximumBitLength())
      {
      if (((unsigned)((1 << length) - code)) > m_extrabit_successors[extra])
	 return false ;
      }
   else if (code < m_rightmost[length] &&
	    (unsigned)(m_rightmost[length] - code)
	    > m_extrabit_successors[extra])
      return false ;
   // ensure that adding the code won't require the tree
   //   to grow too large
   if (tooManyLeaves(code,length))
      return false ;
   unsigned index = 0 ;
   unsigned depth = 0 ;
   unsigned pred_index = ~0 ;
   unsigned pred_depth = 0 ;
   unsigned succ_index = ~0 ;
   unsigned succ_depth = 0 ;
   for (HuffmanCode mask = (1 << (length-1)) ; mask ; mask >>= 1, depth++)
      {
      bool right = ((code & mask) != 0) ;
      HuffmanChildInfo childinfo = nodes[index].getChild(right) ;
      if (right)
	 {
	 if (nodes[index].leftChildValid())
	    {
	    pred_index = index ;
	    pred_depth = depth ;
	    }
	 }
      else
	 {
	 if (nodes[index].rightChildValid())
	    {
	    succ_index = index ;
	    succ_depth = depth ;
	    }
	 }
      if (childinfo.isValid())
	 {
	 if (!childinfo.isLeaf())
	    {
	    index = childinfo.childIndex() ;
	    }
	 else
	    {
	    if (mask != 1)
	       return false ;	     // a prefix has already been used
	    // we've already assigned this exact code, so check whether it
	    //   has the correct number of extra bits and is *not* the EOD
	    //   symbol
	    present = true ;
	    if (childinfo.extraBits() != extra 
		|| childinfo.symbol() == END_OF_DATA)
	       return false ;
	    return true ;
	    }
	 }
      else // if (!childinfo.isValid())
	 {
	 // we haven't yet assigned any nodes matching any further down the
	 //   code, so check whether the code is consistent with the existing
	 //   assignments
	 if (pred_index != (unsigned)~0)
	    {
	    HuffmanChildInfo pred ;
	    unsigned pred_len = predecessor_length(nodes,pred_index,
						   pred_depth,pred) ;
	    if (pred_len > length)
	       return false ;
	    if (pred_len == length && pred.isValid())
	       {
	       // ensure appropriate ordering of literal and length symbols:
	       unsigned pred_extra = pred.extraBits() ;
	       // if we are a literal symbol, our predecessor must be as well
	       if (extra == EXTRA_ISLITERAL && pred_extra != EXTRA_ISLITERAL)
		  return false ;
	       // a higher number of extra bits implies a higher symbol,
	       //   which must lexically follow the lower symbol
	       if (extra >= m_min_extra && extra != EXTRA_ISLITERAL &&
		   pred_extra >= m_min_extra &&
		   pred_extra != EXTRA_ISLITERAL &&
		   extra < pred_extra)
		  return false ;
	       }
	    }
	 if (succ_index != (unsigned)~0)
	    {
	    HuffmanChildInfo succ ;
	    unsigned succ_len = successor_length(nodes,succ_index,succ_depth,
						 maximumBitLength(),succ) ;
	    if (succ_len < length)
	       return false ;
	    if (succ_len == length && succ.isValid())
	       {
	       // ensure appropriate ordering of literal and length symbols:
	       unsigned succ_extra = succ.extraBits() ;
	       // if our successor is a literal symbol, we must be as well
	       if (succ_extra == EXTRA_ISLITERAL && extra != EXTRA_ISLITERAL)
		  return false ;
	       // a higher number of extra bits implies a higher symbol,
	       //   which must lexically follow the lower symbol
	       if (extra >= m_min_extra && extra != EXTRA_ISLITERAL &&
		   succ_extra >= m_min_extra &&
		   succ_extra != EXTRA_ISLITERAL &&
		   extra > succ_extra)
		  return false ;
	       }
	    }
	 return true ;
	 }
      }
   // if we get here, that means the desired code is a prefix of an already
   //   assigned code, so we're not consistent with the tree
   return false ;
}

//----------------------------------------------------------------------

bool PartialHuffmanTreeBase::add(HuffmanTreeNode *nodes, unsigned index,
				 HuffmanCode code, unsigned length,
				 unsigned extra_bits, unsigned symbol)
{
   if (length == 0)
      return false ;
   // the all-zeros code is the shortest in the tree
   if (code == 0 && length > minimumBitLength())
      setMinBitLength(length) ;
   // the all-ones code is the longest in the tree
   else if (code == all_ones[length] && length < maximumBitLength())
      setMaxBitLength(length) ;
   for (HuffmanCode mask = (1 << (length-1)) ; mask > 1 ; mask >>= 1)
      {
      bool right = ((code & mask) != 0) ;
      HuffmanChildInfo childinfo = nodes[index].getChild(right) ;
      if (childinfo.isValid())
	 {
	 if (childinfo.isLeaf())
	    return false ; // inconsistent!
	 index = childinfo.childIndex() ;
	 }
      else
	 {
	 // insert a new node for the next bit of the code
	 unsigned childindex = allocateNode() ;
	 if (right)
	    nodes[index].setRightChild(childindex) ;
	 else
	    nodes[index].setLeftChild(childindex) ;
	 index = childindex ;
	 }
      }
   if ((code & 1) != 0)
      {
      if (nodes[index].rightChild().isValid())
	 return false ;			// already assigned, thus not added
      nodes[index].makeRightLeaf(symbol) ;
      nodes[index].setRightExtraBits(extra_bits) ;
      }
   else
      {
      if (nodes[index].leftChild().isValid())
	 return false ;			// already assigned, thus not added
      nodes[index].makeLeftLeaf(symbol) ;
      nodes[index].setLeftExtraBits(extra_bits) ;
      }
   updateLeftmost(code,length) ;
   updateRightmost(code,length) ;
   incrExtra(extra_bits) ;
   if (length > maxCodeLength())
      {
      m_max_length_used = length ;
      }
   return true ;
}

//----------------------------------------------------------------------

static void dump_tree(const HuffmanTreeNode *nodes, unsigned index,
		      unsigned depth, char *digits)
{
   HuffmanChildInfo leftchild = nodes[index].leftChild() ;
   digits[depth] = '0' ;
   digits[depth+1] = '\0' ;
   if (leftchild.isValid())
      {
      if (leftchild.isLeaf())
	 {
	 cerr << digits ;
	 if (leftchild.extraBits() != EXTRA_ISLITERAL)
	    cerr << "+" << leftchild.extraBits() ;
	 if (leftchild.symbol() != NODEINFO_SYMBOL_UNKNOWN)
	    cerr << " = " << leftchild.symbol() ;
	 cerr << endl ;
	 }
      else
	 {
	 unsigned child = leftchild.childIndex() ;
	 if (child != ROOT_NODE)
	    dump_tree(nodes,child,depth+1,digits) ;
	 }
      }
   else
      {
      cerr << digits << " ?" << endl ;
      }
   HuffmanChildInfo rightchild = nodes[index].rightChild() ;
   digits[depth] = '1' ;
   digits[depth+1] = '\0' ;
   if (rightchild.isValid())
      {
      if (rightchild.isLeaf())
	 {
	 cerr << digits ;
	 if (rightchild.extraBits() != EXTRA_ISLITERAL)
	    cerr << "+" << rightchild.extraBits() ;
	 if (rightchild.symbol() != NODEINFO_SYMBOL_UNKNOWN)
	    cerr << " = " << rightchild.symbol() ;
	 cerr << endl ;
	 }
      else
	 {
	 unsigned child = rightchild.childIndex() ;
	 if (child != ROOT_NODE)
	    dump_tree(nodes,child,depth+1,digits) ;
	 }
      }
   else
      {
      cerr << digits << " ?" << endl ;
      }
   return ;
}

//----------------------------------------------------------------------

void PartialHuffmanTreeBase::dump(const HuffmanTreeNode *nodes) const
{
   char digits[MAX_BITLENGTH+3] ;
   digits[0] = '\0' ;
   dump_tree(nodes,ROOT_NODE,0,digits) ;
   return ;
}

/************************************************************************/
/*	Methods for class HuffmanInfo					*/
/************************************************************************/

HuffmanInfo::HuffmanInfo(const HuffmanInfo *orig, const BitPointer &pos,
			 size_t extension_len)
   : m_litcodes(orig->m_litcodes),
     m_distcodes(orig->m_distcodes),
     m_startpos(pos)
{
   setNext(nullptr) ;
   m_lastliteral = orig->lastLiteral() ;
   m_lastlitlength = orig->lastLiteralLength() ;
   m_lastlitcount = orig->lastLiteralRepeat() ;
   m_bitcount = orig->bitCount() + extension_len ;
   return ;
}

//----------------------------------------------------------------------

bool HuffmanInfo::consistentLiteral(HuffmanCode code, unsigned length) const
{
   bool present ;
   return m_litcodes.consistentWithTree(code,length,EXTRA_ISLITERAL,
					present) ;
}

//----------------------------------------------------------------------

bool HuffmanInfo::consistentMatchLength(HuffmanCode code, unsigned len_bits,
					unsigned extra_bits ) const
{
   bool present ;
   bool consistent
      = m_litcodes.consistentWithTree(code,len_bits,extra_bits,present) ;
   if (consistent && !present && extraLiteralBitsAtLimit(extra_bits))
      return false ;
   return consistent ;
}

//----------------------------------------------------------------------

bool HuffmanInfo::consistentDistance(HuffmanCode code, unsigned dist_bits,
				     unsigned extra_bits) const
{
   bool present ;
   bool consistent
      = m_distcodes.consistentWithTree(code,dist_bits,extra_bits,present) ;
   if (consistent && !present && extraDistanceBitsAtLimit(extra_bits))
      return false ;
   return consistent ;
}

//----------------------------------------------------------------------

void HuffmanInfo::updateLastLiteral(HuffmanCode code, unsigned length)
{
   if (length == lastLiteralLength() && code == lastLiteral())
      {
      m_lastlitcount++ ;
      }
   else
      {
      m_lastliteral = code ;
      m_lastlitlength = length ;
      m_lastlitcount = 1 ;
      }
   return ;
}

//----------------------------------------------------------------------

bool HuffmanInfo::excessiveRepeats(HuffmanCode code, unsigned length) const
{
   if (length == lastLiteralLength() && code == lastLiteral())
      return lastLiteralRepeat() >= MAX_LITERAL_REPEATS ;
   return false ;
}

//----------------------------------------------------------------------

HuffmanInfo *HuffmanInfo::extend(const BitPointer& position, HuffmanCode code,
				 unsigned len, unsigned symbol) const
{
   if (code == all_ones[len] &&
       (len < m_litcodes.maxCodeLength() || len < NEEDED_LIT_BITS))
      return nullptr ;
   Owned<HuffmanInfo> new_info(this,position,len) ;
   if (new_info)
      {
      new_info->updateLastLiteral(code,len) ;
      new_info->m_litcodes.add(code,len,EXTRA_ISLITERAL,symbol) ;
      }
   return new_info.move() ; //FIXME
}

//----------------------------------------------------------------------

HuffmanInfo *HuffmanInfo::extend(const BitPointer& position, HuffmanCode code,
				 unsigned matchlen, unsigned matchextra,
				 HuffmanCode distcode, unsigned distlen,
				 unsigned distextra) const
{
   if (code == all_ones[matchlen] && 
       (matchlen < m_litcodes.maxCodeLength() || matchlen < NEEDED_LIT_BITS))
      return nullptr ;
   size_t extension = matchlen + matchextra + distlen + distextra ;
   if (distcode == all_ones[distlen] &&
       (distlen < m_distcodes.maxCodeLength() || distlen < NEEDED_DIST_BITS))
      return nullptr ;
   Owned<HuffmanInfo> new_info(this,position,extension) ;
   if (new_info)
      {
      new_info->clearLastLiteral() ;
      new_info->m_litcodes.add(code,matchlen,matchextra) ;
      new_info->m_distcodes.add(distcode,distlen,distextra) ;
      }
   return new_info.move() ; //FIXME
}

/************************************************************************/
/************************************************************************/

static bool extend_bitstream(HuffmanHypothesis* hyp, HuffmanSearchQueue &search_queue,
			     const BitPointer* str_start, HuffmanSearchQueue& longest_streams) ;

//----------------------------------------------------------------------

static bool add_extension(const BitPointer* str_start, HuffmanHypothesis* hyp,
			  HuffmanSearchQueue& search_queue, HuffmanSearchQueue& longest_streams)
{
   if (!hyp)
      return false ;
   if (hyp->bitCount() <= search_queue.shiftCount())
      {
      if (extend_bitstream(hyp,search_queue,str_start,longest_streams))
	 return true ;
      }
   else if (search_queue.push(hyp))
      {
      if (verbosity >= 3 && hyp->bitCount() > 400000)
	 {
	 cerr << "added consistent stream of " << hyp->bitCount()
	      << " bits to queue" << endl << flush ;
	 }
      return true ;
      }
   return false ;
}

//----------------------------------------------------------------------

static bool extend_bitstream(HuffmanHypothesis* hyp, HuffmanSearchQueue& search_queue,
			     const BitPointer* str_start, HuffmanSearchQueue& longest_streams)
{
   INCR_STAT(total_expansions) ;
//if(STAT_COUNT(total_expansions) > 2*1000*1000){cerr<<search_queue.totalAdditions()<<endl;exit(1) ;}
   if (verbosity &&
       (STAT_COUNT(total_expansions) % EXPANSION_REPORT_INTERVAL) == 0)
      {
      cerr << "." << flush ;
      if ((STAT_COUNT(total_expansions) % (50 * EXPANSION_REPORT_INTERVAL)) == 0)
	 {
	 cerr << " " << setw(10) << search_queue.totalAdditions()
	      << setw(0) << " @ " << search_queue.shiftCount()
	      << endl << flush ;
	 SET_STAT(search_additions,search_queue.totalAdditions()) ;
	 SET_STAT(search_dups,search_queue.duplicatesSkipped()) ;
	 SET_STAT(longest_additions,longest_streams.totalAdditions()) ;
         if (verbosity > VERBOSITY_PACKETS)
	    {
	    print_partial_packet_statistics() ;
	    memory_stats(cout);
	    }
	 gc() ;
	 }
      }
   if (search_queue.full() || !hyp)
      {
      INCR_STAT(queue_full) ;
      return false ;
      }
   bool extended = false ;
   const BitPointer *str_currpos = hyp->startPosition() ;
   unsigned min_bitlength = hyp->minBitLength() ;
   unsigned max_bitlength = hyp->maxBitLength() ;
   if (hyp->inBackReference())
      {
      // extend by possible match-length codes
      for (unsigned extra = 0 ; extra <= MAX_LENGTH_EXTRABITS ; extra++)
	 {
	 BitPointer new_start(str_currpos) ;
	 new_start.retreat(extra) ;
	 if (new_start < *str_start)
	    break ;
	 HuffmanCode code = 0 ;
	 if (min_bitlength > 1)
	    code = new_start.prevBitsReversed(min_bitlength-1) ;
	 for (unsigned len = min_bitlength ; len <= max_bitlength ; len++)
	    {
	    uint32_t bit = new_start.prevBit() ;
	    if (new_start < *str_start)
	       break ;
	    code |= (bit << (len - 1)) ;
	    if (hyp->consistentMatchLength(code,len,extra))
	       {
	       HuffmanHypothesis *new_hyp
		  = hyp->extend(new_start,code,len,extra,false) ;
//cerr<<"add gen"<<hyp->generation()<<" @ "<<hyp->bitCount()<<": len "<<binary(code,len)<<"+"<<extra<<endl;
	       if (add_extension(str_start,new_hyp,search_queue,
				 longest_streams))
		  {
		  extended = true ;
		  }
	       }
	    }
	 if (search_queue.full())
	    {
	    INCR_STAT(queue_full) ;
	    break ;
	    }
	 }
      }
   else
      {
      // scan for possible literal codes preceding current start
      BitPointer new_start(str_currpos) ;
      HuffmanCode code = 0 ;
      if (min_bitlength > 1)
	 code = new_start.prevBitsReversed(min_bitlength-1) ;
      for (unsigned length=min_bitlength ; length<=max_bitlength ; length++)
	 {
	 uint32_t bit = new_start.prevBit() ;
	 if (new_start < *str_start)
	    break ;
	 code |= (bit << (length - 1)) ;
	 if (!hyp->excessiveRepeats(code,length) &&
	     hyp->consistentLiteral(code, length))
	    {
	    HuffmanHypothesis *new_hyp = hyp->extend(new_start, code, length) ;
//cerr<<"add gen"<<hyp->generation()<<" @ "<<hyp->bitCount()<<": lit "<<binary(code,length)<<endl;
	    if (add_extension(str_start,new_hyp,search_queue,longest_streams))
	       {
	       extended = true ;
	       if (search_queue.full())
		  {
		  INCR_STAT(queue_full) ;
		  break ;
		  }
	       }
	    }
	 }
      // scan for possible back-references preceding current start; each is
      //   a match length followed by a distance length, so iterate over
      //   the possible distance codes first (they are also more constrained
      //   and thus have a lower effective fan-out) and flag the extended
      //   hypothesis as requiring a match length.
      unsigned min_dist_len = hyp->minDistanceLength() ;
      unsigned max_dist_len = hyp->maxDistanceLength() ;
      for (size_t extra = 0 ; extra <= MAX_DISTANCE_EXTRABITS ; extra++)
	 {
	 BitPointer new_pos(str_currpos) ;
	 new_pos.retreat(extra) ;
	 if (new_pos < *str_start)
	    break ;
	 HuffmanCode distcode = 0 ;
	 if (min_dist_len > 1)
	    distcode = new_pos.prevBitsReversed(min_dist_len - 1) ;
	 for (unsigned len = min_dist_len ; len <= max_dist_len ; len++)
	    {
	    uint32_t bit = new_pos.prevBit() ;
	    if (new_pos < *str_start)
	       break ;
	    distcode |= (bit << (len - 1)) ;
	    if (hyp->consistentDistance(distcode, len, extra))
	       {
	       HuffmanHypothesis *new_hyp
		  = hyp->extend(new_pos, distcode, len, extra, true) ;
//cerr<<"add gen"<<hyp->generation()<<" @ "<<hyp->bitCount()<<": dist "<<binary(distcode,len)<<"+"<<extra<<endl;
	       if (add_extension(str_start,new_hyp,search_queue,
				 longest_streams))
		  {
		  extended = true ;
		  if (search_queue.full())
		     {
		     INCR_STAT(queue_full) ;
		     break ;
		     }
		  }
	       }
	    }
	 }
      }
   // dispose of the given HuffmanHypothesis instance in accordance with the
   //   result of the extension attempts
   size_t shiftcount = longest_streams.shiftCount() ;
   if (extended || hyp->bitCount() <= shiftcount)
      {
      delete hyp ;
      }
   else
      {
      bool added = longest_streams.push(hyp) ;
      if (longest_streams.shiftCount() > shiftcount)
	 {
	 // we have a new longest stream
	 if (verbosity >= 2)
	    cerr << "found longest consistent stream of " << hyp->bitCount()
		 << " bits" << endl << flush ;
	 }
      else if (verbosity >= 3 && added &&
	       hyp->bitCount() >= KEEP_ALL_THRESHOLD)
	 cerr << "found consistent stream of " << hyp->bitCount()
	      << " bits" << endl << flush ;
      }
   return extended ;
}

//----------------------------------------------------------------------

static bool add_literal_code(HuffSymbol sym, VariableBits codestring,
			     void *user_data)
{
   HuffmanHypothesis *hyp = (HuffmanHypothesis*)user_data ;
   HuffmanCode code = (HuffmanCode)codestring.value() ;
   unsigned extra = 0 ; //FIXME
   hyp->addLitCode(code,codestring.length(),extra,(unsigned)sym) ;
   return true ;
}

//----------------------------------------------------------------------

static bool add_distance_code(HuffSymbol sym, VariableBits codestring,
			      void *user_data)
{
   HuffmanHypothesis *hyp = (HuffmanHypothesis*)user_data ;
   HuffmanCode code = (HuffmanCode)codestring.value() ;
   unsigned extra = 0 ; //FIXME
   hyp->addDistCode(code,codestring.length(),extra,(unsigned)sym) ;
   return true ;
}

//----------------------------------------------------------------------

static HuffmanHypothesis *find_longest_streams(const BitPointer *str_start,
					       const BitPointer *str_end,
					       const HuffSymbolTable *symtab)
{
   CpuTimer timer ;
   HuffmanSearchQueue search_queue(MAX_SEARCH,SEARCH_QUEUE_SIZE) ;
   HuffmanSearchQueue longest_streams(MAX_LONGEST,MAX_EXTENSION-1,true) ;
   longest_streams.shift(KEEP_NONE_THRESHOLD) ;
   HuffmanHypothesis empty_hyp(str_end) ;
   // search on each possible EOD code, ordering them by likelihood
   for (size_t i = 0 ; eod_lengths[i] ; i++)
      {
      size_t eod_length = eod_lengths[i] ;
      BitPointer str_pos(str_end) ;
      str_pos.retreat(eod_length) ;
      HuffmanCode code = str_pos.getBitsReversed(eod_length) ;
      if (symtab)
	 {
	 VariableBits eod ;
	 symtab->getEOD(eod) ;
	 if (eod.length() != eod_length)
	    continue ;
	 if (eod.value() != code)
	    {
	    if (verbosity)
	       cerr << "  inconsistent EOD value in packet" << endl ;
	    break ;
	    }
	 }
      if (verbosity >= VERBOSITY_SCAN)
	 cerr << "  EOD length=" << eod_length << endl << flush ;
      HuffmanHypothesis *hyp
	 = empty_hyp.extend(str_pos,code,eod_length,END_OF_DATA) ;
      if (!hyp)
	 continue ;
      if (symtab)
	 {
	 // build up the trees in the HuffmanHypothesis from the code strings
	 //   in the HuffSymbolTable
	 symtab->iterateCodeTree(add_literal_code,hyp) ;
	 symtab->iterateDistTree(add_distance_code,hyp) ;
	 }
      else
	 {
	 // for length=7, we may be dealing with a fixed-Huffman packet,
	 //   for which the maximum bit length is 9; in all other cases,
	 //   the fact that EOD occurs exactly once ensures that it is in
	 //   the equivalence class of least-frequent symbols, which means
	 //   that it will have either the longest or next-to-longest code
	 //   length
	 hyp->setMaxBitLength(eod_length == 7 ? 9 : eod_length+1) ;
	 }
      if (verbosity > VERBOSITY_SCAN)
	 {
	 cerr << "== litcodes ==" << endl ;
	 hyp->dumpLitCodes() ;
	 cerr << "== distcodes ==" << endl ;
	 hyp->dumpDistCodes() ;
	 }
      (void)extend_bitstream(hyp,search_queue,str_start,longest_streams) ;
      if (verbosity)
	 cerr << endl ; // terminate the line with trace characters
      }
   if (verbosity > VERBOSITY_PACKETS)
      cerr << "start queue loop" << endl ;
   // iterate until the queue is empty:
   //   1. pop a search node
   //   2. expand it, inserting any valid expansions into the queue
   //   3. if there are no valid expansions, add the popped node to
   //      the list of longest bitstreams, removing any which are shorter
   while (search_queue.conditionalShift())
      {
      HuffmanHypothesis *hyp = search_queue.pop() ;
      (void)extend_bitstream(hyp,search_queue,str_start,longest_streams) ;
      }
   if (verbosity)
      cerr << endl ; // terminate the line with trace characters
   if (verbosity > VERBOSITY_PACKETS)
      {
      memory_stats(cerr) ;
      cerr << "queue loop done, returning " << longest_streams.queueSize()
	   << endl ;
      cerr << "  total additions to longest_streams = "
	   << longest_streams.totalAdditions() << endl ;
      cerr << "CPU time used = " << timer.seconds() << " seconds" << endl ;
      }
   ADD_TO_STAT(search_additions,search_queue.totalAdditions()) ;
   ADD_TO_STAT(search_dups,search_queue.duplicatesSkipped()) ;
   ADD_TO_STAT(longest_additions,longest_streams.totalAdditions()) ;
   return longest_streams.popAll() ;
}

//----------------------------------------------------------------------

bool search(const BitPointer* str_start, const BitPointer* str_end, BitPointer* packet_header, bool deflate64)
{
cerr<<"stream length = "<<(8*(*str_end - *str_start))<<" bits (approx)"<<endl;
   if (!packet_header &&
       (*str_end - *str_start) < KEEP_NONE_THRESHOLD / 8)
      return false ;
   HuffmanTreeHypothesis::initializeCodeAllocators() ;
   lit_tree_directory.reinit(LIT_TREE_DIR_SIZE) ;
   dist_tree_directory.reinit(DIST_TREE_DIR_SIZE) ;
   Owned<HuffSymbolTable> symtab { nullptr } ;
   if (packet_header)
      {
      // get the packet's type
      uint32_t phdr = packet_header->nextBits(PACKHDR_SIZE) ;
      switch (PACKHDR_TYPE(phdr))
	 {
	 case PT_INVALID:
	    return false ;
	 case PT_FIXEDHUFF:
	    symtab = HuffSymbolTable::buildDefault(deflate64) ;
	    break ;
	 case PT_DYNAMIC:
	    symtab = HuffSymbolTable::build(*packet_header,str_end,false) ;
	    break ;
	 case PT_UNCOMP:
	    return false ; // can't happen
	 }
      }
   HuffmanHypothesis *longest = find_longest_streams(str_start,str_end,symtab) ;
//FIXME

   // output results if requested
   print_partial_packet_statistics();
   if (verbosity >= VERBOSITY_PACKETS)
      {
      for (auto hyp = longest ; longest ; longest = longest->next())
	 {
	 cerr << "hyp, len=" << hyp->bitCount() << endl ;
	 hyp->dumpLitCodes();
	 cerr << "--dist--" << endl ;
	 hyp->dumpDistCodes();
	 cerr << "----------" << endl ;
	 }
      }
   free_hypotheses(longest) ;
   HuffmanTreeHypothesis::releaseCodeAllocators() ;
   lit_tree_directory = nullptr ;
   dist_tree_directory = nullptr ;
   return true ;
}

//----------------------------------------------------------------------

HuffmanHypothesis* search(const BitPointer* str_start, const BitPointer* str_end, const HuffSymbolTable* symtab)
{
   if (!symtab)
      return nullptr ;
   HuffmanTreeHypothesis::initializeCodeAllocators() ;
   lit_tree_directory.reinit(LIT_TREE_DIR_SIZE) ;
   dist_tree_directory.reinit(DIST_TREE_DIR_SIZE) ;
   HuffmanHypothesis *longest = find_longest_streams(str_start,str_end,symtab) ;
   // output results if requested
   print_partial_packet_statistics();
   if (verbosity >= VERBOSITY_PACKETS)
      {
      while (longest)
	 {
	 HuffmanHypothesis *hyp = longest ; 
	 longest = longest->next() ; 
	 cerr << "hyp, len=" << hyp->bitCount() << endl ;
	 hyp->dumpLitCodes();
	 cerr << "--dist--" << endl ;
	 hyp->dumpDistCodes();
	 cerr << "----------" << endl ;
	 }
      }
   HuffmanTreeHypothesis::releaseCodeAllocators() ;
   lit_tree_directory = nullptr ;
   dist_tree_directory = nullptr ;
   return longest ;
}

//----------------------------------------------------------------------

void print_partial_packet_statistics()
{
   if (show_stats && STAT_COUNT(total_expansions) > 0)
      {
      fprintf(stdout,"Partial-Packet Recovery:\n") ;
      fprintf(stdout,"  %lu search-node expansions\n",
	      (unsigned long)STAT_COUNT(total_expansions)) ;
      fprintf(stdout,"  %lu search-queue insertions\n",
	      (unsigned long)STAT_COUNT(search_additions)) ;
      fprintf(stdout,"  %lu search-queue duplicates skipped\n",
	      (unsigned long)STAT_COUNT(search_dups)) ;
      fprintf(stdout,"  %lu search-queue full occurrences\n",
	      (unsigned long)STAT_COUNT(queue_full)) ;
      fprintf(stdout,"  %lu result-queue insertions\n",
	      (unsigned long)STAT_COUNT(longest_additions)) ;
      fprintf(stdout,"  %lu Huffman-tree insertions\n",
	      (unsigned long)STAT_COUNT(tree_insertions)) ;
      fprintf(stdout,"     %lu codes already present\n",
	      (unsigned long)STAT_COUNT(tree_present)) ;
      fprintf(stdout,"     %lu codes would generate invalid tree\n",
	      (unsigned long)STAT_COUNT(tree_conflict)) ;
      fprintf(stdout,"     %lu codes generated duplicate tree\n",
	      (unsigned long)STAT_COUNT(tree_duplicates)) ;
      }
   return ;
}

// end of file partial.C //
