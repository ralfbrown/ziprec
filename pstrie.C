/****************************** -*- C++ -*- *****************************/
/*									*/
/*	LangIdent: n-gram based language-identification			*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  File: pstrie.C - packed simple Word-frequency trie			*/
/*  Version:  1.10beta				       			*/
/*  LastEdit: 2019-07-28						*/
/*									*/
/*  (c) Copyright 2012,2013,2015,2019 Ralf Brown/CMU			*/
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

#include <functional>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <cstring>
#include "whatlang2/mtrie.h"
#include "pstrie.h"
#include "wildcard.h"

#include "framepac/byteorder.h"
#include "framepac/file.h"
#include "framepac/memory.h"
#include "framepac/mmapfile.h"
#include "framepac/utility.h"

using namespace std ;
using namespace Fr ;

/************************************************************************/
/*	Manifest Constants						*/
/************************************************************************/

// since no node will ever point at the root, we can re-use the root
//   index as the null pointer
#define NOCHILD_INDEX 0

#define PACKEDTRIE_SIGNATURE "PackedTrie\0"
#define PACKEDTRIE_FORMAT_MIN_VERSION 1 // earliest format we can read
#define PACKEDTRIE_FORMAT_VERSION 1

// reserve some space for future additions to the file format
#define PACKEDTRIE_PADBYTES_1  58

/************************************************************************/
/*	Types								*/
/************************************************************************/

class EnumerationInfo
   {
   public:
      const LangIDPackedTrie  *trie ;
      PackedTrieMatch *matches ;
      uint8_t *key ;
      const WildcardSet **alternates ;
      unsigned max_keylen ;
      unsigned max_matches ;
      mutable unsigned num_matches ;
      bool extensible ;

   public:
      EnumerationInfo(const LangIDPackedTrie *t,
		      uint8_t *k,
		      unsigned keylen,
		      unsigned match,
		      PackedTrieMatch *matchinfo,
		      const WildcardSet **alts,
		      bool ext)
	 {
	    trie = t ;
	    alternates = alts ;
	    matches = matchinfo ;
	    key = k ;
	    max_keylen = keylen ;
	    max_matches = match ;
	    num_matches = 0 ;
	    extensible = ext ;
	 }

      void setMatch(const PackedSimpleTrieNode *node, unsigned keylen) const
	 {
	 if (num_matches < max_matches)
	    {
	    matches[num_matches].setNode(node) ;
	    matches[num_matches].setKey(key,keylen) ;
	    num_matches++ ;
	    }
	 return ;
	 }
   } ;

/************************************************************************/
/*	Global variables						*/
/************************************************************************/

void write_escaped_key(CFile& f, const uint8_t* key, unsigned keylen) ;

/************************************************************************/
/*	Helper functions						*/
/************************************************************************/

#ifndef lengthof
#  define lengthof(x) (sizeof(x)/sizeof((x)[0]))
#endif /* lengthof */

/************************************************************************/
/*	Methods for class PackedSimpleTrieNode				*/
/************************************************************************/

PackedSimpleTrieNode::PackedSimpleTrieNode()
{
   setFrequency(INVALID_FREQ) ;
   setFirstChild(0) ;
   memset(m_children,'\0',sizeof(m_children)) ;
   return ;
}

//----------------------------------------------------------------------

unsigned PackedSimpleTrieNode::numChildren() const
{
   return (m_popcounts[lengthof(m_popcounts)-1] + popcount(m_children[lengthof(m_popcounts)-1].load())) ;
}

//----------------------------------------------------------------------

bool PackedSimpleTrieNode::childPresent(unsigned int N) const 
{
   if (N >= PTRIE_CHILDREN_PER_NODE)
      return false ;
   uint64_t children = m_children[N / M_CHILDREN_BITS].load() ;
   uint64_t mask = (1U << (N % M_CHILDREN_BITS)) ;
   return (children & mask) != 0 ;
}

//----------------------------------------------------------------------

uint32_t PackedSimpleTrieNode::childIndex(unsigned int N) const
{
   if (N >= PTRIE_CHILDREN_PER_NODE)
      return LangIDPackedTrie::NULL_INDEX ;
   uint64_t mask = (1U << (N % M_CHILDREN_BITS)) - 1 ;
   uint64_t children = m_children[N / M_CHILDREN_BITS].load() ;
   return (firstChild() + m_popcounts[N / M_CHILDREN_BITS] + popcount(children & mask)) ;
}

