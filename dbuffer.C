/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  File: dbuffer.C - file-wise buffer for bytes/back-references	*/
/*  Version:  1.10beta				       			*/
/*  LastEdit: 27jun2019							*/
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

#include <cmath>
#include "byteio.h"
#include "dbuffer.h"
#include "inflate.h"
#include "global.h"
#include "framepac/config.h"
#include "framepac/file.h"
#include "framepac/texttransforms.h"
#include "framepac/timer.h"

using namespace Fr ;

/************************************************************************/
/*	Manifest Constants for this module				*/
/************************************************************************/

// minimum number of reconstructed back-references which need to match
//   the literals prior to a discontinuity to accept a possible overlap
#define MIN_DISC_OVERLAP 12

/************************************************************************/
/*	Globals variables for this module				*/
/************************************************************************/

/************************************************************************/
/*	Methods for class WildcardCounts				*/
/************************************************************************/

WildcardCounts::WildcardCounts(unsigned size)
{
   m_counts = New<uint32_t>(size) ;
   m_numcounts = m_counts ? size : 0 ;
   clear() ;
   return ;
}

//----------------------------------------------------------------------

WildcardCounts::~WildcardCounts()
{
   Free(m_counts) ;
   m_counts = 0 ;
   m_numcounts = 0 ;
   return ;
}

//----------------------------------------------------------------------

void WildcardCounts::clear()
{ 
   for (size_t i = 0 ; i < numCounts() ; i++)
      m_counts[i] = 0 ;
   m_prevhighest = 0 ;
   m_known_highest = false ;
   return ;
}

//----------------------------------------------------------------------

unsigned WildcardCounts::highestUsed() const
{
   if (!m_known_highest)
      {
      for (size_t i = numCounts() ; i > m_prevhighest ; i--)
	 {
	 if (m_counts[i-1])
	    {
	    m_prevhighest = i - 1 ;
	    return (i - 1) ;
	    }
	 }
      }
   return m_prevhighest ;
}

//----------------------------------------------------------------------

void WildcardCounts::setHighestUsed()
{
   m_prevhighest = highestUsed() ;
   m_known_highest = true ;
   return ;
}

//----------------------------------------------------------------------

bool WildcardCounts::expandTo(unsigned new_size)
{
   if (new_size <= numCounts())
      return true ;
   uint32_t *new_counts = NewR<uint32_t>(m_counts,new_size) ;
   if (new_counts)
      {
      m_counts = new_counts ;
      for (size_t i = numCounts() ; i < new_size ; i++)
	 {
	 m_counts[i] = 0 ;
	 }
      m_numcounts = new_size ;
      return true ;
      }
   else
      return false ;
}

/************************************************************************/
/*	Methods for class DecodeBuffer					*/
/************************************************************************/

DecodeBuffer::DecodeBuffer(FILE *fp, WriteFormat format,
			   unsigned char unknown_char,
			   const char *friendly_filename,
			   bool deflate64, bool test_mode)
{
   m_refwindow = deflate64 ? REFERENCE_WINDOW_DEFLATE64 : REFERENCE_WINDOW_DEFLATE ;
   m_deflate64 = deflate64 ;
   m_buffer = new DecodedByte[referenceWindow()] ;
   m_filebuffer = 0 ;
   m_context_flags = 0 ;
   m_backingfile = nullptr ;
   // Note: we need to be able to deal with multiple ref-windows worth of
   //   replacements, but we won't know how many until later!
   clearReferenceWindow(true) ;
   setReplacements(0,0) ;
   m_infp = 0 ;
   m_outfp = 0 ;
   m_numbytes = 0 ;
   m_loadedbytes = 0 ;
   m_wildcardcounts = 0 ;
   setOutputFile(fp,format,unknown_char,friendly_filename,0,test_mode) ;
   rewind() ;
   return ;
}

//----------------------------------------------------------------------

DecodeBuffer::~DecodeBuffer()
{
   finalize() ;
   delete m_wildcardcounts ; m_wildcardcounts = 0 ;
   delete [] m_replacements ;   m_replacements = 0 ;
   delete [] m_buffer ; m_buffer = 0 ;
   m_refwindow = 0 ;
   m_numreplacements = 0 ;
   return ;
}

//----------------------------------------------------------------------

unsigned char DecodeBuffer::setUnknownChar(unsigned char unk)
{
   unsigned char prev_unk = unknownChar() ;
   m_unknown = unk ; 
   return prev_unk ;
}

//----------------------------------------------------------------------

void DecodeBuffer::clearReferenceWindow(bool init)
{
   if (init)
      m_discontinuities = 0 ;
   else
      {
      m_discontinuities++ ;
      size_t repl_count = (m_discontinuities + 1) * referenceWindow() ;
      expandReplacements(repl_count - numReplacements()) ;
      }
   rewindReferenceWindow() ;
   return ;
}

//----------------------------------------------------------------------

void DecodeBuffer::rewindReferenceWindow()
{
   size_t loc = (m_discontinuities + 1) * referenceWindow() ;
   for (size_t i = 0 ; i < referenceWindow() ; i++)
      {
      m_buffer[i].setOriginalLocation(loc-i) ;
      }
   rewind() ;
   return ;
}

//----------------------------------------------------------------------

