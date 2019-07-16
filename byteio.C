/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  File: byteio.C - multi-byte input/output functions			*/
/*  Version:  1.00gamma				       			*/
/*  LastEdit: 09may2013							*/
/*									*/
/*  (c) Copyright 2011,2012,2013 Ralf Brown/CMU				*/
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

#include "byteio.h"

/************************************************************************/
/*	Helper functions						*/
/************************************************************************/

static uint64_t readN(FILE *fp, unsigned count, bool &OK)
{
   uint64_t value = 0 ;
   if (fp)
      {
      OK = true ;
      for (size_t i = 0 ; i < count ; i++)
	 {
	 int byte = fgetc(fp) ;
	 if (byte == EOF)
	    {
	    value = 0 ;
	    OK = false ;
	    break ;
	    }
	 value = (value << 8) | (byte & 0xFF) ;
	 }
      }
   else
      {
      OK = false ;
      }
   return value ;
}

/************************************************************************/
/************************************************************************/

bool read16(FILE *fp, uint16_t &value)
{
   bool success ;
   value = (uint16_t)readN(fp,2,success) ;
   return success ;
}

//----------------------------------------------------------------------

bool read24(FILE *fp, uint32_t &value)
{
   bool success ;
   value = (uint32_t)readN(fp,3,success) ;
   return success ;
}

//----------------------------------------------------------------------

bool read32(FILE *fp, uint32_t &value)
{
   bool success ;
   value = (uint32_t)readN(fp,4,success) ;
   return success ;
}

//----------------------------------------------------------------------

bool read64(FILE *fp, uint64_t &value)
{
   bool success ;
   value = readN(fp,8,success) ;
   return success ;
}

//----------------------------------------------------------------------

bool write16(uint16_t val, FILE *outfp)
{
   return (fputc((val >> 8) & 0xFF, outfp) != EOF &&
	   fputc(val & 0xFF, outfp) != EOF) ;
}

//----------------------------------------------------------------------

bool write24(uint32_t val, FILE *outfp)
{
   return (fputc((val >> 16) & 0xFF, outfp) != EOF &&
	   fputc((val >> 8) & 0xFF, outfp) != EOF &&
	   fputc(val & 0xFF, outfp) != EOF) ;
}

//----------------------------------------------------------------------

bool write32(uint32_t val, FILE *outfp)
{
   return (fputc((val >> 24) & 0xFF, outfp) != EOF &&
	   fputc((val >> 16) & 0xFF, outfp) != EOF &&
	   fputc((val >> 8) & 0xFF, outfp) != EOF &&
	   fputc(val & 0xFF, outfp) != EOF) ;
}

//----------------------------------------------------------------------

bool write64(uint64_t val, FILE *outfp)
{
   return (fputc((val >> 56) & 0xFF, outfp) != EOF &&
	   fputc((val >> 48) & 0xFF, outfp) != EOF &&
	   fputc((val >> 40) & 0xFF, outfp) != EOF &&
	   fputc((val >> 32) & 0xFF, outfp) != EOF &&
           fputc((val >> 24) & 0xFF, outfp) != EOF &&
	   fputc((val >> 16) & 0xFF, outfp) != EOF &&
	   fputc((val >> 8) & 0xFF, outfp) != EOF &&
	   fputc(val & 0xFF, outfp) != EOF) ;
}

// end of file byteio.C //
