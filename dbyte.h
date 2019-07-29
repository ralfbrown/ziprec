/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  File: dbyte.h - representation of a byte or back-reference		*/
/*  Version:  1.10beta				       			*/
/*  LastEdit: 2019-07-25						*/
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

#ifndef __DBYTE_H_INCLUDED
#define __DBYTE_H_INCLUDED

#include <cstdio>
#include <stdint.h>
#include <unistd.h>	// for off_t

#include "framepac/file.h"

using namespace std ;

/************************************************************************/
/*	Manifest Constants						*/
/************************************************************************/

#define DEFAULT_UNKNOWN '?'

#define DECODEDBYTE_SIGNATURE "Recovered Lempel-Ziv Data Stream\nv2\n\n\x1A\4"

#define DBYTE_RECONSTRUCTED      0xFF000000
#define DBYTE_MASK_LITERAL       0xFF800000
#define DBYTE_MASK_CERTAINLIT    0xFFE00000

#define DBYTE_DISCONTINUITY	 0xFC000000
#define DBYTE_DISCONTINUITY_MASK 0xFE000000

#define DBYTE_MASK_CONFIDENCE    0x007F0000
#define DBYTE_SHIFT_CONFIDENCE   16
#define DBYTE_CONFIDENCE_USER    0x007F0000
#define DBYTE_CONFIDENCE_UNKNOWN 0x00000000
#define DBYTE_CONFIDENCE_LEVELS  62   // six bits, less "user" and "unknown"

#define DBYTE_INFER_BASE         0x00400000
#define DBYTE_INFER_LEVELS	 32

#define DBYTE_MASK_TYPE          0x00FF0000 // literal bit + conf = non-ptr type
#define DBYTE_SHIFT_TYPE         16
#define DBYTE_LIT_TYPE(x) (((x) & DBYTE_MASK_TYPE) >> DBYTE_SHIFT_TYPE)

#define REFERENCE_WINDOW_DEFLATE   (32 * 1024)
#define REFERENCE_WINDOW_DEFLATE64 (64 * 1024)
#define REFERENCE_WINDOW_LZNT1     4096

// number of bytes a DecodedByte takes on disk
#define BYTES_PER_DBYTE 4

/************************************************************************/
/*	Utility macros							*/
/************************************************************************/

#ifndef lengthof
#define lengthof(x) (sizeof(x) / sizeof(x[0]))
#endif

/************************************************************************/
/*	Types								*/
/************************************************************************/

enum ByteType
   {
      BT_Unknown,
      BT_WildGuess,
      BT_Guessed,
      BT_Reconstructed,
      BT_UserSupplied,
      BT_InferredLit,
      BT_Literal
   } ;

//----------------------------------------------------------------------

enum WriteFormat
   {
      WFMT_None,
      WFMT_PlainText,
      WFMT_DecodedByte,
      WFMT_HTML,
      WFMT_Listing,
      WFMT_Buffered
   } ;

//----------------------------------------------------------------------

class DecodeBuffer ;