static void compute_byte_weights(double *byte_weights,
				 const DecodedByte *bytes,
				 unsigned num_bytes)
{
   for (size_t i = 0 ; i < 256 ; i++)
      byte_weights[i] = 0 ;
   // weight by number of occurrences
   unsigned total_count = 0 ;
   for (size_t i = 0 ; i < num_bytes ; i++)
      {
      if (bytes[i].isLiteral())
	 {
	 byte_weights[bytes[i].byteValue()]++ ;
	 total_count++ ;
	 }
      }
   if (total_count > 0)
      {
      double avg_count = total_count / 256 ;
      for (size_t i = 0 ; i < 256 ; i++)
	 {
	 if (byte_weights[i])
	    byte_weights[i] = avg_count / byte_weights[i] ;
	 else
	    byte_weights[i] = 1.0 ;
	 }
      }
   return ;
}

//----------------------------------------------------------------------

static double score_alignment(const DecodedByte *bytes,
			      const DecodedByte *replacements,
			      unsigned num_bytes, unsigned offset,
			      const double *byte_weights,
			      unsigned &count, unsigned &correct)
{
   double score = 0.0 ;
   count = 0 ;
   correct = 0 ;
   size_t limit = num_bytes + offset ;
   for (size_t i = offset ; i < num_bytes ; i++)
      {
      const DecodedByte &db1 = bytes[i] ;
      const DecodedByte &db2 = replacements[limit-i] ;
      if (db2.isLiteral() && db1.isLiteral())
	 {
	 count++ ;
	 double weight = db1.confidence() * db2.confidence() ;
	 if (db1.byteValue() == db2.byteValue())
	    {
	    score += weight * byte_weights[db1.byteValue()] ;
	    correct++ ;
	    }
	 else
	    score -= weight * byte_weights[db1.byteValue()] ;
	 }
      }
   return score / (DBYTE_CONFIDENCE_LEVELS * DBYTE_CONFIDENCE_LEVELS) ;
}

//----------------------------------------------------------------------

bool DecodeBuffer::alignDiscontinuity(unsigned which,
				      unsigned corruptionsize,
				      double compression_ratio)
{
   if (which <= m_discontinuities)
      {
      unsigned max_repl = highestReplacement(which,referenceWindow()) ;
      max_repl %= referenceWindow() ;
      // scan the buffer for the desired discontinuity
      const DecodedByte *file_buffer = &m_filebuffer[firstRealByte()] ;
      const DecodedByte *buffer_end = &m_filebuffer[loadedBytes()] ;
      unsigned disc = 0 ;
      for ( ; file_buffer < buffer_end ; file_buffer++)
	 {
	 if (file_buffer->isDiscontinuity() && disc++ >= which)
	    break ;
	 }
      // did we find the requested discontinuity?
      if (file_buffer >= buffer_end)
	 return false ;
      size_t discont_loc = file_buffer - m_filebuffer ;
      // check boundaries for consistency
      if (discont_loc > max_repl)
	 max_repl = file_buffer - m_filebuffer ;
      if (max_repl > referenceWindow())
	 max_repl = referenceWindow() ;
      // back up to the start of the longest possible overlap region
      file_buffer -= max_repl ;
      const DecodedByte *replacements
	 = m_replacements + which * referenceWindow() ;
      double total_count = countReplacements(which,max_repl) ;
      // weight byte values inversely by their frequency in the candidate
      //   overlap region
      double byte_weights[256] ;
      compute_byte_weights(byte_weights,file_buffer,max_repl) ;
      // score each possible offset, remembering the best one
      double best_score = 0.0 ;
      unsigned best_offset = max_repl ;
      for (unsigned offset = 1 ; offset + 2*MIN_DISC_OVERLAP < max_repl ; offset++)
	 {
	 unsigned count ;
	 unsigned correct ;
	 double score = score_alignment(file_buffer,replacements,max_repl,
					offset,byte_weights,count,correct) ;
	 if (correct < MIN_DISC_OVERLAP)
	    continue ;
	 // weight the raw score by the proportion of overlap between
	 //   inferred replacements and literals prior to the discontinuity
	 score *= std::sqrt(count / total_count) ;
	 // add in an adjustment for the amount of difference between
	 //   expected gap size and actual gap size
	 double expected_gap = corruptionsize * compression_ratio ;
	 if (expected_gap > 0)
	    score *= std::sqrt(fabs(expected_gap - offset)) ;
	 // check whether this score beats the previous best
	 if (score > best_score)
	    {
	    best_score = score ;
	    best_offset = offset ;
	    if (verbosity >= VERBOSITY_SEARCH)
	       {
	       cerr << "\talignDiscontinuity(" << which << "): score = "
		    << score << " @ " << offset << endl ;
	       }
	    }
	 // the probability of matching the majority of the
	 //   replacements at more than one offset is extremely small,
	 //   so quit the search immediately
	 if (correct > total_count / 2)
	    break ;
	 }
      if (best_offset >= max_repl || best_score <= 0.0)
	 return false ;
      // adjust the replacements to refer to the appropriate value in the
      //   pre-discontinuity region
      clearReplacements(which) ;
      setInferredLiterals(which,m_filebuffer+discont_loc,
			  discont_loc - firstRealByte(),best_offset) ;
      m_filebuffer[discont_loc].setDiscontinuitySize(best_offset+1) ;
      writeUpdatedByte(discont_loc - firstRealByte()) ;
      }
   return true ;
}

//----------------------------------------------------------------------

