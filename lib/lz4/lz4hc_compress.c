/*
 * LZ4 HC - High Compression Mode of LZ4
 * Copyright (C) 2011-2015, Yann Collet.
 *
 * BSD 2 - Clause License (http://www.opensource.org/licenses/bsd - license.php)
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *	* Redistributions of source code must retain the above copyright
 *	  notice, this list of conditions and the following disclaimer.
 *	* Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * You can contact the author at :
 *	- LZ4 homepage : http://www.lz4.org
 *	- LZ4 source repository : https://github.com/lz4/lz4
 *
 *	Changed for kernel usage by:
 *	Sven Schmidt <4sschmid@informatik.uni-hamburg.de>
 */

/*-************************************
 *	Dependencies
 **************************************/
#include <linux/lz4.h>
#include "lz4defs.h"
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h> /* memset */

/* *************************************
 *	Local Constants and types
 ***************************************/

#define OPTIMAL_ML (int)((ML_MASK - 1) + MINMATCH)

#define HASH_FUNCTION(i)	(((i) * 2654435761U) \
	>> ((MINMATCH*8) - LZ4HC_HASH_LOG))
#define DELTANEXTU16(p)	chainTable[(U16)(p)] /* faster */

static U32 LZ4HC_hashPtr(const void *ptr)
{
	return HASH_FUNCTION(LZ4_read32(ptr));
}

/**************************************
 *	HC Compression
 **************************************/
static void LZ4HC_init(LZ4HC_CCtx_internal *hc4, const BYTE *start)
{
	memset((void *)hc4->hashTable, 0, sizeof(hc4->hashTable));
	memset(hc4->chainTable, 0xFF, sizeof(hc4->chainTable));
	hc4->nextToUpdate = 64 * KB;
	hc4->base = start - 64 * KB;
	hc4->end = start;
	hc4->dictBase = start - 64 * KB;
	hc4->dictLimit = 64 * KB;
	hc4->lowLimit = 64 * KB;
}

/* Update chains up to ip (excluded) */
static FORCE_INLINE void LZ4HC_Insert(LZ4HC_CCtx_internal *hc4,
	const BYTE *ip)
{
	U16 * const chainTable = hc4->chainTable;
	U32 * const hashTable	= hc4->hashTable;
	const BYTE * const base = hc4->base;
	U32 const target = (U32)(ip - base);
	U32 idx = hc4->nextToUpdate;

	while (idx < target) {
		U32 const h = LZ4HC_hashPtr(base + idx);
		size_t delta = idx - hashTable[h];

		if (delta > MAX_DISTANCE)
			delta = MAX_DISTANCE;

		DELTANEXTU16(idx) = (U16)delta;

		hashTable[h] = idx;
		idx++;
	}

	hc4->nextToUpdate = target;
}

static FORCE_INLINE int LZ4HC_InsertAndFindBestMatch(
	LZ4HC_CCtx_internal *hc4, /* Index table will be updated */
	const BYTE *ip,
	const BYTE * const iLimit,
	const BYTE **matchpos,
	const int maxNbAttempts)
{
	U16 * const chainTable = hc4->chainTable;
	U32 * const HashTable = hc4->hashTable;
	const BYTE * const base = hc4->base;
	const BYTE * const dictBase = hc4->dictBase;
	const U32 dictLimit = hc4->dictLimit;
	const U32 lowLimit = (hc4->lowLimit + 64 * KB > (U32)(ip - base))
		? hc4->lowLimit
		: (U32)(ip - base) - (64 * KB - 1);
	U32 matchIndex;
	int nbAttempts = maxNbAttempts;
	size_t ml = 0;

	/* HC4 match finder */
	LZ4HC_Insert(hc4, ip);
	matchIndex = HashTable[LZ4HC_hashPtr(ip)];

	while ((matchIndex >= lowLimit)
		&& (nbAttempts)) {
		nbAttempts--;
		if (matchIndex >= dictLimit) {
			const BYTE * const match = base + matchIndex;

			if (*(match + ml) == *(ip + ml)
				&& (LZ4_read32(match) == LZ4_read32(ip))) {
				size_t const mlt = LZ4_count(ip + MINMATCH,
					match + MINMATCH, iLimit) + MINMATCH;

				if (mlt > ml) {
					ml = mlt;
					*matchpos = match;
				}
			}
		} else {
			const BYTE * const match = dictBase + matchIndex;

			if (LZ4_read32(match) == LZ4_read32(ip)) {
				size_t mlt;
				const BYTE *vLimit = ip
					+ (dictLimit - matchIndex);

				if (vLimit > iLimit)
					vLimit = iLimit;
				mlt = LZ4_count(ip + MINMATCH,
					match + MINMATCH, vLimit) + MINMATCH;
				if ((ip + mlt == vLimit)
					&& (vLimit < iLimit))
					mlt += LZ4_count(ip + mlt,
						base + dictLimit,
						iLimit);
				if (mlt > ml) {
					/* virtual matchpos */
					ml = mlt;
					*matchpos = base + matchIndex;
				}
			}
		}
		matchIndex -= DELTANEXTU16(matchIndex);
	}

	return (int)ml;
}

