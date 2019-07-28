/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  File: models.h - language-model manipulation			*/
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

#include "dbyte.h"
#include "dbuffer.h"  // for ContextFlags
#include "pstrie.h"
#include "wildcard.h"
#include "whatlang2/langid.h"

/************************************************************************/
/*	Types								*/
/************************************************************************/

//#ifdef BULK_EXTRACTOR
typedef float ZRScore ; // save memory
//#else
//typedef double ZRScore ;
//#endif

//----------------------------------------------------------------------

class ReconstructionData
   {
   public:
      ReconstructionData() = default ;
      ~ReconstructionData() = default ;

      // accessors
      const char* currentModel() const { return m_current_model ; }
      const size_t* ngramCounts() const { return m_ngram_counts ; }
      const double* ngramAvgFreq() const { return m_ngram_avgfreq ; }
      double ngramAvgFreq(unsigned len) const { return m_ngram_avgfreq[len] ; }
      size_t ngramLength() const { return m_ngram_length ; }
      NybbleTrie* wordFreq() { return &m_word_freq ; }
      LangIDPackedTrie* ngramsForward() { return &m_ngrams_forward ; }
      LangIDPackedTrie* ngramsReverse() { return &m_ngrams_reverse ; }

      // manipulators
      void clear() ;
      bool load(const char* filename) ;

   private:
      bool load(Fr::CFile& fp, const char* filename) ;
      void loadCounts(Fr::CFile& fp) ;
      void computeFrequencies() ;

   private:
      Fr::CharPtr    m_current_model ;
      Fr::SizeTPtr   m_ngram_counts ;
      Fr::DoublePtr  m_ngram_avgfreq ;
      size_t     m_ngram_length { 0 } ;
      Fr::Owned<NybbleTrie> m_word_freq { nullptr } ;
      Fr::Owned<LangIDPackedTrie> m_ngrams_forward { nullptr } ;
      Fr::Owned<LangIDPackedTrie> m_ngrams_reverse { nullptr } ;
   } ;

//----------------------------------------------------------------------

class BidirModel
   {
   public:
      BidirModel(const LangIDPackedTrie *gleft, const LangIDPackedTrie *gright) ;
      ~BidirModel() = default ;

      // modifiers
      void setFileModels(LangIDPackedTrie *left, LangIDPackedTrie *right)
	 { m_file_left = left ; m_file_right = right ; setLengths() ; }
      void deleteFileModels()
	 { delete m_file_left ; m_file_left = nullptr ;
	   delete m_file_right ; m_file_right = nullptr ;
	   setLengths() ; }

      // accessors
      const LangIDPackedTrie *fileForwardModel() const { return m_file_left ; }
      const LangIDPackedTrie *fileReverseModel() const { return m_file_right ; }
      const LangIDPackedTrie *globalForwardModel() const { return m_global_left ; }
      const LangIDPackedTrie *globalReverseModel() const { return m_global_right ; }
      size_t longestForwardNgram() const { return m_forward_len ; }
      size_t longestReverseNgram() const { return m_reverse_len ; }
      double centerMatchFactor() const { return m_center_factor ; }

      bool computeScores(bool reverse, const DecodedByte *bytes, size_t max_bytes,
			 const WildcardCollection *context_wildcards,
			 ZRScore *scores, double weight,
			 ContextFlags &context_flags)
	 const ;
      bool computeCenterScores(const DecodedByte *bytes, size_t left_size, size_t right_size,
			       const WildcardCollection *context_wildcards,
			       ZRScore *scores, double weight) const ;

   protected:
      void setLengths() ;
      static bool computeScore(const LangIDPackedTrie *trie, uint8_t *key, size_t num_bytes,
			       const WildcardSet **context_wildcards,
			       ZRScore *scores, double weight) ;
      static bool computeCenterScore(const LangIDPackedTrie *trie, uint8_t *key, size_t num_bytes,
				     size_t center_byte, const WildcardSet **context_wildcards,
				     ZRScore *scores, double weight) ;
      unsigned applyModel(const LangIDPackedTrie* model, double model_weight, uint8_t* key, bool reverse,
	 		  unsigned max_bytes, size_t min_len, double weight, ZRScore* scores, unsigned* ambiguities,
	 		  const WildcardSet** contexts, ContextFlags& context_flags) const ;
   public:
      LangIDPackedTrie	*m_file_left ;
      LangIDPackedTrie	*m_file_right ;
      const LangIDPackedTrie	*m_global_left ;
      const LangIDPackedTrie	*m_global_right ;
      double		 m_center_factor ;
      size_t		 m_forward_len ;
      size_t		 m_reverse_len ;
   } ;

/************************************************************************/
/*	Global Variables						*/
/************************************************************************/

extern ReconstructionData reconstruction_data ;

/************************************************************************/
/************************************************************************/

void thorough_search(bool thoro = true) ;
unsigned set_max_score_ambig(unsigned new_max) ;

bool load_reconstruction_data_by_lang(class DecodeBuffer &decode_buffer,
				      const LanguageIdentifier *langid,
				      const char *&encoding) ;

// end of file models.h //