bool DecodeBuffer::alignDiscontinuities()
{
   if (!m_filebuffer)
      return false ;
   START_TIME(timer) ;
   unsigned first = 0 ;
   if (m_filebuffer[firstRealByte()].isDiscontinuity())
      first = 1 ;
   bool success = true ;
   for (size_t disc = first ; disc <= m_discontinuities ; disc++)
      {
      if (!alignDiscontinuity(disc,0,0.0)) //FIXME: get gapsize/comp_ratio
	 {
	 success = false ;
	 break ;
	 }
      }
   ADD_TIME(timer,time_adj_discont) ;
   return success ;
}

//----------------------------------------------------------------------

bool DecodeBuffer::finalizeDB()
{
   FILE *outfp = outputFile() ;
   bool success = true ;
   // append the replacements
   off_t repl_offset = ftell(outfp) ;
   if (numReplacements() > 0)
      {
      success = DecodedByte::writeBuffer(m_replacements,numReplacements(),
					 outfp,WFMT_DecodedByte,
					 unknownChar()) ;
      }

   // optionally append the DEFLATE packet descriptors
   off_t packet_offset = ftell(outfp) ;
   DeflatePacketDesc *packet_list = 0 ; //FIXME
   uint32_t num_packets = packet_list->length() ;
   if (success && num_packets > 0)
      {
      for ( ; packet_list && success ; packet_list = packet_list->next())
	 {
	 if (!packet_list->write(outfp))
	    success = false ;
	 }
      if (!success)
	 num_packets = 0 ;
      }
   // go back and store the number of decoded bytes actually written
   fseek(outfp,sizeof(DECODEDBYTE_SIGNATURE)+8,SEEK_SET) ;
   success = write64(m_numbytes,outfp) ;
   if (success)
      {
      // update the number of discontinuities
      fseek(outfp,6,SEEK_CUR) ;
      success = write16(m_discontinuities,outfp) ;
      }
   if (success)
      {
      // update the count and offset of the replacement values
      unsigned highest = highestReplacement() ;
      if (highest == 0)
	 highest = ((m_discontinuities+1)*referenceWindow()) - 1 ;
      success = (write64(repl_offset,outfp) &&
		 write32(numReplacements(),outfp) &&
		 write32(highest,outfp)) ;
      }
   if (success)
      {
      // update the count and offset of the packet descriptors
      success = (write64(packet_offset,outfp) && write32(num_packets,outfp)) ;
      }
   return success ;
}

//----------------------------------------------------------------------

bool DecodeBuffer::finalize()
{
   bool success = true ;
   if (outputFile())
      {
      DecodeBuffer* me = this ;  // eliminate compiler warning about comparing 'this' against NULL
      DecodedByte::writeFooter(m_format,outputFile(),friendlyFilename(),me) ;
      if (m_format == WFMT_DecodedByte)
	 {
	 success = finalizeDB() ;
	 if (!success)
	    {
	    fprintf(stderr,"Unable to finalize file %s\n",
		    friendlyFilename()) ;
	    }
	 }
      fflush(outputFile()) ;
      m_outfp = 0 ;
      }
   return success ;
}

//----------------------------------------------------------------------

DecodedByte *DecodeBuffer::copyReplacements() const
{
   DecodedByte *repl = 0 ;
   if (numReplacements() > 0 && replacements())
      {
      repl = new DecodedByte[numReplacements()] ;
      if (repl)
	 {
	 for (size_t i = 0 ; i < numReplacements() ; i++)
	    {
	    repl[i] = replacements()[i] ;
	    }
	 }
      }
   return repl ;
}

//----------------------------------------------------------------------

bool DecodeBuffer::expandReplacements(size_t added_repl)
{
   DecodedByte *replacements
      = new DecodedByte[numReplacements() + added_repl + 1] ;
   if (replacements)
      {
      if (numReplacements() > 0)
	 {
	 for (size_t i = 0 ; i <= numReplacements() ; i++)
	    {
	    replacements[i] = m_replacements[i] ;
	    }
	 }
      size_t first = numReplacements() ? 1 : 0 ;
      for (size_t i = first ; i <= added_repl ; i++)
	 {
	 size_t loc = i + numReplacements() ;
	 replacements[loc].setOriginalLocation(loc) ;
	 }
      delete [] m_replacements ;
      m_replacements = replacements ;
      m_numreplacements += added_repl ;
      return true ;
      }
   else
      {
      delete [] replacements ;
      return false ;
      }
}

//----------------------------------------------------------------------

bool DecodeBuffer::clearReplacements(unsigned which_discont)
{
   if (which_discont > m_discontinuities || !m_replacements)
      return false ;
   size_t base = which_discont * referenceWindow() ;
   size_t limit = base + referenceWindow() ;
   if (limit > numReplacements())
      limit = numReplacements() ;
   for (size_t i = base ; i < limit ; i++)
      {
      m_replacements[i].setOriginalLocation(i) ;
      }
   return true ;
}

//----------------------------------------------------------------------

bool DecodeBuffer::setInferredLiterals(unsigned which_discont,
				       const DecodedByte *bytes,
				       size_t num_bytes,
				       unsigned offset)
{
   if (which_discont > m_discontinuities || !m_replacements)
      return false ;
   size_t base = which_discont * referenceWindow() + offset ;
   for (size_t i = 1 ; i < num_bytes && i + offset < referenceWindow() ; i++)
      {
      const DecodedByte &db = bytes[-i] ;
      if (db.isLiteral())
	 {
	 m_replacements[base+i].setInferredByteValue(db.byteValue()) ;
	 m_replacements[base+i].setConfidence(0xDF) ; //FIXME
	 }
      else if (db.isReference())
	 {
	 unsigned origloc = db.originalLocation() ;
	 m_replacements[base+i].setOriginalLocation(origloc) ;
	 }
      }
   return true ;
}

