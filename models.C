/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  File: models.C - language-model manipulation			*/
/*  Version:  1.10beta				       			*/
/*  LastEdit: 2019-07-28						*/
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

#include <cfloat>
#include <cmath>
#include "dbuffer.h"
#include "models.h"
#include "wildcard.h"
#include "global.h"
#include "framepac/config.h"
#include "framepac/memory.h"
#include "framepac/texttransforms.h"

using namespace Fr ;

/************************************************************************/
/*	Manifest constants						*/
/************************************************************************/

// how many bytes at a time to score for language identification
#define SAMPLE_SIZE 1024

// how many alternative matches to use for computeScore()
#define MAX_AMBIG 6

// how many alternative joint n-grams to take for computeCenterScore()
#define MAX_CENTER_AMBIG 8

// how many alternatives for the first byte will we allow before giving up?
#define MAX_FIRST_AMBIG 2

// max allowed wildcard combos when scoring
#define MAX_SCORE_AMBIG (12*MAX_AMBIG)
#define MAX_CENTER_SCORE_AMBIG (30*MAX_CENTER_AMBIG)

// how many successful ngram ranks do we include in the scoring?  Short-
//   circuit the evaluation once we reach this amount
#define MAX_RANKS 1

// what is the shortest history we'll accept for predicting an unknown byte?
#define MIN_NGRAM_LOCAL 2
#define MIN_NGRAM_GLOBAL 2

#define LENGTH_FACTOR_CACHESIZE 128
#define HISTORY_FACTOR_CACHESIZE 8192

#define RECONST_DISCOUNT 1.5

// fall-back global location to search for language models
#define APPDIR "/usr/share/ziprec/"

/************************************************************************/
/*	Types for this module						*/
/************************************************************************/

struct LocationSpec
   {
   const char *formatstring ;
   unsigned var1, var2, var3, var4 ;
   } ;

//----------------------------------------------------------------------

class ScoringFactors
   {
   public:
      ScoringFactors() ;
      ~ScoringFactors() = default ;

      double ratioFactor(unsigned freq)
	 { return freq < HISTORY_FACTOR_CACHESIZE ? m_ratio[freq] : computeRatioFactor(freq) ; }
      double lengthFactor(unsigned freq)
	 { return freq < HISTORY_FACTOR_CACHESIZE ? m_length[freq] : computeLengthFactor(freq) ; }

   private:
      static double computeRatioFactor(unsigned hist)
	 { return (((1.0 + ::log(hist)) * ratio_weight_factor)/hist) ; }
      static double computeLengthFactor(unsigned len)
	 { return ::exp(len * length_weight_factor) ; }
   private:
      double m_ratio[HISTORY_FACTOR_CACHESIZE] ;
      double m_length[HISTORY_FACTOR_CACHESIZE] ;
      static constexpr double length_weight_factor { 0.4 } ;
      static constexpr double ratio_weight_factor { 0.4 } ;
   } ;

/************************************************************************/
/*	Global variables for this module				*/
/************************************************************************/

ReconstructionData reconstruction_data ;

static double center_match_factor_2 = 0.15 ;    // bidirectional models
static double center_match_factor_1 = 0.25 ;    // forward model only
static bool center_match_reverse = false ;

static double global_model_weight = 1.0 ;
static double local_model_weight = 0.05 ;

static ScoringFactors score_factors ;

unsigned max_score_ambig = MAX_SCORE_AMBIG ;
unsigned max_center_score_ambig = MAX_CENTER_SCORE_AMBIG ;

