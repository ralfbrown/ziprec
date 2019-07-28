/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  File: inflate.h - DEFLATE decompression				*/
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

#ifndef __INFLATE_H_INCLUDED
#define __INFLATE_H_INCLUDED

#include "huffman.h"
#include "framepac/file.h"
#include "framepac/smartptr.h"

/************************************************************************/
/*	Manifest Constants						*/
/************************************************************************/

#define PACKHDR_SIZE 3
#define PACKHDR_LAST_SIZE 1
#define PACKHDR_TYPE_SIZE 2
// since we retrieve bits in "reverse" order, adjust the masks appropriately:
//   the last-packet flag is the first bit, which is the LSB, while the
//   type bits are the two MSBs
#define PACKHDR_LAST_MASK 0x01
#define PACKHDR_TYPE_MASK 0x06
#define PACKHDR_TYPE(x) ((x & PACKHDR_TYPE_MASK) >> 1)

#define LITERAL_LENGTH	1	// flags code as a literal
#define INVALID_LENGTH  0
#define INVALID_DISTANCE 0

// number of possible values for the compressed bit-length data
#define NUM_BIT_LENGTHS 19

// the different types of packets have different minimum legal/sensible
//   lengths in bits:
//  uncompressed:    35/43
//  fixed Huffman:   12/19
//  dynamic Huffman: >20
// use the smallest of the above as the amount to back up from the start
//   of the successor packet before searching for another packet start
#define MINIMUM_PACKET_SIZE_BITS 20

// the maximum length of a non-DEFLATE64 stream is 4GB
#define MAX_DEFLATE_SIZE (4UL * 1024 * 1024 * 1024)

// the maximum size of a literal packet is 64K plus 4 bytes for the
//   size fields and up to two bytes for the packet type header (if it
//   straddles a byte boundary)
#define MAX_LITERAL_PACKET_SIZE (64*1024 + 6)

// the maximum length of the Huffman code for a symbol is set by the
//   file format, which only provides four bits for the bit lengths
//   in the compressed encoding of the Huffman tree
#define MAX_BITLENGTH 15

/************************************************************************/
/*	Type definitions						*/
/************************************************************************/

enum PacketType
   {
      PT_UNCOMP = 0,
      PT_FIXEDHUFF = 1,
      PT_DYNAMIC = 2,
      PT_INVALID = 3
   } ;

//----------------------------------------------------------------------

class LocationList ;
class FileInformation ;

//----------------------------------------------------------------------

//----------------------------------------------------------------------

class DeflatePacketDesc
   {
   public:
      DeflatePacketDesc(const BitPointer *stream_start,
			const BitPointer *packet_start,
			const BitPointer *packet_end, bool last = false,
			bool deflate64 = false) ;
      DeflatePacketDesc(Fr::CFile& fp) { read(fp) ; }
      ~DeflatePacketDesc() ;

      // accessors
      DeflatePacketDesc *next() const { return m_next ; }
      const BitPointer &streamStart() const { return m_stream_start ; }
      const BitPointer &packetHeader() const { return m_packet_header ; }
      const BitPointer &packetBody() const { return m_packet_body ; }
      const BitPointer &packetEnd() const { return m_packet_end ; }
      const uint8_t *streamData() const { return m_stream_data ; }
      bool last() const { return m_last ; }
      bool isUncompressed() const { return m_packet_type == PT_UNCOMP ; }
      PacketType packetType() const { return m_packet_type ; }
      bool deflate64() const { return m_deflate64 ; }
      bool containsCorruption() const
	 { return m_corruption_end >= m_corruption_start ; }
      bool indefiniteCorruption() const { return m_corruption_end_unknown ; }
      off_t uncompressedOffset() const { return m_uncomp_offset ; }
      unsigned long uncompressedSize() const { return m_uncomp_size ; }
      unsigned long corruptionStart() const { return m_corruption_start ; }
      unsigned long corruptionEnd() const { return m_corruption_end ; }
      unsigned length() const ;

      // manipulators
      void setNext(DeflatePacketDesc *nxt) { m_next = nxt ; }
      void setPacketType(PacketType type) { m_packet_type = type ; }
      void markAsLast() { m_last = true ; }
      void clearCorruption() ;
      void setCorruption(unsigned long startloc, unsigned long endloc) ;
      void setCorruption(unsigned long loc) ;
      void updateCorruption(unsigned long startloc, unsigned long endloc) ;
      void setUncompOffset(const DeflatePacketDesc *prv) ;
      void setUncompSize(unsigned long size) { m_uncomp_size = size ; }
      void clipStart(size_t bytes_to_skip) ;
      void missingStart() ;
      void missingEnd() ;
      void usingDeflate64(bool use = true) { m_deflate64 = use ; }
      bool cacheStreamData() ; // make local copy

      bool split(const BitPointer &next_packet_start,
		 unsigned type = PT_DYNAMIC) ;

      // I/O
      bool read(class Fr::CFile& infp) ;
      bool write(Fr::CFile& outfp) const ;

   private:
      DeflatePacketDesc *m_next ;
      Fr::NewPtr<uint8_t> m_stream_data ;
      BitPointer	m_stream_start ;
      BitPointer 	m_packet_header ;
      BitPointer 	m_packet_body ;
      BitPointer 	m_packet_end ;
      off_t		m_uncomp_offset ;
      unsigned long	m_uncomp_size ;
      unsigned long	m_stream_len ;
      unsigned long	m_corruption_start ;
      unsigned long	m_corruption_end ;
      PacketType	m_packet_type ;
      bool		m_last ;
      bool		m_deflate64 ;
      bool		m_corruption_end_unknown ;
   } ;

/************************************************************************/
/*	Functions						        */
/************************************************************************/

bool valid_packet_header(const char* buffer, bool deflate64, bool allow_fixedHuff) ;

bool recover_stream(const LocationList* start_sig, const LocationList* end_sig,
		    const class ZipRecParameters&, const FileInformation* fileinfo,
		    const char* filename_hint, uint32_t original_size_hint,
		    bool known_start, bool deflate64, bool known_end = true) ;

#endif /* !__INFLATE_H_INCLUDED */

// end of file inflate.h //