//----------------------------------------------------------------------

bool DecodeBuffer::setReplacements(const DecodedByte *repl, size_t num_repl,
				   bool init)
{
   if (init)
      {
      m_highest_replaced = 0 ;
      m_replacements = 0 ;
      }
   m_numreplacements = num_repl ;
   bool success = true ;
   if (repl)
      {
      m_replacements = new DecodedByte[num_repl+1] ;
      if (m_replacements)
	 {
	 for (size_t i = 0 ; i < num_repl ; i++)
	    {
	    m_replacements[i] = repl[i] ;
	    }
	 }
      else
	 {
	 delete [] m_replacements ; m_replacements = 0 ;
	 success = false ;
	 }
      }
   else
      {
      m_replacements = 0 ;
      m_numreplacements = 0 ;
      }
   return success ;
}

//----------------------------------------------------------------------

bool DecodeBuffer::setReplacement(size_t which, const DecodedByte &repl)
{
   if (which && which < numReplacements())
      {
      m_replacements[which] = repl ;
      if (m_wildcardcounts && repl.isLiteral())
	 m_wildcardcounts->clear(which) ;
      if (which > m_highest_replaced)
	 m_highest_replaced = which ;
      return true ;
      }
   return false ;
}

//----------------------------------------------------------------------

bool DecodeBuffer::setReplacement(size_t which, uint8_t c,
				  unsigned confidence)
{
   if (which && which < numReplacements())
      {
      m_replacements[which].setReconstructed(c,confidence) ;
      if (m_wildcardcounts)
	 m_wildcardcounts->clear(which) ;
      if (which > m_highest_replaced)
	 m_highest_replaced = which ;
      return true ;
      }
   return false ;
}

//----------------------------------------------------------------------

unsigned DecodeBuffer::discontinuities() const
{
   unsigned discont = m_discontinuities ;
   if (m_filebuffer && loadedBytes() >= totalBytes())
      {
      if (!m_filebuffer[firstRealByte()].isDiscontinuity())
	 {
	 if (discont == 0)
	    {
	    // scan for discontinuity markers to verify that we do indeed
	    //   have a discontinuity
	    for (size_t i = firstRealByte() ; i < loadedBytes() ; i++)
	       {
	       if (m_filebuffer[i].isDiscontinuity())
		  return 1 ;
	       }
	    }
	 }
      }
   return discont ;
}

//----------------------------------------------------------------------

bool DecodeBuffer::openInputFile(FILE *fp, const char *filename)
{
   if (!fp)
      {
      m_infp = 0 ;
      m_backingfile = nullptr ;
      return true ;
      }
   m_backingfile = dup_string(filename) ;
   bool success = true ;
   m_infp = fp ;
   // check for the proper signature at the start of the file
   fseek(inputFile(),0L,SEEK_SET) ;
   char sigbuffer[sizeof(DECODEDBYTE_SIGNATURE)] ;
   if (fread(sigbuffer,sizeof(char),sizeof(sigbuffer),fp) != sizeof(sigbuffer))
      success = false ;
   else if (memcmp(sigbuffer,DECODEDBYTE_SIGNATURE,sizeof(sigbuffer)) != 0)
      success = false ;
   uint64_t value64, db_offset = 0 ;
   // read the file offset and number of decoded bytes
   if (success &&
       read64(inputFile(),db_offset) &&
       read64(inputFile(),value64))
      {
      m_numbytes = value64 ;
      }
   else
      {
      m_numbytes = 0 ;
      success = false ;
      }
   uint32_t value ;
   uint16_t per_db ;
   uint16_t discont ;
   // get the size of the reference window, the number of bytes per
   //   DecodedByte (currently unused), and the number of discontinuities1
   m_refwindow = REFERENCE_WINDOW_DEFLATE64 ;
   if (success && read32(inputFile(),value) &&
       read16(inputFile(),per_db) && read16(inputFile(),discont))
      {
      m_refwindow = value ;
      m_deflate64 = (value == REFERENCE_WINDOW_DEFLATE64) ;
      (void)per_db ;
      m_discontinuities = discont ;
      }
   uint64_t repl_offset = ~0 ;
   uint32_t repl_highest = 0 ;
   // read the replacement information, if present
   if (success &&
       read64(inputFile(),repl_offset) &&
       read32(inputFile(),value) && read32(inputFile(),repl_highest))
      {
      m_numreplacements = value ;
      m_highest_replaced = repl_highest ;
      repl_highest++ ;
      if (value > repl_highest)
	 repl_highest = value ;
      }
   else
      {
      m_numreplacements = repl_highest = 0 ;
      success = false ;
      }
   uint64_t packet_offset = ~0 ;
   uint32_t packet_count ;
   // get the offset and count of the packet descriptors
   (void)read64(inputFile(),packet_offset) ;
   (void)read32(inputFile(),packet_count) ;
   // read the optional replacement information
   if (repl_highest > 0)
      {
      delete [] m_replacements ;
      m_replacements = new DecodedByte[repl_highest+1] ;
      if (!m_replacements)
	 {
	 delete [] m_replacements ;
	 m_replacements = 0 ;
	 m_numreplacements = 0 ;
	 repl_highest = 0 ;
	 success = false ;
	 }
      }
   if (repl_highest > 0)
      {
      fseek(inputFile(),repl_offset,SEEK_SET) ;
      for (size_t i = 0 ; i < numReplacements() && success ; i++)
	 {
	 if (!m_replacements[i].read(inputFile()))
	    success = false ;
	 }
      for (size_t i = numReplacements() ; i <= repl_highest ; i++)
	 {
	 m_replacements[i].setOriginalLocation(i) ;
	 }
      m_numreplacements = repl_highest ;
      }
   // read the optional packet descriptors
   if (packet_count > 0)
      {
      fseek(inputFile(),packet_offset,SEEK_SET) ;
      DeflatePacketDesc *packets = 0 ;
      for (size_t i = 0 ; i < packet_count && !feof(inputFile()) ; i++)
	 {
	 DeflatePacketDesc *p = new DeflatePacketDesc(inputFile()) ;
	 if (p)
	    {
	    p->setNext(packets) ;
	    packets = p ;
	    }
	 }
      }
   // return to the start of the decoded bytes, and remember that location
   //   as we'll need to return here to run applyReplacements()
   fseek(inputFile(),db_offset,SEEK_SET) ;
   m_datastart = db_offset ;
   return success ;
}