// specification of the locations in which to search for language models
static LocationSpec model_locations[] =
   {
      { "%s-%s.lang",		2, 3, 0, 0 },
      { "%s/%s-%s.lang", 	1, 2, 3, 0 },
      { "models/%s-%s.lang",	2, 3, 0, 0 },
      { APPDIR "%s-%s.lang",	2, 3, 0, 0 },
      { "%s-%s.lang",		2, 4, 0, 0 },
      { "%s/%s-%s.lang",	1, 2, 4, 0 },
      { "models/%s-%s.lang",	2, 4, 0, 0 },
      { APPDIR "%s-%s.lang",	2, 4, 0, 0 },
      { "%s.lang",		2, 0, 0, 0 },
      { "%s/%s.lang",		1, 2, 0, 0 },
      { "models/%s.lang",	2, 0, 0, 0 },
      { APPDIR "%s.lang",	2, 0, 0, 0 },
      { "null.lang",		0, 0, 0, 0 },
      { "%s/null.lang",		1, 0, 0, 0 },
      { "models/null.lang",	0, 0, 0, 0 },
      { APPDIR "null.lang",	0, 0, 0, 0 },
      { nullptr, 0, 0, 0, 0 } // sentinel to mark end of array
   } ;

/************************************************************************/
/*	Utility functions						*/
/************************************************************************/

/************************************************************************/
/*	Methods for class ScoringFactors				*/
/************************************************************************/

ScoringFactors::ScoringFactors()
{
   for (size_t i = 1 ; i < HISTORY_FACTOR_CACHESIZE ; i++)
      {
      m_ratio[i] = computeRatioFactor(i) ;
      }
   for (size_t i = 1 ; i < LENGTH_FACTOR_CACHESIZE ; i++)
      {
      m_length[i] = computeLengthFactor(i) ;
      }
   return ;
}

/************************************************************************/
/*	Methods for class ReconstructionData				*/
/************************************************************************/

void ReconstructionData::clear()
{
   m_word_freq = nullptr ;
   m_ngrams_forward = nullptr ;
   m_ngrams_reverse = nullptr ;
   m_ngram_counts = nullptr ;
   m_ngram_avgfreq = nullptr ;
   m_ngram_length = 0 ;
   m_current_model = nullptr ;
   return ;
}

//----------------------------------------------------------------------

bool ReconstructionData::load(const char* data_file)
{
   bool success = false ;
   if (data_file && *data_file)
      {
      if (m_current_model && m_ngrams_forward && m_ngrams_reverse &&
	  strcmp(m_current_model,data_file) == 0)
	 {
	 // no need to re-load the model, we already have it
	 return true ;
	 }
      CInputFile fp(data_file,CFile::binary) ;
      if (fp)
	 {
	 clear() ;
	 success = load(fp,data_file) ;
	 if (success)
	    {
	    m_current_model = dup_string(data_file) ;
	    }
	 }
      }
   return success ;
}

//----------------------------------------------------------------------

bool ReconstructionData::load(CFile& fp, const char *filename)
{
   PROGRESS("loading language model\n") ;
   // check for the proper file signature and version number
   if (fp.verifySignature(LANGMODEL_SIGNATURE) != LANGMODEL_FORMAT_VERSION)
      return false ;
   // skip the alignment padding
   if (!fp.skip(6))
      return false ;
   // read the offsets of the embedded models
   uint64_t offset_forward = fp.read64LE() ;
   uint64_t offset_reverse = fp.read64LE() ;
   uint64_t offset_counts = fp.read64LE() ;
   uint64_t offset_words = fp.read64LE() ;
   // load in the language models
   bool success = true ;
   if (offset_forward != 0)
      {
      fp.seek(offset_forward) ;
      m_ngrams_forward = LangIDPackedTrie::load(fp,filename) ;
      if (!m_ngrams_forward)
	 success = false ;
      }
   if (offset_reverse != 0)
      {
      fp.seek(offset_reverse) ;
      m_ngrams_reverse = LangIDPackedTrie::load(fp,filename) ;
      if (!m_ngrams_reverse)
	 success = false ;
      }
   if (offset_counts != 0)
      {
      fp.seek(offset_counts) ;
      loadCounts(fp) ;
      if (m_ngram_counts)
	 computeFrequencies() ;
      else
	 success = false ;
      }
   if (offset_words != 0)
      {
      fp.seek(offset_words) ;
      m_word_freq.reinit() ;
      if (m_word_freq)
	 {
	 auto frequencies = m_word_freq.get() ;
	 // read the number of words to expect
	 uint32_t count = fp.read32LE() ;
	 uint32_t total_tokens = 0 ;
	 for (size_t i = 0 ; i < count && !fp.eof() ; i++)
	    {
	    // read a word record: 64-bit frequency, 16-bit length, and then the word
	    size_t freq = fp.read64LE() ;
	    total_tokens += freq ;
	    unsigned wordlen = fp.read16LE() ;
	    if (wordlen > MAX_WORD)
	       {
	       fprintf(stderr,"Invalid data in language file: word length = %u\n",
		       wordlen) ;
	       success = false ;
	       break ;
	       }
	    uint8_t wordbuffer[MAX_WORD] ;
	    if (fp.read(wordbuffer,wordlen,sizeof(char)) < wordlen)
	       {
	       success = false ;
	       break ;
	       }
	    frequencies->insert(wordbuffer,wordlen,(uint32_t)freq,false) ;
	    }
	 frequencies->addTokenCount(total_tokens) ;
	 }
      }
   return success ;
}