static FORCE_INLINE int LZ4HC_InsertAndGetWiderMatch(
	LZ4HC_CCtx_internal *hc4,
	const BYTE * const ip,
	const BYTE * const iLowLimit,
	const BYTE * const iHighLimit,
	int longest,
	const BYTE **matchpos,
	const BYTE **startpos,
	const int maxNbAttempts)
{
	U16 * const chainTable = hc4->chainTable;
	U32 * const HashTable = hc4->hashTable;
	const BYTE * const base = hc4->base;
	const U32 dictLimit = hc4->dictLimit;
	const BYTE * const lowPrefixPtr = base + dictLimit;
	const U32 lowLimit = (hc4->lowLimit + 64 * KB > (U32)(ip - base))
		? hc4->lowLimit
		: (U32)(ip - base) - (64 * KB - 1);
	const BYTE * const dictBase = hc4->dictBase;
	U32 matchIndex;
	int nbAttempts = maxNbAttempts;
	int delta = (int)(ip - iLowLimit);

	/* First Match */
	LZ4HC_Insert(hc4, ip);
	matchIndex = HashTable[LZ4HC_hashPtr(ip)];

	while ((matchIndex >= lowLimit)
		&& (nbAttempts)) {
		nbAttempts--;
		if (matchIndex >= dictLimit) {
			const BYTE *matchPtr = base + matchIndex;

			if (*(iLowLimit + longest)
				== *(matchPtr - delta + longest)) {
				if (LZ4_read32(matchPtr) == LZ4_read32(ip)) {
					int mlt = MINMATCH + LZ4_count(
						ip + MINMATCH,
						matchPtr + MINMATCH,
						iHighLimit);
					int back = 0;

					while ((ip + back > iLowLimit)
						&& (matchPtr + back > lowPrefixPtr)
						&& (ip[back - 1] == matchPtr[back - 1]))
						back--;

					mlt -= back;

					if (mlt > longest) {
						longest = (int)mlt;
						*matchpos = matchPtr + back;
						*startpos = ip + back;
					}
				}
			}
		} else {
			const BYTE * const matchPtr = dictBase + matchIndex;

			if (LZ4_read32(matchPtr) == LZ4_read32(ip)) {
				size_t mlt;
				int back = 0;
				const BYTE *vLimit = ip + (dictLimit - matchIndex);

				if (vLimit > iHighLimit)
					vLimit = iHighLimit;

				mlt = LZ4_count(ip + MINMATCH,
					matchPtr + MINMATCH, vLimit) + MINMATCH;

				if ((ip + mlt == vLimit) && (vLimit < iHighLimit))
					mlt += LZ4_count(ip + mlt, base + dictLimit,
						iHighLimit);
				while ((ip + back > iLowLimit)
					&& (matchIndex + back > lowLimit)
					&& (ip[back - 1] == matchPtr[back - 1]))
					back--;

				mlt -= back;

				if ((int)mlt > longest) {
					longest = (int)mlt;
					*matchpos = base + matchIndex + back;
					*startpos = ip + back;
				}
			}
		}

		matchIndex -= DELTANEXTU16(matchIndex);
	}

	return longest;
}