//----------------------------------------------------------------------

uint32_t PackedSimpleTrieNode::childIndexIfPresent(unsigned int N) const
{
   if (N >= PTRIE_CHILDREN_PER_NODE)
      return LangIDPackedTrie::NULL_INDEX ;
   uint64_t mask = (1U << (N % M_CHILDREN_BITS)) ;
   uint64_t children = m_children[N / M_CHILDREN_BITS].load() ;
   if ((children & mask) == 0)
      return LangIDPackedTrie::NULL_INDEX ;
   mask-- ;
   return (firstChild() + m_popcounts[N / M_CHILDREN_BITS] + popcount(children & mask)) ;
}

//----------------------------------------------------------------------

PackedSimpleTrieNode* PackedSimpleTrieNode::childNodeIfPresent(unsigned int N, const LangIDPackedTrie *trie)
   const
{
   uint32_t index = childIndexIfPresent(N) ;
   return index != LangIDPackedTrie::NULL_INDEX ? trie->node(index) : nullptr ;
}

//----------------------------------------------------------------------

void PackedSimpleTrieNode::setChild(unsigned N)
{
   if (N < PTRIE_CHILDREN_PER_NODE)
      {
      uint64_t mask = (1U << (N % M_CHILDREN_BITS)) ;
      m_children[N / M_CHILDREN_BITS] |= mask ;
      }
   return ;
}

//----------------------------------------------------------------------

void PackedSimpleTrieNode::setPopCounts()
{
   // set up running population counts for faster lookup of children
   unsigned pcount = 0 ;
   for (size_t i = 0 ; i < lengthof(m_popcounts) ; i++)
      {
      m_popcounts[i] = (uint8_t)pcount ;
      pcount += popcount(m_children[i].load()) ;
      }
   return ;
}

//----------------------------------------------------------------------

bool PackedSimpleTrieNode::nextFrequencies(const LangIDPackedTrie* trie, uint32_t* frequencies) const
{
   if (!frequencies || trie->terminalNode(this))
      return false ;
   uint32_t child = firstChild() ;
   for (size_t N = 0 ; N < lengthof(m_children) ; N++)
      {
      uint64_t children = m_children[N].load() ;
      unsigned i = 64 ;
      while (children)
	 {
	 *frequencies++ = (children&1) ? trie->node(child++)->frequency() : 0 ;
	 children >>= 1 ;
	 i-- ;
	 }
      std::fill_n(frequencies,i,0) ;
      frequencies += i ;
      }
   return (child != firstChild()) ;
}

//----------------------------------------------------------------------

// although this function breaks data abstraction by knowing about how ZipRec
//   computes its ngram scores, it saves a fair amount of compute time by
//   avoiding a second pass over the 256-element array of frequencies that
//   one gets with nextFrequencies()
// PRECOND: 'this' must not be a terminal node
bool PackedSimpleTrieNode::addToScores(const LangIDPackedTrie* trie, float* scores, double weight) const
{
//   if (trie->terminalNode(this))
//      return false ;
   uint32_t child = firstChild() ;
   for (size_t N = 0 ; N < lengthof(m_children) ; N++)
      {
      uint64_t children = m_children[N].load() ;
      unsigned i = 64 ;
      while (children)
	 {
	 if ((children & 1) != 0)
	    {
	    *scores += (float)(weight * trie->node(child++)->frequency()) ;
	    }
	 scores++ ;
	 i-- ;
	 children >>= 1 ;
	 }
      scores += i ;
      }
   return (child != firstChild()) ;
}

//----------------------------------------------------------------------

// although this function breaks data abstraction by knowing about how ZipRec
//   computes its ngram scores, it saves a fair amount of compute time by
//   avoiding a second pass over the 256-element array of frequencies that
//   one gets with nextFrequencies()
// PRECOND: 'this' must not be a terminal node
bool PackedSimpleTrieNode::addToScores(const LangIDPackedTrie* trie, double* scores, double weight) const
{
//   if (trie->terminalNode(this))
//      return false ;
   uint32_t child = firstChild() ;
   for (size_t N = 0 ; N < lengthof(m_children) ; N++)
      {
      uint64_t children = m_children[N].load() ;
      unsigned i = 64 ;
      while (children)
	 {
	 if ((children & 1) != 0)
	    {
	    *scores += weight * trie->node(child++)->frequency() ;
	    }
	 scores++ ;
	 i-- ;
	 children >>= 1 ;
	 }
      scores += i ;
      }
   return (child != firstChild()) ;
}

