/* $USAGI: md5.h,v 1.2 2000/11/02 11:59:25 yoshfuji Exp $ */
/*	$KAME: md5.h,v 1.4 2000/03/27 04:36:22 sumikawa Exp $	*/
/*	$Id: md5.h,v 1.3 2006/01/17 17:40:45 paul Exp $ */

/*
 * Copyright (C) 2004 6WIND
 *                          <Vincent.Jardin@6WIND.com>
 * All rights reserved.
 *
 * This MD5 code is Big endian and Little Endian compatible.
 */

/*
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _LIBZEBRA_MD5_H_
#define _LIBZEBRA_MD5_H_

#define MD5_BUFLEN 64

typedef struct {
	union {
		uint32_t md5_state32[4];
		uint8_t md5_state8[16];
	} md5_st;

#define md5_sta md5_st.md5_state32[0]
#define md5_stb md5_st.md5_state32[1]
#define md5_stc md5_st.md5_state32[2]
#define md5_std md5_st.md5_state32[3]
#define md5_st8 md5_st.md5_state8

	union {
		uint64_t md5_count64;
		uint8_t md5_count8[8];
	} md5_count;

#define md5_n md5_count.md5_count64
#define md5_n8 md5_count.md5_count8

	uint md5_i;
	uint8_t md5_buf[MD5_BUFLEN];
} md5_ctxt;

extern void md5_init(md5_ctxt *);
extern void md5_loop(md5_ctxt *, const void *, u_int);
extern void md5_pad(md5_ctxt *);
extern void md5_result(uint8_t *, md5_ctxt *);

/* compatibility */
#define MD5_CTX md5_ctxt
#define MD5Init(x) md5_init((x))
#define MD5Update(x, y, z) md5_loop((x), (y), (z))
#define MD5Final(x, y) \
	do { \
		md5_pad((y)); \
		md5_result((x), (y)); \
	} while(0)

/* From RFC 2104 */
void hmac_md5(unsigned char *text, int text_len, unsigned char *key, int key_len, uint8_t *digest);

#endif /* ! _LIBZEBRA_MD5_H_*/