static FORCE_INLINE int LZ4HC_encodeSequence(
	const BYTE **ip,
	BYTE **op,
	const BYTE **anchor,
	int matchLength,
	const BYTE * const match,
	limitedOutput_directive limitedOutputBuffer,
	BYTE *oend)
{
	int length;
	BYTE *token;

	/* Encode Literal length */
	length = (int)(*ip - *anchor);
	token = (*op)++;

	if ((limitedOutputBuffer)
		&& ((*op + (length>>8)
			+ length + (2 + 1 + LASTLITERALS)) > oend)) {
		/* Check output limit */
		return 1;
	}
	if (length >= (int)RUN_MASK) {
		int len;

		*token = (RUN_MASK<<ML_BITS);
		len = length - RUN_MASK;
		for (; len > 254 ; len -= 255)
			*(*op)++ = 255;
		*(*op)++ = (BYTE)len;
	} else
		*token = (BYTE)(length<<ML_BITS);

	/* Copy Literals */
	LZ4_wildCopy(*op, *anchor, (*op) + length);
	*op += length;

	/* Encode Offset */
	LZ4_writeLE16(*op, (U16)(*ip - match));
	*op += 2;

	/* Encode MatchLength */
	length = (int)(matchLength - MINMATCH);

	if ((limitedOutputBuffer)
		&& (*op + (length>>8)
			+ (1 + LASTLITERALS) > oend)) {
		/* Check output limit */
		return 1;
	}

	if (length >= (int)ML_MASK) {
		*token += ML_MASK;
		length -= ML_MASK;

		for (; length > 509 ; length -= 510) {
			*(*op)++ = 255;
			*(*op)++ = 255;
		}

		if (length > 254) {
			length -= 255;
			*(*op)++ = 255;
		}

		*(*op)++ = (BYTE)length;
	} else
		*token += (BYTE)(length);

	/* Prepare next loop */
	*ip += matchLength;
	*anchor = *ip;

	return 0;
}