//----------------------------------------------------------------------

unsigned PackedSimpleTrieNode::countMatches(const LangIDPackedTrie *trie,
					    const uint8_t *key,
					    unsigned keylen,
					    const WildcardSet **alternatives,
					    unsigned max_matches,
					    bool nonterminals_only) const
{
   if (keylen == 0)			// if we get to the end of the key,
      {					//   we successfully matched
      return (!nonterminals_only || !trie->terminalNode(this)) ? 1 : 0 ;
      }
   if (trie->terminalNode(this))
      return 0 ;
   if (alternatives[0] && alternatives[0]->setSize() > 0)
      {
      // we have a wildcard, so scan all the possibilities
      unsigned matches = 0 ;
      uint32_t child = firstChild() ;
      for (size_t N = 0 ; N < lengthof(m_children) ; ++N)
	 {
	 uint64_t children = m_children[N].load() ;
	 for (size_t i = 0 ; children ; i++)
	    {
	    if ((children & 1) != 0)
	       {
	       if (alternatives[0]->contains(N*M_CHILDREN_BITS+i))
		  {
		  PackedSimpleTrieNode *childnode = trie->node(child) ;
		  if (!childnode)
		     continue ;
		  matches += childnode->countMatches(trie,key+1,keylen-1, alternatives+1,
						     max_matches - matches, nonterminals_only) ;
		  if (matches > max_matches)
		     return matches ;
		  }
	       child++ ;
	       }
	    children >>= 1 ;
	    }
	 }
      return matches ;
      }
   else
      {
      // the current key byte must match
      PackedSimpleTrieNode *childnode = childNodeIfPresent(*key,trie) ;
      if (childnode)
	 return childnode->countMatches(trie,key+1,keylen-1,
					alternatives+1,
					max_matches,
					nonterminals_only) ;
      return 0 ;
      }
}

//----------------------------------------------------------------------

bool PackedSimpleTrieNode::enumerateMatches(const LangIDPackedTrie* trie, uint8_t* keybuf,
					    unsigned max_keylength, unsigned curr_keylength,
					    const WildcardSet** alternatives, PackedSimpleTrieMatchFn* fn,
					    void* user_data) const
{
   if (curr_keylength >= max_keylength)
      return fn(keybuf,curr_keylength,trie,this,user_data) ;
   else if (trie->terminalNode(this))
      return true ;
   if (alternatives[curr_keylength] && alternatives[curr_keylength]->setSize() > 0)
      {
      // we have a wildcard, so enumerate the possibilities
      uint32_t child = firstChild() ;
      for (size_t N = 0 ; N < lengthof(m_children) ; ++N)
	 {
	 uint64_t children = m_children[N].load() ;
	 for (size_t i = 0 ; children ; i++)
	    {
	    if ((children & 1) != 0)
	       {
	       unsigned index = N * M_CHILDREN_BITS + i ;
	       if (alternatives[curr_keylength]->contains(index))
		  {
		  PackedSimpleTrieNode *childnode = trie->node(child) ;
		  keybuf[curr_keylength] = (uint8_t)index ;
		  if (!childnode->enumerateMatches(trie,keybuf,max_keylength,curr_keylength+1,alternatives,
						   fn,user_data))
		     return false ;
		  }
	       child++ ;
	       }
	    children >>= 1 ;
	    }
	 }
      }
   else
      {
      // the current key byte must match
      PackedSimpleTrieNode *childnode
	 = childNodeIfPresent(keybuf[curr_keylength],trie) ;
      if (childnode &&
	  !childnode->enumerateMatches(trie,keybuf,max_keylength,
				       curr_keylength+1,alternatives,
				       fn,user_data))
	 {
	 return false ;
	 }
      }
   return true ;
}

//----------------------------------------------------------------------