//----------------------------------------------------------------------

void ReconstructionData::loadCounts(CFile& fp)
{
   m_ngram_length = fp.read32LE() ;
   m_ngram_counts.allocate(m_ngram_length+1) ;
   if (m_ngram_length > 0 && m_ngram_counts)
      {
      for (size_t i = 0 ; i <= m_ngram_length ; i++)
	 {
	 m_ngram_counts[i] = (size_t)fp.read64LE() ;
	 }
      }
   return ;
}

//----------------------------------------------------------------------

void ReconstructionData::computeFrequencies()
{
   m_ngram_avgfreq.allocate(m_ngram_length+1) ;
   if (!m_ngram_avgfreq)
      return ;
   m_ngram_avgfreq[0] = DBL_MAX ;
   for (size_t i = 1 ; i <= m_ngram_length ; i++)
      {
      if (m_ngram_counts[i] > 0)
	 m_ngram_avgfreq[i] = m_ngram_counts[0] / (double)m_ngram_counts[i] ;
      else
	 m_ngram_avgfreq[i] = m_ngram_counts[0] ;
      }
   return ;
}

/************************************************************************/
/************************************************************************/

// force 0 to 256 while leaving 1-256 unaltered:
//    (x-1)&0xFF is [255,0,...,255] for [0...256]
//    add one to get [256,1,...,256]
#define adjusted_setsize(x) ((((x)->setSize() - 1) & 0xFF) + 1)

//----------------------------------------------------------------------

static void find_longest_ambiguities(unsigned* ambiguities, size_t num_bytes, size_t min_length, size_t max_length,
				     const WildcardSet** allowed_wild, unsigned max_ambig)
{
   //assert(min_length > 0) ;
   //assert(max_ambig < ULONG_MAX/256) ;
   for (size_t i = 0 ; i + min_length <= num_bytes ; i++)
      {
      size_t ambig = 1 ;
      if (allowed_wild[i])
	 {
	 // limit the amount of ambiguity we allow on the first byte
	 //   to reduce n-gram lookup costs
	 ambig = adjusted_setsize(allowed_wild[i]) ;
	 if (ambig > MAX_FIRST_AMBIG)
	    {
	    ambiguities[i] = 0 ;
	    continue ;
	    }
	 }
      size_t len = 1 ;
      for ( ; len <= max_length && i + len < num_bytes ; len++)
	 {
	 const WildcardSet *wild = allowed_wild[i+len] ;
	 if (wild)
	    {
	    unsigned setsize = adjusted_setsize(wild) ;
	    ambig *= setsize ;
	    if (ambig > (len+1)*max_ambig)
	       break ;
	    }
	 }
      ambiguities[i] = len ;
      }
}

//----------------------------------------------------------------------