static int LZ4HC_compress_generic(
	LZ4HC_CCtx_internal *const ctx,
	const char * const source,
	char * const dest,
	int const inputSize,
	int const maxOutputSize,
	int compressionLevel,
	limitedOutput_directive limit
	)
{
	const BYTE *ip = (const BYTE *) source;
	const BYTE *anchor = ip;
	const BYTE * const iend = ip + inputSize;
	const BYTE * const mflimit = iend - MFLIMIT;
	const BYTE * const matchlimit = (iend - LASTLITERALS);

	BYTE *op = (BYTE *) dest;
	BYTE * const oend = op + maxOutputSize;

	unsigned int maxNbAttempts;
	int ml, ml2, ml3, ml0;
	const BYTE *ref = NULL;
	const BYTE *start2 = NULL;
	const BYTE *ref2 = NULL;
	const BYTE *start3 = NULL;
	const BYTE *ref3 = NULL;
	const BYTE *start0;
	const BYTE *ref0;

	/* init */
	if (compressionLevel > LZ4HC_MAX_CLEVEL)
		compressionLevel = LZ4HC_MAX_CLEVEL;
	if (compressionLevel < 1)
		compressionLevel = LZ4HC_DEFAULT_CLEVEL;
	maxNbAttempts = 1 << (compressionLevel - 1);
	ctx->end += inputSize;

	ip++;

	/* Main Loop */
	while (ip < mflimit) {
		ml = LZ4HC_InsertAndFindBestMatch(ctx, ip,
			matchlimit, (&ref), maxNbAttempts);
		if (!ml) {
			ip++;
			continue;
		}

		/* saved, in case we would skip too much */
		start0 = ip;
		ref0 = ref;
		ml0 = ml;

_Search2:
		if (ip + ml < mflimit)
			ml2 = LZ4HC_InsertAndGetWiderMatch(ctx,
				ip + ml - 2, ip + 0,
				matchlimit, ml, &ref2,
				&start2, maxNbAttempts);
		else
			ml2 = ml;

		if (ml2 == ml) {
			/* No better match */
			if (LZ4HC_encodeSequence(&ip, &op,
				&anchor, ml, ref, limit, oend))
				return 0;
			continue;
		}

		if (start0 < ip) {
			if (start2 < ip + ml0) {
				/* empirical */
				ip = start0;
				ref = ref0;
				ml = ml0;
			}
		}

		/* Here, start0 == ip */
		if ((start2 - ip) < 3) {
			/* First Match too small : removed */
			ml = ml2;
			ip = start2;
			ref = ref2;
			goto _Search2;
		}

_Search3:
		/*
		* Currently we have :
		* ml2 > ml1, and
		* ip1 + 3 <= ip2 (usually < ip1 + ml1)
		*/
		if ((start2 - ip) < OPTIMAL_ML) {
			int correction;
			int new_ml = ml;

			if (new_ml > OPTIMAL_ML)
				new_ml = OPTIMAL_ML;
			if (ip + new_ml > start2 + ml2 - MINMATCH)
				new_ml = (int)(start2 - ip) + ml2 - MINMATCH;

			correction = new_ml - (int)(start2 - ip);

			if (correction > 0) {
				start2 += correction;
				ref2 += correction;
				ml2 -= correction;
			}
		}
		/*
		 * Now, we have start2 = ip + new_ml,
		 * with new_ml = min(ml, OPTIMAL_ML = 18)
		 */

		if (start2 + ml2 < mflimit)
			ml3 = LZ4HC_InsertAndGetWiderMatch(ctx,
				start2 + ml2 - 3, start2,
				matchlimit, ml2, &ref3, &start3,
				maxNbAttempts);
		else
			ml3 = ml2;

		if (ml3 == ml2) {
			/* No better match : 2 sequences to encode */
			/* ip & ref are known; Now for ml */
			if (start2 < ip + ml)
				ml = (int)(start2 - ip);
			/* Now, encode 2 sequences */
			if (LZ4HC_encodeSequence(&ip, &op, &anchor,
				ml, ref, limit, oend))
				return 0;
			ip = start2;
			if (LZ4HC_encodeSequence(&ip, &op, &anchor,
				ml2, ref2, limit, oend))
				return 0;
			continue;
		}

		if (start3 < ip + ml + 3) {
			/* Not enough space for match 2 : remove it */
			if (start3 >= (ip + ml)) {
				/* can write Seq1 immediately
				 * ==> Seq2 is removed,
				 * so Seq3 becomes Seq1
				 */
				if (start2 < ip + ml) {
					int correction = (int)(ip + ml - start2);

					start2 += correction;
					ref2 += correction;
					ml2 -= correction;
					if (ml2 < MINMATCH) {
						start2 = start3;
						ref2 = ref3;
						ml2 = ml3;
					}
				}

				if (LZ4HC_encodeSequence(&ip, &op, &anchor,
					ml, ref, limit, oend))
					return 0;
				ip = start3;
				ref = ref3;
				ml = ml3;

				start0 = start2;
				ref0 = ref2;
				ml0 = ml2;
				goto _Search2;
			}

			start2 = start3;
			ref2 = ref3;
			ml2 = ml3;
			goto _Search3;
		}

		/*
		* OK, now we have 3 ascending matches;
		* let's write at least the first one
		* ip & ref are known; Now for ml
		*/
		if (start2 < ip + ml) {
			if ((start2 - ip) < (int)ML_MASK) {
				int correction;

				if (ml > OPTIMAL_ML)
					ml = OPTIMAL_ML;
				if (ip + ml > start2 + ml2 - MINMATCH)
					ml = (int)(start2 - ip) + ml2 - MINMATCH;
				correction = ml - (int)(start2 - ip);
				if (correction > 0) {
					start2 += correction;
					ref2 += correction;
					ml2 -= correction;
				}
			} else
				ml = (int)(start2 - ip);
		}
		if (LZ4HC_encodeSequence(&ip, &op, &anchor, ml,
			ref, limit, oend))
			return 0;

		ip = start2;
		ref = ref2;
		ml = ml2;

		start2 = start3;
		ref2 = ref3;
		ml2 = ml3;

		goto _Search3;
	}

	/* Encode Last Literals */
	{
		int lastRun = (int)(iend - anchor);

		if ((limit)
			&& (((char *)op - dest) + lastRun + 1
				+ ((lastRun + 255 - RUN_MASK)/255)
					> (U32)maxOutputSize)) {
			/* Check output limit */
			return 0;
		}
		if (lastRun >= (int)RUN_MASK) {
			*op++ = (RUN_MASK<<ML_BITS);
			lastRun -= RUN_MASK;
			for (; lastRun > 254 ; lastRun -= 255)
				*op++ = 255;
			*op++ = (BYTE) lastRun;
		} else
			*op++ = (BYTE)(lastRun<<ML_BITS);
		memcpy(op, anchor, iend - anchor);
		op += iend - anchor;
	}

	/* End */
	return (int) (((char *)op) - dest);
}

static int LZ4_compress_HC_extStateHC(
	void *state,
	const char *src,
	char *dst,
	int srcSize,
	int maxDstSize,
	int compressionLevel)
{
	LZ4HC_CCtx_internal *ctx = &((LZ4_streamHC_t *)state)->internal_donotuse;

	if (((size_t)(state)&(sizeof(void *) - 1)) != 0) {
		/* Error : state is not aligned
		 * for pointers (32 or 64 bits)
		 */
		return 0;
	}

	LZ4HC_init(ctx, (const BYTE *)src);

	if (maxDstSize < LZ4_compressBound(srcSize))
		return LZ4HC_compress_generic(ctx, src, dst,
			srcSize, maxDstSize, compressionLevel, limitedOutput);
	else
		return LZ4HC_compress_generic(ctx, src, dst,
			srcSize, maxDstSize, compressionLevel, noLimit);
}

int LZ4_compress_HC(const char *src, char *dst, int srcSize,
	int maxDstSize, int compressionLevel, void *wrkmem)
{
	return LZ4_compress_HC_extStateHC(wrkmem, src, dst,
		srcSize, maxDstSize, compressionLevel);
}
EXPORT_SYMBOL(LZ4_compress_HC);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("LZ4 HC compressor");