bool PackedSimpleTrieNode::enumerateChildren(const LangIDPackedTrie* trie, uint8_t* keybuf,
					     unsigned max_keylength_bits, unsigned curr_keylength_bits,
					     PackedSimpleTrieEnumFn* fn, void* user_data) const
{
   if (leaf() && !fn(keybuf,curr_keylength_bits/8,frequency(),user_data))
      return false ;
   else if (trie->terminalNode(this))
      return true ;
   if (curr_keylength_bits < max_keylength_bits)
      {
      unsigned curr_bits = curr_keylength_bits + PTRIE_BITS_PER_LEVEL ;
      uint32_t child = firstChild() ;
      for (size_t N = 0 ; N < lengthof(m_children) ; ++N)
	 {
	 uint64_t children = m_children[N].load() ;
	 for (size_t i = 0 ; children ; i++)
	    {
	    if ((children & 1) != 0)
	       {
	       PackedSimpleTrieNode *childnode = trie->node(child++) ;
	       if (childnode)
		  {
		  unsigned byte = curr_keylength_bits / 8 ;
		  keybuf[byte] = N * M_CHILDREN_BITS + i ;
		  if (!childnode->enumerateChildren(trie,keybuf, max_keylength_bits, curr_bits,fn,user_data))
		     return false ;
		  }
	       }
	    children >>= 1 ;
	    }
	 }
      }
   return true ;
}

//----------------------------------------------------------------------

unsigned PackedSimpleTrieNode::enumerateMatches(const EnumerationInfo *info,
						unsigned keylen) const
{
   const LangIDPackedTrie *trie = info->trie ;
   if (keylen >= info->max_keylen)	// if we get to the end of the key,
      {					//   we successfully matched
      if (!leaf())
	 return 0 ;
      if (info->extensible && (trie->terminalNode(this) || frequency() == 0))
	 return 0 ;
      info->setMatch(this,keylen) ;
      return 1 ;
      }
   if (trie->terminalNode(this))
      return 0 ; // no extension possible, so not a match
   const WildcardSet *alternative = info->alternates[keylen] ;
   if (alternative && alternative->setSize() > 0)
      {
      // we have a wildcard, so scan all the possibilities
      unsigned count = 0 ;
      uint32_t child = firstChild() ;
      if (trie->isTerminalNode(child))
	 {
	 for (size_t N = 0 ; N < lengthof(m_children) ; ++N)
	    {
	    uint64_t children = m_children[N].load() ;
	    for (size_t i = 0 ; children ; i++)
	       {
	       if ((children & 1) != 0)
		  {
		  unsigned index = N * M_CHILDREN_BITS + i ;
		  if (alternative->contains(index))
		     {
		     PackedSimpleTrieNode *childnode = trie->getTerminalNode(child) ;
		     info->key[keylen] = (uint8_t)index ;
		     count += childnode->enumerateMatches(info,keylen+1) ;
		     if (count > info->max_matches)
			return count ;
		     }
		  child++ ;
		  }
	       children >>= 1 ;
	       }
	    }
	 }
      else
	 {
	 for (size_t N = 0 ; N < lengthof(m_children) ; ++N)
	    {
	    uint64_t children = m_children[N].load() ;
	    for (size_t i = 0 ; children ; i++)
	       {
	       if ((children & 1) != 0)
		  {
		  unsigned index = N * M_CHILDREN_BITS + i ;
		  if (alternative->contains(index))
		     {
		     PackedSimpleTrieNode *childnode = trie->getFullNode(child) ;
		     info->key[keylen] = (uint8_t)index ;
		     count += childnode->enumerateMatches(info,keylen+1) ;
		     if (count > info->max_matches)
			return count ;
		     }
		  child++ ;
		  }
	       children >>= 1 ;
	       }
	    }
	 }
      return count ;
      }
   else
      {
      // the current key byte must match
      uint8_t N = info->key[keylen] ;
      uint64_t mask = (1U << (N % M_CHILDREN_BITS)) ;
      uint64_t children = m_children[N / M_CHILDREN_BITS].load() ;
      if ((children & mask) != 0)
	 {
	 children &= (mask-1) ;
	 auto childnode = trie->node(firstChild() + m_popcounts[N % M_CHILDREN_BITS] + popcount(children)) ;
	 return childnode->enumerateMatches(info,keylen+1) ;
	 }
      return 0 ;
      }
}

/************************************************************************/
/*	Methods for class PackedTrie					*/
/************************************************************************/