static void count_ambiguities(unsigned* ambiguities, size_t num_bytes, const WildcardSet** allowed_wild,
			      unsigned max_ambig)
{
   size_t ambig = 1 ;
   for (size_t i = num_bytes ; i > 0 ; i--)
      {
      size_t bytes = (num_bytes - i) + 1 ;
      if (allowed_wild[i-1])
	 {
	 unsigned setsize = adjusted_setsize(allowed_wild[i-1]) ;
	 ambig = (ambig >= ULONG_MAX / setsize) ? ULONG_MAX : ambig * setsize ;
	 // only start an n-gram at this position if the first byte is
	 //   relatively unambiguous
         ambiguities[i-1] = (setsize <= MAX_FIRST_AMBIG && ambig <= bytes * max_ambig)
	    ? ambig : 0 ; 
	 }
      else
	 {
	 // are there still few enough ambiguities to be worth a try?
	 ambiguities[i-1] = (ambig <= bytes * max_ambig) ? ambig : 0 ;
	 }
      }
   return ;
}

/************************************************************************/
/*	Methods for class BidirModel					*/
/************************************************************************/

BidirModel::BidirModel(const LangIDPackedTrie *gleft, const LangIDPackedTrie *gright)
{ 
   m_file_left = m_file_right = nullptr ;
   m_global_left = gleft ; m_global_right = gright ;
   setLengths() ;
   m_center_factor = ((m_global_right && center_match_reverse)
		      ? center_match_factor_2
		      : center_match_factor_1) ; 
   return ;
}

//----------------------------------------------------------------------

void BidirModel::setLengths()
{
   size_t longest = 0 ;
   if (m_file_left)
      longest = m_file_left->longestKey() ;
   if (m_global_left && m_global_left->longestKey() > longest)
      longest = m_global_left->longestKey() ;
   m_forward_len = longest ;
   longest = 0 ;
   if (m_file_right)
      longest = m_file_right->longestKey() ;
   if (m_global_right && m_global_right->longestKey() > longest)
      longest = m_global_right->longestKey() ;
   m_reverse_len = longest ;
   return ;
}

//----------------------------------------------------------------------

bool BidirModel::computeScore(const LangIDPackedTrie* trie, uint8_t* key, size_t num_bytes,
			      const WildcardSet** context_wildcards, ZRScore* scores, double weight)
{
   PackedTrieMatch matches[MAX_AMBIG] ;
   unsigned matchcount
      = trie->enumerate(key,num_bytes,context_wildcards,matches,MAX_AMBIG,true) ;
   if (matchcount == 0 || matchcount > MAX_AMBIG)
      return false ;
   size_t len = (num_bytes<LENGTH_FACTOR_CACHESIZE) ? num_bytes : LENGTH_FACTOR_CACHESIZE ;
   weight = weight * score_factors.lengthFactor(len) / matchcount ;
   for (size_t i = 0 ; i < matchcount ; i++)
      {
      auto node = matches[i].node() ;
      double ratio_factor = score_factors.ratioFactor(node->frequency()) ;
      node->addToScores(trie,scores,ratio_factor * weight) ;
      }
   return true ;
}

//----------------------------------------------------------------------

bool BidirModel::computeCenterScore(const LangIDPackedTrie *trie, uint8_t *key,
				    size_t num_bytes, size_t center_byte,
				    const WildcardSet **context_wildcards,
				    ZRScore *scores, double weight)
{
   PackedTrieMatch matches[MAX_CENTER_AMBIG] ;
   LocalAlloc<uint8_t,8192> keybuf(num_bytes * MAX_CENTER_AMBIG) ;
   if (!keybuf)
      return false ;
   for (size_t i = 0 ; i < lengthof(matches) ; i++)
      {
      matches[i].setKeyBuffer(keybuf + i*num_bytes, num_bytes) ;
      }
   unsigned matchcount
      = trie->enumerate(key,num_bytes,context_wildcards,matches,MAX_CENTER_AMBIG,false) ;
   if (matchcount == 0 || matchcount > MAX_CENTER_AMBIG)
      {
      return false ;
      }
   weight /= (matchcount * reconstruction_data.ngramAvgFreq(num_bytes)) ;
   weight *= num_bytes * num_bytes ;
   for (size_t i = 0 ; i < matchcount ; i++)
      {
      const uint8_t *key = matches[i].key() ;
      // extract the matched byte at the 'center' location and the frequency
      //   of the matched ngram
      uint32_t freq = matches[i].node()->frequency() ;
      scores[key[center_byte]] += (freq * weight) ;
      }
   return true ;
}

