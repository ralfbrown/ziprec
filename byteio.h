/****************************** -*- C++ -*- *****************************/
/*									*/
/*	ZipRecover: extract text from corrupted zip/gzip streams	*/
/*	by Ralf Brown / Carnegie Mellon University			*/
/*									*/
/*  File: byteio.h - multi-byte input/output functions			*/
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

#include <cstdlib>
#include <cstdio>
#include <stdint.h>

bool read16(FILE *fp, uint16_t &value) ;
bool read24(FILE *fp, uint32_t &value) ;
bool read32(FILE *fp, uint32_t &value) ;
bool read64(FILE *fp, uint64_t &value) ;

bool write16(uint16_t val, FILE *outfp) ;
bool write24(uint32_t val, FILE *outfp) ;
bool write32(uint32_t val, FILE *outfp) ;
bool write64(uint64_t val, FILE *outfp) ;

// end of file byteio.h //