LangIDPackedTrie::LangIDPackedTrie(const NybbleTrie* trie, uint32_t min_freq, bool show_conversion)
{
   init() ;
   if (trie)
      {
      m_size = trie->numFullByteNodes(min_freq) ;
      m_numterminals = trie->numTerminalNodes(min_freq) ;
      m_nodes.allocate(m_size) ;
      m_terminals.allocate(m_numterminals) ;
      if (m_nodes && m_terminals)
	 {
	 auto proot = &m_nodes[PTRIE_ROOT_INDEX] ;
	 new (proot) PackedSimpleTrieNode ;
	 m_used = 1 ;
	 if (!insertChildren(proot,trie,PTRIE_ROOT_INDEX,0,min_freq))
	    {
	    m_size = 0 ;
	    m_numterminals = 0 ;
	    }
	 if (show_conversion)
	    {
	    cout << "   converted " << m_used << " full nodes and "
		 << m_termused << " terminals" << endl ;
	    }
	 m_size = m_used ;
	 m_numterminals = m_termused ;
	 }
      else
	 {
	 m_nodes = nullptr ; 
	 m_terminals = nullptr ;
	 m_size = 0 ;
	 m_numterminals = 0 ;
	 }
      }
   return ;
}

//----------------------------------------------------------------------

LangIDPackedTrie::LangIDPackedTrie(CFile& f, const char *filename)
{
   init() ;
   if (f && parseHeader(f))
      {
      size_t offset = f.tell() ;
      m_fmap.open(filename) ;
      if (m_fmap)
	 {
	 // we can memory-map the file, so just point our member variables
	 //   at the mapped data
	 m_nodes = (PackedSimpleTrieNode*)(**m_fmap + offset) ;
	 m_terminals = (PackedTrieTerminalNode*)(m_nodes + m_size) ;
	 }
      else
	 {
	 // unable to memory-map the file, so read its contents into buffers
	 //   and point our variables at the buffers
	 m_nodes.allocate(m_size) ;
	 m_terminals.allocate(m_numterminals) ;
	 if (!m_nodes || !m_terminals ||
	    f.read(m_nodes.begin(),m_size,sizeof(PackedSimpleTrieNode)) != m_size ||
	    f.read(m_terminals.begin(),m_numterminals,sizeof(PackedTrieTerminalNode)) != m_numterminals)
	    {
	    m_nodes = nullptr ;
	    m_terminals = nullptr ;
	    m_size = 0 ; 
	    m_numterminals = 0 ;
	    }
	 }
      }
   return ;
}

//----------------------------------------------------------------------

LangIDPackedTrie::~LangIDPackedTrie()
{
   if (m_fmap)
      {
      m_nodes.release() ;
      m_terminals.release() ;
      }
   else
      {
      m_nodes = nullptr ;
      m_terminals = nullptr ;
      }
   init() ;				// clear all of the fields
   return ;
}

//----------------------------------------------------------------------

void LangIDPackedTrie::init()
{
   m_size = 0 ;
   m_used = 0 ;
   m_numterminals = 0 ;
   m_termused = 0 ;
   m_maxkeylen = 0 ;
   return ;
}

//----------------------------------------------------------------------

uint32_t LangIDPackedTrie::allocateChildNodes(unsigned numchildren)
{
   uint32_t index = m_used ;
   m_used += numchildren ;
   if (m_used > m_size)
      {
      m_used = m_size ;
cerr<<"out of full nodes"<<endl;
      return NOCHILD_INDEX ;		// error!  should never happen!
      }
   // initialize each of the new children
   for (size_t i = 0 ; i < numchildren ; i++)
      {
      new (m_nodes.at(index + i)) PackedSimpleTrieNode ;
      }
   return index ;
}

//----------------------------------------------------------------------

uint32_t LangIDPackedTrie::allocateTerminalNodes(unsigned numchildren)
{
   uint32_t index = m_termused ;
   m_termused += numchildren ;
   if (m_termused > m_numterminals)
      {
      m_termused = m_numterminals ;
cerr<<"out of terminal nodes"<<endl;
      return NOCHILD_INDEX ;		// error!  should never happen!
      }
   // initialize each of the new children
   for (size_t i = 0 ; i < numchildren ; i++)
      {
      m_terminals.at(index + i)->reinit() ;
      }
   return (index | PTRIE_TERMINAL_MASK) ;
}

