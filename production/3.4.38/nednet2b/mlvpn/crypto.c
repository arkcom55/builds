/*
 * Copyright (c) 2015, Laurent COUSTET <ed@zehome.com>
 *
 * All rights reserved.
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE REGENTS AND CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "includes.h"

#if 0
static unsigned char key[crypto_secretbox_KEYBYTES];

//#define ARK

#ifdef ARK
#include "log.h"
static char hbuf[256];

static char* binToString(unsigned char* bin, int binLen) {
    int len = 0;
    
    for (int i = 0; i < binLen; i++) {
        unsigned char c = bin[i];
        sprintf(hbuf + len, "%02X", c);
        len += 2;
    }
    
    hbuf[len] = 0;
    return hbuf;
}
#endif

int crypto_init()
{
    return sodium_init();
}

int crypto_set_password(const char *password,
                        unsigned long long password_len)
{
    return crypto_generichash(
               key, sizeof(key), (unsigned char *)password, password_len, NULL, 0);
}

int crypto_encrypt(unsigned char *c, const unsigned char *m,
                   unsigned long long mlen,
                   const unsigned char *nonce)
{
#ifdef ARK
    log_debug("ARK", "crypto_encrypt:");
    log_debug("ARK", "      key: %s", binToString(key, crypto_secretbox_KEYBYTES));
    log_debug("ARK", "    nonce: %s", binToString((unsigned char*)nonce, crypto_NONCEBYTES));
#endif
    return crypto_secretbox_easy(c, m, mlen, nonce, key);
}

int crypto_decrypt(unsigned char *m, const unsigned char *c,
                   unsigned long long clen,
                   const unsigned char *nonce)
{
#ifdef ARK
    log_debug("ARK", "crypto_decrypt:");
    log_debug("ARK", "      key: %s", binToString(key, crypto_secretbox_KEYBYTES));
    log_debug("ARK", "    nonce: %s", binToString((unsigned char*)nonce, crypto_NONCEBYTES));
#endif
    return crypto_secretbox_open_easy(m, c, clen, nonce, key);
}
#endif