//----------------------------------------------------------------------

bool DecodeBuffer::setOutputFile(FILE *fp, WriteFormat fmt, unsigned char unk,
				 const char *friendly_filename,
				 const char *encoding, bool test_mode)
{
   bool had_file = (outputFile() != 0) ;
   if (fp && had_file)
      {
      fclose(outputFile()) ;
      }
   m_outfp = fp ;
   m_format = fmt ;
   m_unknown = unk ;
   m_filename = friendly_filename ;
   bool success = true ;
   if (fp && !had_file)
      {
      success = DecodedByte::writeHeader(fmt,fp,encoding,referenceWindow(),
					 test_mode,this) ;
      }
   return success ;
}

//----------------------------------------------------------------------

void DecodeBuffer::rewindInput()
{
   fseek(inputFile(),m_datastart,SEEK_SET) ;
   return ;
}

//----------------------------------------------------------------------

DecodedByte *DecodeBuffer::loadBytes(bool add_sentinel,
				     bool include_wildcards)
{
   if (totalBytes() == 0)
      return 0 ;
   size_t extra = (add_sentinel ? 2 : 0) + (include_wildcards ? numReplacements() : 0) ;
   DecodedByte *bytes = new DecodedByte[totalBytes()+extra] ;
   if (!bytes)
      return 0 ;
   ContextFlags *context_flags = new ContextFlags[totalBytes()+extra] ;
   if (!context_flags)
      {
      delete [] bytes ;
      return 0 ;
      }
   m_loadedbytes = totalBytes() + extra ;
   delete m_wildcardcounts ;
   m_wildcardcounts = new WildcardCounts(referenceWindow()) ;
   bool success = (m_wildcardcounts != 0) ;
   FILE *infp = inputFile() ;
   rewindInput() ;
   size_t ofs = 0 ;
   if (add_sentinel)
      {
      bytes[0] = '\0' ;
      bytes[totalBytes()+1] = '\0' ;
      ofs = 1 ;
      }
   if (include_wildcards)
      {
      if (replacements())
	 ofs += numReplacements() ;
      else
	 include_wildcards = false ;
      }
   if (success)
      {
      for (size_t i = 0 ; i < totalBytes() ; i++)
	 {
	 uint32_t value ;
	 if (!read32(infp,value))
	    {
	    success = false ;
	    break ;
	    }
	 bytes[i+ofs].setOriginalLocation(value) ;
	 if (bytes[i+ofs].isReference())
	    {
	    unsigned loc = bytes[i+ofs].originalLocation() ;
	    if (loc >= m_wildcardcounts->numCounts())
	       {
	       unsigned new_size
		  = (loc + referenceWindow() - 1) / referenceWindow() ;
	       m_wildcardcounts->expandTo(new_size*referenceWindow()) ;
	       }
	    m_wildcardcounts->incr(loc) ;
	    }
	 }
      }
   rewindInput() ;
   if (include_wildcards)
      {
      unsigned highest = highestReplacement() ;
      size_t shift = numReplacements() - highest - 1 ;
      // snip out the unused members
      if (shift)
	 {
	 for (size_t i = ofs ; i < totalBytes() + extra ; i++)
	    bytes[i-shift] = bytes[i] ;
	 }
      ofs = (add_sentinel ? 1 : 0) ;
      for (size_t i = 0 ; i <= highest ; i++)
	 {
	 // reverse while copying, so that the coindices run in the proper
	 //   order for the language model
	 bytes[i+ofs] = m_replacements[highest-i] ;
	 if (bytes[i+ofs].originalLocation() % referenceWindow() == 0)
	    bytes[i+ofs].setOriginalLocation(DBYTE_DISCONTINUITY + referenceWindow()) ;
	 }
      m_loadedbytes -= shift ;
      }
   if (success)
      {
      m_filebuffer = bytes ;
      m_context_flags = context_flags ;
      m_wildcardcounts->setHighestUsed() ;
      return bytes ;
      }
   else
      {
      delete [] bytes ;
      delete [] context_flags ;
      m_filebuffer = 0 ;
      m_context_flags = 0 ;
      return 0 ;
      }
}