//----------------------------------------------------------------------

unsigned BidirModel::applyModel(const LangIDPackedTrie* model, double model_weight, uint8_t* key, bool reverse,
   unsigned max_bytes, size_t min_len, double weight, ZRScore* scores, unsigned* ambiguities,
   const WildcardSet** contexts, ContextFlags& context_flags)
   const
{
   unsigned good_contexts = 0 ;
   if (model)
      {
      size_t max = std::min(max_bytes+1,model->longestKey()) ;
      unsigned ranks = 0 ;
      for (size_t i = max ; i > min_len ; i--)
	 {
	 size_t ofs = max_bytes - (i - 1) ;
	 if (ambiguities[ofs] &&
	     computeScore(model,key + ofs,i-1, contexts + ofs,scores, i*weight*model_weight))
	    {
	    context_flags.setSide(reverse) ;
	    if (++ranks >= MAX_RANKS)
	       {
	       good_contexts++ ;
	       break ;
	       }
	    }
	 }
      }
   return good_contexts ;
}

//----------------------------------------------------------------------

bool BidirModel::computeScores(bool reverse, const DecodedByte* bytes, size_t max_bytes,
			       const WildcardCollection* context_wildcards, ZRScore* scores, double weight,
			       ContextFlags& context_flags) const
{
   if (max_bytes < MIN_NGRAM_LOCAL)
      return false ;
   LocalAlloc<uint8_t,512> key(max_bytes) ;
   LocalAlloc<const WildcardSet*,512> contexts(max_bytes) ;
   double discount_factor = (DBYTE_CONFIDENCE_LEVELS + 2) * RECONST_DISCOUNT ;
   for (size_t i = 0 ; i < max_bytes ; i++)
      {
      size_t pos = reverse ? max_bytes - i : i ;
      if (bytes[pos].isDiscontinuity())
	 {
	 max_bytes = i ;
	 break ;
	 }
      key[i] = bytes[pos].byteValue() ;
      if (bytes[pos].isReconstructed())
	 weight *= (bytes[pos].confidence() / discount_factor) ;
      contexts[i] = bytes[pos].isLiteral() ? nullptr : context_wildcards->set(bytes[pos].originalLocation()) ;
      }
   LocalAlloc<unsigned> ambiguities(max_bytes) ;
   count_ambiguities(ambiguities,max_bytes,contexts,max_score_ambig) ;
   const LangIDPackedTrie *file_model = reverse ? fileReverseModel() : fileForwardModel() ;
   unsigned good_contexts
      = applyModel(file_model,local_model_weight,key,reverse,max_bytes,MIN_NGRAM_LOCAL,weight,scores,ambiguities,
	 contexts,context_flags) ;
   auto global_model = reverse ? globalReverseModel() : globalForwardModel() ;
   good_contexts += applyModel(global_model,global_model_weight,key,reverse,max_bytes,MIN_NGRAM_GLOBAL,weight,
      scores,ambiguities,contexts,context_flags) ;
   return (good_contexts > 0) ;
}

//----------------------------------------------------------------------