//----------------------------------------------------------------------

bool LangIDPackedTrie::insertTerminals(PackedSimpleTrieNode *parent,
				 const NybbleTrie *trie, uint32_t mnode_index,
				 unsigned keylen, uint32_t min_freq)
{
   if (!parent || !trie)
      return false ;
   unsigned numchildren = trie->numExtensions(mnode_index,min_freq) ;
   if (numchildren == 0)
      return true ;
   keylen++ ;
   if (keylen > longestKey())
      m_maxkeylen = keylen ;
   uint32_t firstchild = allocateTerminalNodes(numchildren) ;
   parent->setFirstChild(firstchild) ;
   if (firstchild == NOCHILD_INDEX)
      {
      cerr << "insertTerminals: firstchild==NOCHILD_INDEX"<<endl;
      return false ;
      }
   unsigned index = 0 ;
   for (unsigned i = 0 ; i < PTRIE_CHILDREN_PER_NODE ; i++)
      {
      uint32_t nodeindex = mnode_index ;
      if (trie->extendKey(nodeindex,(uint8_t)i))
	 {
	 const NybbleTrieNode *mchild = trie->node(nodeindex) ;
	 uint32_t mfreq = mchild->frequency() ;
	 if (mfreq >= min_freq)
	    {
	    // set the appropriate bit in the child array
	    parent->setChild(i) ;
	    // add frequency info to the child node
	    PackedSimpleTrieNode *pchild = node(firstchild + index) ;
	    index++ ;
	    pchild->setFrequency(mfreq) ;
	    }
	 }
      }
   return true ;
}

//----------------------------------------------------------------------

bool LangIDPackedTrie::insertChildren(PackedSimpleTrieNode *parent,
				const NybbleTrie *trie, uint32_t node_index,
				unsigned keylen, uint32_t min_freq)
{
   if (!parent || !trie)
      return false ;
   // first pass: fill in all the children
   unsigned numchildren = trie->numExtensions(node_index,min_freq) ;
   if (numchildren == 0)
      return true ;
   keylen++ ;
   if (keylen > longestKey())
      m_maxkeylen = keylen ;
   bool terminal = trie->allChildrenAreTerminals(node_index,min_freq) ;
   uint32_t firstchild = (terminal
			  ? allocateTerminalNodes(numchildren)
			  : allocateChildNodes(numchildren)) ;
   parent->setFirstChild(firstchild) ;
   if (firstchild == NOCHILD_INDEX)
      {
      cerr << "insertChildren: firstchild==NOCHILD_INDEX" << endl ;
      return false ;
      }
   unsigned index = 0 ;
   for (unsigned i = 0 ; i < PTRIE_CHILDREN_PER_NODE ; i++)
      {
      uint32_t nodeindex = node_index ;
      if (trie->extendKey(nodeindex,(uint8_t)i))
	 {
	 const NybbleTrieNode *mchild = trie->node(nodeindex) ;
	 uint32_t mfreq = mchild->frequency() ;
	 if (mfreq >= min_freq)
	    {
	    // set the appropriate bit in the child array
	    parent->setChild(i) ;
	    // add frequency info to the child node
	    PackedSimpleTrieNode *pchild = this->node(firstchild + index) ;
	    index++ ;
	    pchild->setFrequency(mfreq) ;
	    if (terminal)
	       {
	       if (!insertTerminals(pchild,trie,nodeindex,keylen,min_freq))
		  return false ;
	       }
	    else if (!insertChildren(pchild,trie,nodeindex,keylen,min_freq))
	       return false ;
	    }
	 }
      }
   parent->setPopCounts() ;
   return true ;
}

//----------------------------------------------------------------------