//----------------------------------------------------------------------

void DecodeBuffer::clearLoadedBytes()
{
   m_loadedbytes = 0 ;
   delete [] m_filebuffer ;
   m_filebuffer = 0 ;
   return ;
}

//----------------------------------------------------------------------

unsigned DecodeBuffer::copyBufferTail(unsigned char *result,
				      unsigned num_bytes) const
{
   if (num_bytes > m_refwindow)
      num_bytes = m_refwindow ;
   unsigned bufpos = offset() ;
   for (size_t i = num_bytes ; i > 0 ; i--)
      {
      DecodedByte db = m_buffer[bufpos] ;
      bufpos = (bufpos > 0) ? bufpos - 1 : m_refwindow - 1 ;
      result[i-1] = db.isLiteral() ? db.byteValue() : unknownChar() ;
      }
   return num_bytes ;
}

//----------------------------------------------------------------------

bool DecodeBuffer::addByte(DecodedByte b) 
{
   bool success = true ;
   m_buffer[m_bufptr] = b ;
   if (outputFile())
      {
      if (b.originalLocation() == 0 && 
	  (m_format == WFMT_PlainText || m_format == WFMT_HTML))
	 {
	 success = outputString(
	    "\n\n"
	    "*******************************************\n"
	    "***                                     ***\n"
	    "***      Compressed Data Corrupted      ***\n"
	    "***                                     ***\n"
	    "*******************************************\n\n",
	    DBYTE_CONFIDENCE_UNKNOWN) ;
	 }
      else if (!b.write(outputFile(),writeFormat(),unknownChar(),this))
	 success = false ;
      }
   m_bufptr++ ;
   m_bufptr %= referenceWindow() ;
   m_numbytes++ ;
   return success ;
}

//----------------------------------------------------------------------

bool DecodeBuffer::addByte(unsigned char b)
{
   bool success = true ;
   m_buffer[m_bufptr].setByteValue(b) ;
   if (outputFile())
      {
      if (!m_buffer[m_bufptr].write(outputFile(),writeFormat(),unknownChar(),this))
	 success = false ;
      }
   m_bufptr++ ;
   m_bufptr %= referenceWindow() ;
   m_numbytes++ ;
   return success ;
}

//----------------------------------------------------------------------

bool DecodeBuffer::addByte(unsigned char b, unsigned confidence) 
{
   bool success = true ;
   m_buffer[m_bufptr].setByteValue(b) ;
   m_buffer[m_bufptr].setConfidence(confidence) ;
   if (outputFile())
      {
      if (!m_buffer[m_bufptr].write(outputFile(),writeFormat(),unknownChar(),this))
	 success = false ;
      }
   m_bufptr++ ;
   m_bufptr %= referenceWindow() ;
   m_numbytes++ ;
   return success ;
}

//----------------------------------------------------------------------

bool DecodeBuffer::addDiscontinuityMarker(unsigned max_backref,
					  bool clear)
{
   DecodedByte db ;
   db.setOriginalLocation(DBYTE_DISCONTINUITY + max_backref) ;
   bool success = addByte(db) ;
   // the initial packet of the full file's compressed data doesn't
   //   need to add a discontinuity in the back-references, since by
   //   definition there won't be any unresolved references prior to
   //   the point of corruption
   if (clear)
      clearReferenceWindow() ;
   else
      rewindReferenceWindow() ;
   return success ;
}

//----------------------------------------------------------------------

bool DecodeBuffer::addString(const char *s)
{
   if (s)
      {
      bool success = true ;
      for ( ; *s ; s++)
	 {
	 if (!addByte((unsigned char)*s))
	    success = false ;
	 }
      return success ;
      }
   return false ;
}

//----------------------------------------------------------------------

bool DecodeBuffer::addString(const char *s, unsigned confidence)
{
   if (s)
      {
      bool success = true ;
      for ( ; *s ; s++)
	 {
	 if (!addByte((unsigned char)*s,confidence))
	    success = false ;
	 }
      return success ;
      }
   return false ;
}

//----------------------------------------------------------------------

bool DecodeBuffer::outputString(const char *s, unsigned confidence)
{
   if (s && outputFile())
      {
      bool success = true ;
      for ( ; *s ; s++)
	 {
	 DecodedByte db((uint8_t)*s) ;
	 db.setConfidence(confidence) ;
	 if (!db.write(outputFile(),writeFormat(),unknownChar(),this))
	    success = false ;
	 }
      return success ;
      }
   return false ;
}

//----------------------------------------------------------------------

bool DecodeBuffer::copyString(unsigned length, unsigned offset)
{
   bool success = true ;
   for (unsigned i = 0 ; i < length && success ; i++)
      {
      if (!addByte(m_buffer[(m_bufptr-offset) % referenceWindow()]))
	 success = false ;
      }
   return success ;
}

//----------------------------------------------------------------------

bool DecodeBuffer::writeUpdatedByte(size_t which)
{
   if (!m_filebuffer || which >= totalBytes() || !m_backingfile)
      return false ;
   FILE *fp = fopen(m_backingfile,"rb+") ;
   if (fp)
      {
      fseek(fp,m_datastart + BYTES_PER_DBYTE * which,SEEK_SET) ;
      m_filebuffer[which+firstRealByte()].write(fp,WFMT_DecodedByte,
						unknownChar(),this) ;
      fclose(fp) ;
      return true ;
      }
   return false ;
}