bool BidirModel::computeCenterScores(const DecodedByte *bytes,
				     size_t left_size, size_t right_size,
				     const WildcardCollection *context_wildcards,
				     ZRScore *scores, double weight) const
{
   size_t max_len = longestForwardNgram() ;
//   if (max_len > reconstruction_data.ngramLength())
//      max_len = reconstruction_data.ngramLength() ;
   if (max_len < 2)
      return false ;
   // collect the wildcard contexts
   int start_offset = -left_size ;
   if (start_offset < (int)(-max_len) + 2)
      start_offset = -max_len + 2 ;
   int end_offset = right_size ;
   if (end_offset > (int)max_len - 2)
      end_offset = max_len - 2 ;
   int byte_count = end_offset - start_offset + 1 ;
   LocalAlloc<uint8_t> key(2*byte_count) ;
   LocalAlloc<const WildcardSet*> contexts(2*byte_count) ;
   LocalAlloc<unsigned> ambiguities(2*byte_count) ;
   uint8_t *key_rev = key + byte_count ;
   const WildcardSet **contexts_rev = contexts + byte_count ;
   unsigned *ambiguities_rev = ambiguities + byte_count ;
   for (int i = start_offset ; i <= end_offset ; i++)
      {
      const WildcardSet *context = nullptr ;
      if (bytes[i].isReference())
	 context = context_wildcards->set(bytes[i].originalLocation()) ;
      contexts_rev[end_offset - i] = contexts[i-start_offset] = context ;
      key_rev[end_offset - i] = key[i-start_offset] = bytes[i].byteValue() ;
      }
   find_longest_ambiguities(ambiguities,byte_count,3,max_len,contexts, max_center_score_ambig) ;
   find_longest_ambiguities(ambiguities_rev,byte_count,3,max_len,contexts_rev, max_center_score_ambig) ;
   size_t good_contexts = 0 ;
   // enumerate the possible spans, from maximal n-grams down to
   //   trigrams, which contain the wildcard we're scoring
   weight *= centerMatchFactor() ;
   int shift = end_offset + start_offset ;
   for (int i = max_len ; i >= 3 ; i--)
      {
      int first_shift = 2 - i ;
      if (first_shift < start_offset)
	 first_shift = start_offset ;
      int last_shift = -1 ;
      if (last_shift + i - 1 > end_offset)
	 last_shift = end_offset - i + 1 ;
      for (int start = first_shift ; start <= last_shift ; start++)
	 {
	 // compute the score for the current span
	 int ofs = start - start_offset ;
	 if (ambiguities[ofs] >= (unsigned)i)
	    {
#if 0 // we don't currently have file-model ngram counts
	    if (fileForwardModel() &&
		computeCenterScore(fileForwardModel(),key+ofs,i,-start,
				   contexts+ofs,scores,weight))
	       {
	       good_contexts++ ;
	       }
#endif
	    if (computeCenterScore(globalForwardModel(),key+ofs,i,-start,
				   contexts+ofs,scores,weight))
	       good_contexts++ ;
	    }
	 if (center_match_reverse &&
	     ambiguities_rev[ofs+shift] >= (unsigned)i &&
	     ofs >= -shift)
	    {
	    if (globalReverseModel() &&
		computeCenterScore(globalReverseModel(), key_rev+ofs+shift,
				   i,shift-start,contexts_rev+ofs+shift,
				   scores,weight))
	       {
	       good_contexts++ ;
	       }
#if 0 // we don't currently have file-model ngram counts
	    if (fileReverseModel() &&
		computeCenterScore(fileReverseModel(), key_rev+ofs+shift,
				   i,shift-start,contexts_rev+ofs+shift,
				   scores,weight))
	       {
	       good_contexts++ ;
	       }
#endif
	    }
	 }
      if (good_contexts > 0)
	 break ;
      }
   return (good_contexts > 0) ;
}

/************************************************************************/
/************************************************************************/

//----------------------------------------------------------------------

static unsigned most_frequent_language(DecodeBuffer& decode_buffer, const LanguageIdentifier* langid,
				       unsigned samples)
{
   // ensure that we have enough data for a proper determination
   size_t filesize = decode_buffer.totalBytes() ;
   if (filesize >= 10 * SAMPLE_SIZE)
      {
      size_t start_offset = 7 * filesize / 8 ;
      size_t step = (filesize - start_offset - SAMPLE_SIZE) / samples ;
      if (step == 0)
	 step = 1 ;
      Owned<LanguageScores> top_scores(langid->numLanguages());
      char decoded[SAMPLE_SIZE] ;
      bool literals[SAMPLE_SIZE] ;
      for (size_t i = 0 ; i < samples ; i++)
	 {
	 size_t offset = start_offset + (i * step) ; 
	 decode_buffer.convert(offset,SAMPLE_SIZE,'\0',decoded,literals) ;
	 // because UTF-16 encoding of ASCII-only or Latin1-only data
	 //   tends to leave all the null bytes as unknown, which breaks
	 //   the scoring of known bytes only, also identify based on
	 //   the recovered data with all unknown bytes changed to NULs
	 Owned<LanguageScores> scores { langid->identify(decoded,SAMPLE_SIZE) } ;
	 if (scores)
	    scores->scaleScores(0.5) ; // give lower weight to replaced unknowns
	 else
	    scores.reinit(langid->numLanguages()) ;
	 size_t start = 0 ;
	 for ( ; ; )
	    {
	    while (start < SAMPLE_SIZE && !literals[start])
	       start++ ;
	    if (start >= SAMPLE_SIZE)
	       break ;
	    size_t end = start + 1 ;
	    while (end < SAMPLE_SIZE && literals[end])
	       end++ ;
	    if (end - start > 2)
	       langid->identify(scores,decoded+start,end-start,langid->alignments(),false,true,SAMPLE_SIZE) ;
	    start = end ;
	    }
	 top_scores->addThresholded(scores,scores->highestScore()*0.8) ;
	 }
      unsigned top_lang = top_scores->highestLangID() ;
      return top_lang ;
      }
   return ~0 ;
}