bool LangIDPackedTrie::parseHeader(CFile& f)
{
   const size_t siglen = sizeof(PACKEDTRIE_SIGNATURE) ;
   char signature[siglen] ;
   if (f.read(signature,siglen,sizeof(char)) != siglen ||
       memcmp(signature,PACKEDTRIE_SIGNATURE,siglen) != 0)
      {
      // error: wrong file type
      return false ;
      }
   unsigned char version ;
   if (!f.readValue(&version)
       || version < PACKEDTRIE_FORMAT_MIN_VERSION
       || version > PACKEDTRIE_FORMAT_VERSION)
      {
      // error: wrong version of data file
      return false ;
      }
   unsigned char bits ;
   if (!f.readValue(&bits) || bits != PTRIE_BITS_PER_LEVEL)
      {
      // error: wrong type of trie
      return false ;
      }
   UInt32 val_size, val_keylen, val_numterm ;
   char padbuf[PACKEDTRIE_PADBYTES_1] ;
   if (!f.readValue(&val_size) ||
      !f.readValue(&val_keylen) ||
      !f.readValue(&val_numterm) ||
      f.read(padbuf,sizeof(padbuf),1) != sizeof(padbuf))
      {
      // error reading header
      return false ;
      }
   m_maxkeylen = val_keylen.load() ;
   m_size = val_size.load() ;
   m_numterminals = val_numterm.load() ;
   return true ;
}

//----------------------------------------------------------------------

PackedSimpleTrieNode* LangIDPackedTrie::findNode(const uint8_t *key, unsigned keylength) const
{
   uint32_t cur_index = PTRIE_ROOT_INDEX ;
   while (keylength > 0)
      {
      if (!extendKey(cur_index,*key))
	 return nullptr ;
      key++ ;
      keylength-- ;
      }
   return node(cur_index) ;
}

//----------------------------------------------------------------------

uint32_t LangIDPackedTrie::find(const uint8_t *key, unsigned keylength) const
{
   uint32_t cur_index = PTRIE_ROOT_INDEX ;
   while (keylength > 0)
      {
      if (!extendKey(cur_index,*key))
	 return 0 ;
      key++ ;
      keylength-- ;
      }
   PackedSimpleTrieNode *n = node(cur_index) ;
   return n ? n->frequency() : 0 ;
}

//----------------------------------------------------------------------

bool LangIDPackedTrie::extendKey(uint32_t &nodeindex, uint8_t keybyte) const
{
   if ((nodeindex & PTRIE_TERMINAL_MASK) != 0)
      {
      nodeindex = LangIDPackedTrie::NULL_INDEX ;
      return false ;
      }
   PackedSimpleTrieNode *n = node(nodeindex) ;
   uint32_t index = n->childIndexIfPresent(keybyte) ;
   nodeindex = index ;
   return (index != LangIDPackedTrie::NULL_INDEX) ;
}

//----------------------------------------------------------------------

unsigned LangIDPackedTrie::countMatches(const uint8_t *key, unsigned keylen,
				  const WildcardSet **alternatives,
				  unsigned max_matches,
				  bool nonterminals_only) const
{
   if (key && keylen > 0 && alternatives && m_nodes && m_nodes[0].firstChild())
      return m_nodes[0].countMatches(this,key,keylen,alternatives,
				     max_matches,nonterminals_only) ;
   return 0 ;
}

//----------------------------------------------------------------------

bool LangIDPackedTrie::enumerate(uint8_t *keybuf, unsigned maxkeylength,
			   PackedSimpleTrieEnumFn *fn, void *user_data) const
{
   if (keybuf && fn && m_nodes && m_nodes[0].firstChild())
      {
      memset(keybuf,'\0',maxkeylength) ;
      return m_nodes[0].enumerateChildren(this,keybuf,maxkeylength*8,0,fn,
					  user_data) ;
      }
   return false ;
}

//----------------------------------------------------------------------

bool LangIDPackedTrie::enumerate(uint8_t *keybuf, unsigned keylength,
			   const WildcardSet **alternatives,
			   PackedSimpleTrieMatchFn *fn, void *user_data) const
{
   if (keybuf && keylength > 0 && fn && alternatives &&
       m_nodes && m_nodes[0].firstChild())
      {
      return m_nodes[0].enumerateMatches(this,keybuf,keylength,0,
					 alternatives,fn,user_data) ;
      }
   return false ;
}

//----------------------------------------------------------------------

unsigned LangIDPackedTrie::enumerate(uint8_t *keybuf, unsigned keylength,
			       const WildcardSet **alternatives,
			       PackedTrieMatch *matches,
			       unsigned max_matches,
			       bool require_extensible_match) const
{
   if (keybuf && keylength > 0 && matches && alternatives &&
       m_nodes && m_nodes[0].firstChild())
      {
      EnumerationInfo info(this,keybuf,keylength,max_matches,matches,alternatives,
			   require_extensible_match) ;
      return m_nodes[0].enumerateMatches(&info,0) ;
      }
   return 0 ;
}

