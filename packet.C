/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  File: packet.C - DEFLATE Packet Descriptor				*/
/*  Version:  1.10beta				       			*/
/*  LastEdit: 27jun2019							*/
/*									*/
/*  (c) Copyright 2012,2013,2019 Ralf Brown/CMU				*/
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
#include "framepac/config.h"

using namespace Fr ;

/************************************************************************/
/************************************************************************/

DeflatePacketDesc::DeflatePacketDesc(const BitPointer *stream_start,
				     const BitPointer *packet_start,
				     const BitPointer *packet_end, bool last,
				     bool deflate64)
   : m_stream_start(stream_start), m_packet_header(packet_start),
     m_packet_body(packet_start), m_packet_end(packet_end)
{
   m_stream_data = 0 ;
   clearCorruption() ;
   m_last = last ;
   m_uncomp_offset = 0 ;
   m_uncomp_size = 0 ;
   m_stream_len = 0 ;
   usingDeflate64(deflate64) ;
   m_packet_type = PT_DYNAMIC ; // default type, as it's the most common
   return ;
}

//----------------------------------------------------------------------

DeflatePacketDesc::~DeflatePacketDesc()
{
   Free(m_stream_data) ;
   m_stream_data = 0 ;

   return ;
}

//----------------------------------------------------------------------

unsigned DeflatePacketDesc::length() const
{
   unsigned count = 0 ;
   for (const DeflatePacketDesc *p = this ; p ; p = p->next())
      {
      count++ ;
      }
   return count ;
}

//----------------------------------------------------------------------

void DeflatePacketDesc::setUncompOffset(const DeflatePacketDesc *prev)
{
   if (prev)
      {
      if (prev->uncompressedSize() == (unsigned long)~0 ||
	  prev->uncompressedOffset() == ~0)
	 {
	 m_uncomp_offset = ~0 ;
	 }
      else
	 {
	 m_uncomp_offset
	    = prev->uncompressedOffset() + prev->uncompressedSize() ;
	 }
      }
   else
      m_uncomp_offset = 0 ;
   return ;
}

//----------------------------------------------------------------------

void DeflatePacketDesc::clearCorruption()
{
   m_corruption_start = ~0 ;
   m_corruption_end = 0 ;
   m_corruption_end_unknown = false ;
   return ;
}

//----------------------------------------------------------------------

void DeflatePacketDesc::setCorruption(unsigned long startloc,
				      unsigned long endloc)
{
   m_corruption_start = startloc ;
   m_corruption_end = endloc ;
   m_corruption_end_unknown = false ;
   return ;
}

//----------------------------------------------------------------------

void DeflatePacketDesc::setCorruption(unsigned long loc)
{
   m_corruption_start = loc ;
   m_corruption_end = loc ;
   m_corruption_end_unknown = true ;
   return ;
}

//----------------------------------------------------------------------

void DeflatePacketDesc::updateCorruption(unsigned long startloc,
					 unsigned long endloc)
{
   if (startloc < corruptionStart())
      m_corruption_start = startloc ;
   if (endloc > corruptionEnd())
      m_corruption_end = endloc ;
   m_corruption_end_unknown = true ;
   return ;
}

//----------------------------------------------------------------------

void DeflatePacketDesc::clipStart(size_t bytes_to_skip)
{
   m_corruption_start = m_corruption_end = 0 ;
   m_packet_header.advanceBytes(bytes_to_skip) ;
   m_packet_body = m_packet_header ;
   return ;
}

//----------------------------------------------------------------------

void DeflatePacketDesc::missingStart()
{
   m_corruption_start = 0 ;
   m_corruption_end = 1 ;
   return ;
}

//----------------------------------------------------------------------

void DeflatePacketDesc::missingEnd()
{
   m_corruption_start
      = packetEnd().bytePointer() - packetHeader().bytePointer() ;
   m_corruption_end = ~0 ;
   DeflatePacketDesc *n = next() ;
   m_next = 0 ;
   delete n ;
   return ;
}

//----------------------------------------------------------------------

bool DeflatePacketDesc::cacheStreamData()
{
   if (!m_stream_data)
      {
//FIXME

      }
   return m_stream_data != 0 ;
}

//----------------------------------------------------------------------

bool DeflatePacketDesc::split(const BitPointer &next_packet_start,
			      unsigned ptype)
{
   if (next_packet_start > packetHeader() && next_packet_start < packetEnd())
      {
      DeflatePacketDesc *newpacket
	 = new DeflatePacketDesc(&streamStart(),&next_packet_start,
				 &packetEnd(),last(),deflate64()) ;
      if (newpacket)
	 {
	 newpacket->setPacketType((PacketType)ptype) ;
	 newpacket->setNext(next()) ;
	 setNext(newpacket) ;
	 m_packet_end = next_packet_start ;
	 m_last = false ;
	 return true ;
	 }
      }
   return false ;
}

//----------------------------------------------------------------------

bool DeflatePacketDesc::read(CFile& infp)
{
   if (infp)
      {
      uint32_t value32 ;
      uint64_t value64 ;
      int value8 ;
      bool success = infp.read64LE(value64) ;
      if (success)
	 {
	 m_uncomp_offset = value64 ;
	 success = infp.read32LE(value32) ;
	 }
      if (success)
	 {
	 m_uncomp_size = value32 ;
	 success = infp.read32LE(value32) ;
	 }
      if (success)
	 {
	 m_stream_len = value32 ;
	 success = infp.read32LE(value32) ;
	 }
      if (success)
	 {
	 m_corruption_start = value32 ;
	 success = infp.read32LE(value32) ;
	 }
      if (success)
	 {
	 m_corruption_end = value32 ;
	 value8 = infp.getc() ;
	 success = (value8 != EOF) ;
	 }
      if (success)
	 {
	 m_last = value8 ? true : false ;
	 value8 = infp.getc() ;
	 success = (value8 != EOF) ;
	 }
      if (success)
	 {
	 m_deflate64 = value8 ? true : false ;
	 }
//FIXME: BitPointer fields and a copy of the stream data
      return success ;
      }
   return false ;
}

//----------------------------------------------------------------------

bool DeflatePacketDesc::write(CFile& outfp) const
{
   if (outfp)
      {
      bool success 
	 = (outfp.write64LE(m_uncomp_offset) &&
	    outfp.write32LE(m_uncomp_size) &&
	    outfp.write32LE(m_stream_len) &&
	    outfp.write32LE(m_corruption_start) &&
	    outfp.write32LE(m_corruption_end) &&
	    outfp.putc(m_last ? '\1' : '\0') &&
	    outfp.putc(m_deflate64 ? '\1' : '\0')) ;
//FIXME: add BitPointer fields and a copy of the stream data
      
      return success ;
      }
   return false ;
}

// end of file packet.C //