//----------------------------------------------------------------------

static const char *select_var(unsigned which,
			      const char *arg1, const char *arg2,
			      const char *arg3, const char *arg4)
{
   switch (which)
      {
      case 1:
	 return arg1 ;
      case 2:
	 return arg2 ;
      case 3:
	 return arg3 ;
      case 4:
	 return arg4 ;
      default:
	 return nullptr ;
      }
}

//----------------------------------------------------------------------

static char *try_loading(const LocationSpec *locations,
			 const char *dblocation,
			 const char *langname,
			 const char *encoding,
			 const char *alt_encoding)
{
   CharPtr filename ;
   while (locations && locations->formatstring)
      {
      const char *arg1 = select_var(locations->var1,dblocation,langname,
				    encoding,alt_encoding) ;
      const char *arg2 = select_var(locations->var2,dblocation,langname,
				    encoding,alt_encoding) ;
      const char *arg3 = select_var(locations->var3,dblocation,langname,
				    encoding,alt_encoding) ;
      const char *arg4 = select_var(locations->var4,dblocation,langname,
				    encoding,alt_encoding) ;
      filename = aprintf(locations->formatstring,arg1,arg2,arg3,arg4) ;
      if (reconstruction_data.load(filename))
	 break ;
      else
	 {
	 filename = nullptr ;
	 }
      }
   return filename.move() ;
}

//----------------------------------------------------------------------

bool load_reconstruction_data_by_lang(DecodeBuffer &decode_buffer, const LanguageIdentifier *langid,
				      const char *&encoding)
{
   // score N sections of the file, and report on the most frequent one
   unsigned langnum = most_frequent_language(decode_buffer,langid,10) ;
   const char *langname = langid->languageName(langnum) ;
   if (langname)
      {
      encoding = langid->languageEncoding(langnum) ;
      if (verbosity > 0)
	 {
	 cerr << "detected language '" << langname << "' in " << encoding << endl ;
	 }
      const char *alt_enc = strcmp(encoding,"ASCII") == 0 ? "utf8" : encoding ;
      CharPtr langdata = try_loading(model_locations, langid->databaseLocation(), langname, encoding, alt_enc) ;
      bool success = false ;
      if (langdata)
	 {
	 success = true ;
	 cerr << "; loaded language data from " << langdata << endl ;
	 }
      return success ;
      }
   return true ;
}

//----------------------------------------------------------------------

unsigned set_max_score_ambig(unsigned new_max)
{
   unsigned old_max = max_score_ambig ;
   max_score_ambig = new_max ;
   return old_max ;
}

//----------------------------------------------------------------------

void thorough_search(bool thoro)
{
   if (thoro)
      {
      max_score_ambig = MAX_SCORE_AMBIG * 40 ;
      max_center_score_ambig = MAX_CENTER_SCORE_AMBIG * 40 ;
      }
   else
      {
      max_score_ambig = MAX_SCORE_AMBIG ;
      max_center_score_ambig = MAX_CENTER_SCORE_AMBIG ;
      }
   return ;
}

// end of file models.C //