//----------------------------------------------------------------------

Owned<LangIDPackedTrie> LangIDPackedTrie::load(CFile& f, const char* filename)
{
   if (!f)
      return nullptr ;
   // std::ref is needed to keep the templated Owned ctor from trying to copy-construct a CFile from f....
   Owned<LangIDPackedTrie> trie(std::ref(f),filename) ;
   if (!trie || !trie->good())
      {
      return nullptr ;
      }
   return trie ;
}

//----------------------------------------------------------------------

Owned<LangIDPackedTrie> LangIDPackedTrie::load(const char* filename)
{
   CInputFile fp(filename,CFile::binary) ;
   return load(fp,filename) ;
}

//----------------------------------------------------------------------

bool LangIDPackedTrie::writeHeader(CFile& f) const
{
   // write the signature string
   const size_t siglen = sizeof(PACKEDTRIE_SIGNATURE) ;
   if (f.write(PACKEDTRIE_SIGNATURE,siglen,sizeof(char)) != siglen)
      return false; 
   // follow with the format version number
   unsigned char version = PACKEDTRIE_FORMAT_VERSION ;
   unsigned char bits = PTRIE_BITS_PER_LEVEL ;
   if (!f.writeValue(version) || !f.writeValue(bits))
      return false ;
   // write out the size of the trie
   UInt32 val_used(size()), val_keylen(longestKey()), val_numterm(m_numterminals) ;
   if (!f.writeValue(val_used) ||
      !f.writeValue(val_keylen) ||
      !f.writeValue(val_numterm))
      return false ;
   // pad the header with NULs for the unused reserved portion of the header
   return f.putNulls(PACKEDTRIE_PADBYTES_1) ;
}

//----------------------------------------------------------------------

bool LangIDPackedTrie::write(CFile& f) const
{
   if (!f || !writeHeader(f))
      return false ;
   // write the actual trie nodes
   if (f.write(m_nodes,m_size,sizeof(PackedSimpleTrieNode)) != m_size)
      return false ;
   // write the terminals
   if (f.write(m_terminals,m_numterminals,sizeof(PackedTrieTerminalNode)) != m_numterminals)
      return false ;
   f.writeComplete() ;
   return true ;
}

//----------------------------------------------------------------------

bool LangIDPackedTrie::write(const char *filename) const
{
   COutputFile fp(filename,CFile::safe_rewrite) ;
   return this->write(fp) ? fp.close() : false ;
}

//----------------------------------------------------------------------

static bool dump_ngram(const uint8_t *key, unsigned keylen,
		       uint32_t frequency, void *user_data)
{
   CFile& f = *((CFile*)user_data) ;
   if (f && frequency != PackedSimpleTrieNode::INVALID_FREQ)
      {
      f.printf("   ") ;
      write_escaped_key(f,key,keylen) ;
      f.printf(" :: %lu\n",(unsigned long)frequency) ;
      }
   return true ;
}

//----------------------------------------------------------------------

bool LangIDPackedTrie::dump(CFile& f) const
{
   LocalAlloc<uint8_t,10000> keybuf(longestKey()) ;
   return keybuf ? enumerate(keybuf,longestKey(),dump_ngram,&f) : false ;
}

/************************************************************************/
/*	Additional methods for class NybbleTrie				*/
/************************************************************************/

#if 0
static bool add_ngram(const uint8_t *key, unsigned keylen,
		      uint32_t frequency, void *user_data)
{
   NybbleTrie *trie = (NybbleTrie*)user_data ;
   trie->insert(key,keylen,frequency,false) ;
   return true ;
}
#endif

//----------------------------------------------------------------------

#if 0
NybbleTrie::NybbleTrie(const class LangIDPackedTrie *ptrie)
{
   if (ptrie)
      {
      init(ptrie->size() * 3 / 2) ;
      LocalAlloc<uint8_t> keybuf(ptrie->longestKey()) ;
      if (keybuf)
	 {
	 ptrie->enumerate(keybuf,ptrie->longestKey(),add_ngram,this) ;
	 }
      }
   else
      init(1) ;
   return ;
}
#endif

// end of file ptrie.C //