class DecodedByte
   {
   public:
      DecodedByte(const DecodedByte &orig)
	 { m_byte_or_pointer = orig.m_byte_or_pointer ; }
      DecodedByte(uint8_t byte) { setByteValue(byte) ; }
      DecodedByte(Fr::CFile& fp) { read(fp) ; }
      DecodedByte() { m_byte_or_pointer = 0 ; }
      ~DecodedByte() {}

      DecodedByte &operator = (const DecodedByte &orig)
	 { m_byte_or_pointer = orig.m_byte_or_pointer ; return *this ; }
      DecodedByte &operator = (uint8_t byte)
	 { setByteValue(byte) ; return *this ; }
      
      // accessors
      bool isLiteral() const
	 { return m_byte_or_pointer >= DBYTE_RECONSTRUCTED ; }
      bool isReference() const
	 { return m_byte_or_pointer < DBYTE_DISCONTINUITY ; }
      bool isOriginalLiteral() const
	 { return (m_byte_or_pointer & DBYTE_MASK_LITERAL) == DBYTE_MASK_LITERAL ; }
      bool isInferredLiteral() const
	 { return isLiteral() &&
	       (m_byte_or_pointer & DBYTE_MASK_CERTAINLIT) != DBYTE_MASK_CERTAINLIT &&
	       (m_byte_or_pointer & DBYTE_MASK_TYPE) >= DBYTE_INFER_BASE ; }
      bool isReconstructed() const
	 { return (m_byte_or_pointer & DBYTE_MASK_LITERAL) == DBYTE_RECONSTRUCTED ; }
      bool isDiscontinuity() const
	 { return (m_byte_or_pointer & DBYTE_DISCONTINUITY_MASK) == DBYTE_DISCONTINUITY ; }
      ByteType byteType() const
	 { return isLiteral() ? byteType_raw() : BT_Unknown ; }
      unsigned confidence() const
	 { return (m_byte_or_pointer & DBYTE_MASK_CONFIDENCE) >> DBYTE_SHIFT_CONFIDENCE ; }
      uint8_t byteValue() const { return (uint8_t)(m_byte_or_pointer & 0xFF); }
      uint32_t originalLocation() const { return m_byte_or_pointer ; }
      unsigned discontinuitySize() const
	 { return m_byte_or_pointer & ~DBYTE_DISCONTINUITY_MASK ; }

      static uint64_t globalTotalBytes() { return s_global_total_bytes ; }
      static uint64_t globalKnownBytes() { return s_global_known_bytes ; }
      static uint64_t globalOriginalSize() { return s_global_original_size ; }

      // manipulators
      static void setOriginalSize(size_t size)
	 { s_original_size = size ; s_global_original_size += size ; }
      static void addCounts(size_t known, size_t total, size_t original) ;
      static void clearCounts() ;
      void setOriginalLocation(uint32_t loc) { m_byte_or_pointer = loc ; }
      void setByteValue(uint8_t byte)
	 { m_byte_or_pointer = DBYTE_MASK_CERTAINLIT | byte ; }
      void setInferredByteValue(uint8_t byte)
	 { m_byte_or_pointer = DBYTE_MASK_LITERAL | byte ; }
      void setReconstructed(uint8_t byte, unsigned conf)
	 { m_byte_or_pointer = (DBYTE_RECONSTRUCTED
				| ((conf << DBYTE_SHIFT_CONFIDENCE)
				   & DBYTE_MASK_CONFIDENCE)
				| byte) ; }
      void setConfidence(unsigned conf)
	 { m_byte_or_pointer = ((m_byte_or_pointer & ~DBYTE_MASK_CONFIDENCE)
				| (conf << DBYTE_SHIFT_CONFIDENCE)) ; }
      void setDiscontinuitySize(unsigned size)
	 { if (isDiscontinuity())
	       m_byte_or_pointer = DBYTE_DISCONTINUITY | (size & ~DBYTE_DISCONTINUITY_MASK) ; }

      // I/O
      bool read(Fr::CFile& infp) ;
      bool write(Fr::CFile& outfp, WriteFormat, unsigned char unknown_char, DecodeBuffer* dbuf = nullptr) const ;
      static bool writeHTMLHeader(Fr::CFile&, const char *encoding, bool test_mode) ;
      static bool writeDBHeader(Fr::CFile&, size_t ref_window) ;
      static bool writeHeader(WriteFormat, Fr::CFile&, const char* encoding = nullptr,
			      size_t reference_window = REFERENCE_WINDOW_DEFLATE,
			      bool test_mode = false, DecodeBuffer* dbuf = nullptr) ;
      static bool writeBuffer(const DecodedByte *buf, size_t n_elem,
	 		      Fr::CFile& outfp, WriteFormat fmt = WFMT_PlainText,
			      unsigned char unknown_char = DEFAULT_UNKNOWN) ;
      static bool writeMessage(WriteFormat, Fr::CFile&, const char* msg) ;
      static bool writeFooter(WriteFormat, Fr::CFile&, const char* filename,
			      bool test_mode = false, DecodeBuffer* dbuf = nullptr) ;

   protected:
      ByteType byteType_raw() const
	 { return s_confidence_to_type[DBYTE_LIT_TYPE(m_byte_or_pointer)] ; }
      ByteType prevByteType() const { return s_prev_bytetype ; }
      static void prevByteType(ByteType bt) { s_prev_bytetype = bt ; }

   private:
      // we use 24 bits in the file, but there is no convenient standard
      //  type of that size, so use 32 bits in RAM
      uint32_t m_byte_or_pointer ;
      static ByteType s_prev_bytetype ;
      static const ByteType s_confidence_to_type[] ;
      static size_t s_total_bytes ;	// statistics for WFMT_Listing
      static size_t s_known_bytes ;
      static size_t s_original_size ;
      static uint64_t s_global_total_bytes ;
      static uint64_t s_global_known_bytes ;
      static uint64_t s_global_original_size ;
   } ;

#endif /* !__DBYTE_H_INCLUDED */

// end of file dbyte.h //