//----------------------------------------------------------------------

bool DecodeBuffer::applyReplacement(DecodedByte &db) const
{
   if (!db.isLiteral())
      {
      uint32_t loc = db.originalLocation() ;
      if (loc < numReplacements())
	 {
	 db = m_replacements[loc] ;
	 }
      else
	 return false ;
      }
   return true ;
}

//----------------------------------------------------------------------

bool DecodeBuffer::applyReplacement(uint32_t which) const
{
   DecodedByte &db = m_filebuffer[which] ;
   if (!db.isLiteral())
      {
      uint32_t loc = db.originalLocation() ;
      if (loc < numReplacements())
	 {
	 db = m_replacements[loc] ;
	 }
      else
	 return false ;
      }
   return true ;
}

//----------------------------------------------------------------------

bool DecodeBuffer::writeReplacements(size_t num_discontinuities,
				     unsigned max_backref, FILE *reffp)
{
   if (!outputFile() || numReplacements() == 0)
      return false ;
   size_t base = num_discontinuities * referenceWindow() ;
   size_t limit = numReplacements() ;
   if (limit > base + referenceWindow())
      limit = base + referenceWindow() ;
   size_t high = base ;
   if (m_wildcardcounts)
      {
      for (size_t i = limit ; i > base ; i++)
	 {
	 if (m_wildcardcounts->count(i-1) > 0)
	    {
	    high = i ;
	    break ;
	    }
	 }
      }
   else
      {
      for (size_t i = limit ; i > base ; i++)
	 {
	 if (m_replacements[i-1].isLiteral())
	    {
	    high = i ;
	    break ;
	    }
	 }
      }
   bool success = true ;
   if (high > base + max_backref)
      high = base + max_backref ;
   for (size_t i = high-1 ; i > base ; i--)
      {
      DecodedByte dbyte = m_replacements[i] ;
      if (max_backref < referenceWindow() && count_history_bytes)
	 {
	 INCR_STAT(unknown_bytes) ;
	 INCR_STAT(corrupted_bytes) ;
	 INCR_STAT_IF(dbyte.isLiteral(),bytes_replaced) ;
	 }
      compareToReference(dbyte,reffp,true) ;
      if (!dbyte.write(outputFile(),writeFormat(),unknownChar(),this))
	 {
	 success = false ;
	 break ;
	 }
      }
   return success ;
}

//----------------------------------------------------------------------

size_t DecodeBuffer::highestReplacement(unsigned num_discont,
					unsigned max_backref) const
{
   size_t base = num_discont * referenceWindow() ;
   size_t limit = base + max_backref ;
   for (size_t i = limit ; i > base ; i--)
      {
      if (m_replacements[i-1].isLiteral())
	 return (i - base) ;
      }
   return 0 ;
}

//----------------------------------------------------------------------

unsigned DecodeBuffer::countReplacements(unsigned num_discont,
					 unsigned max_backref) const
{
   if (max_backref == 0)
      max_backref = referenceWindow() ;
   size_t base = num_discont * referenceWindow() ;
   size_t limit = base + max_backref ;
   unsigned count = 0 ;
   for (size_t i = base ; i < limit ; i++)
      {
      if (m_replacements[i].isLiteral())
	 count++ ;
      }
   return count ;
}

//----------------------------------------------------------------------

bool DecodeBuffer::applyReplacements(const char *reference_filename,
				     bool include_predecessors)
{
   if (!inputFile() || !outputFile() || numReplacements() == 0)
      return false ;
   bool success = true ;
   FILE *reffp = 0 ;
   if (reference_filename)
      {
      // open the reference file and skip any un-extracted starting portion
      reffp = fopen(reference_filename,"rb") ;
      if (reffp)
	 {
	 off_t refsize = file_size(reffp) ;
	 bool forced_load = loadedBytes() == 0 ;
	 if (forced_load)
	    loadBytes(false,false) ;
	 if (discontinuities() == 0)
	    {
	    off_t pos = refsize - totalBytes() ;
	    // if the first "real" item in the buffer is a
	    //   discontinuity marker, we need to adjust the offset
	    //   because we should not have counted the marker and
	    //   also need to allow for output of the reconstructed
	    //   history window
	    DecodedByte disc = m_filebuffer[firstRealByte()] ;
	    if (disc.isDiscontinuity())
	       {
	       pos++ ;
	       if (include_predecessors)
		  {
		  unsigned max_backref = disc.discontinuitySize() ;
		  if (max_backref == referenceWindow())
		     max_backref = highestReplacement(0,max_backref) ;
		  if (max_backref) max_backref-- ; //ref=0 doesn't exist
		  if (max_backref < pos)
		     pos -= max_backref ;
		  else
		     pos = 0 ;
		  }
	       }
	    fseek(reffp,pos,SEEK_SET) ;
	    }
	 if (forced_load)
	    clearLoadedBytes() ;
	 }
      }
   // rewind to the start of the actual byte data
   rewindInput() ;
   m_prev_correct = true ;
   m_show_errors = (show_plaintext_errors && verbosity > 0
		    && (writeFormat() == WFMT_PlainText)) ;
   unsigned num_discont = 0 ;
   size_t bytecount = 0 ;
   while (!feof(inputFile()) && bytecount++ < totalBytes())
      {
      DecodedByte dbyte ;
      // get the next byte of the recovered data
      if (!dbyte.read(inputFile()))
	 {
	 success = false ;
	 break ;
	 }
      if (dbyte.isDiscontinuity())
	 {
	 unsigned max_backref = dbyte.discontinuitySize() ;
	 // optionally output replacements occurring before the start
	 //   of the actual recovered byte data
	 if (include_predecessors)
	    {
	    bool show_message = (verbosity > 0) ;
	    if (max_backref == referenceWindow())
	       {
	       show_message = true ;
	       max_backref = highestReplacement(num_discont,max_backref) ;
	       }
	    if (show_message)
	       DecodedByte::writeMessage(writeFormat(),outputFile(),
					 "\n===***=== reconstructed back-references ===***===\n") ;
	    if (!writeReplacements(num_discont++,max_backref,reffp))
	       return false ;
	    if (show_message)
	       DecodedByte::writeMessage(writeFormat(),outputFile(),
					 "\n===***=== start of recovered data ===***===\n") ;
	    }
	 else if (num_discont++ > 0)
	    {
	    DecodedByte::writeMessage(writeFormat(),outputFile(),
				      "\n\n===***=== data corruption detected at this point ===***===\n\n") ;
	    }
	 // in test mode, we need to resynchronize the reference file
	 //   at this point if the discontinuity is not at the very
	 //   start
	 if (reffp && bytecount > 0)
	    {
	    // Note: the following only works for a single discontinuity per file
	    off_t pos = file_size(reffp) - totalBytes() + bytecount ;
	    fseek(reffp,pos,SEEK_SET) ;
	    }
	 continue ;
	 }
      bool replaced = false ;
      // apply the replacement, if available
      if (!dbyte.isLiteral())
	 {
	 uint32_t loc = dbyte.originalLocation() ;
	 if (loc < numReplacements())
	    {
	    dbyte = m_replacements[loc] ;
	    if (dbyte.isLiteral())
	       {
	       replaced = true ;
	       INCR_STAT(bytes_replaced) ;
	       }
	    else
	       {
	       INCR_STAT(reconst_unaltered) ;
	       }
	    }
	 else
	    {
	    INCR_STAT(reconst_unaltered) ;
	    success = false ;
	    break ;
	    }
	 }
      // compare current byte against reference, if available
      compareToReference(dbyte,reffp,replaced) ;
      // write the current byte to the output file
      if (!dbyte.write(outputFile(),writeFormat(),unknownChar(),this))
	 {
	 success = false ;
	 break ;
	 }
      }
   if (reffp)
      fclose(reffp) ;
   return success ;
}

//----------------------------------------------------------------------

void DecodeBuffer::compareToReference(DecodedByte dbyte, FILE *reffp,
				      bool replaced)
{
   if (reffp)
      {
      int refch = fgetc(reffp) ;
      INCR_STAT(total_bytes) ;
      INCR_STAT_IF(dbyte.isLiteral()&&dbyte.byteValue()==refch,identical_bytes) ;
      if (replaced)
	 {
	 if (dbyte.isLiteral())
	    {
	    INCR_STAT(reconst_bytes) ;
	    if (dbyte.byteValue() == refch)
	       {
	       INCR_STAT(reconst_correct) ;
	       if (m_show_errors && !m_prev_correct)
		  {
		  DecodedByte rightbrace('}') ;
		  rightbrace.write(outputFile(),writeFormat(),unknownChar(),this) ;
		  m_prev_correct = true ;
		  }
	       }
	    else
	       {
	       if (m_show_errors && m_prev_correct)
		  {
		  DecodedByte leftbrace('{') ;
		  leftbrace.write(outputFile(),writeFormat(),unknownChar(),this) ;
		  m_prev_correct = false ;
		  }
	       if (tolower(dbyte.byteValue()) == tolower(refch))
		  {
		  INCR_STAT(reconst_correct_casefolded) ;
		  }
	       }
	    }
	 else
	    {
	    if (m_show_errors && m_prev_correct)
	       {
	       DecodedByte leftbrace('{') ;
	       leftbrace.write(outputFile(),writeFormat(),unknownChar(),this) ;
	       m_prev_correct = false ;
	       }
	    }
	 }
      else if (m_show_errors && !m_prev_correct)
	 {
	 DecodedByte rightbrace('}') ;
	 rightbrace.write(outputFile(),writeFormat(),unknownChar(),this) ;
	 m_prev_correct = true ;
	 }
      }
   return ;
}

//----------------------------------------------------------------------

bool DecodeBuffer::convert(size_t offset, size_t length, unsigned char unk,
			   char *result, bool *literals)
{
   // set the file pointer to the start of the data to be converted
   fseek(inputFile(),m_datastart + BYTES_PER_DBYTE * offset,SEEK_SET) ;
   for (size_t i = 0 ; i < length ; i++)
      {
      // get a byte
      DecodedByte dbyte ;
      dbyte.read(inputFile()) ;
      // apply any known replacements
      if (!dbyte.isLiteral())
	 {
	 uint32_t loc = dbyte.originalLocation() ;
	 if (loc < numReplacements())
	    {
	    dbyte = m_replacements[loc] ;
	    }
	 }
      // if still unknown, store the specified 'unk' value, else
      //   copy the value to the buffer
      bool literal = dbyte.isLiteral() ;
      *result++ = (char)(literal ? dbyte.byteValue() : unk) ;
      if (literals)
	 *literals++ = literal ;
      }
   return true ;
}

// end of file dbuffer.C //
