/*
  eXosip - This is the eXtended osip library.
  Copyright (C) 2001-2018 Aymeric MOIZARD amoizard@antisip.com
  
  eXosip is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
  
  eXosip is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

  In addition, as a special exception, the copyright holders give
  permission to link the code of portions of this program with the
  OpenSSL library under certain conditions as described in each
  individual source file, and distribute linked combinations
  including the two.
  You must obey the GNU General Public License in all respects
  for all of the code used other than OpenSSL.  If you modify
  file(s) with this exception, you may extend this exception to your
  version of the file(s), but you are not obligated to do so.  If you
  do not wish to do so, delete this exception statement from your
  version.  If you delete this exception statement from all source
  files in the program, then also delete it here.
*/

#ifdef WIN32
#ifndef UNICODE
#define UNICODE
#endif
#endif

#include "eXosip2.h"
#include "eXtransport.h"

#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#ifdef HAVE_MSTCPIP_H
#include <Mstcpip.h>
#endif

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#ifdef HAVE_WINCRYPT_H
#include <wincrypt.h>
#endif

#if !defined(_WIN32_WCE)
#include <errno.h>
#endif

#if defined(HAVE_NETINET_TCP_H)
#include <netinet/tcp.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#if defined(HAVE_WINSOCK2_H)
#define strerror(X) "-1"
#define ex_errno WSAGetLastError()
#define is_wouldblock_error(r) ((r)==WSAEINTR||(r)==WSAEWOULDBLOCK)
#define is_connreset_error(r) ((r)==WSAECONNRESET || (r)==WSAECONNABORTED || (r)==WSAETIMEDOUT || (r)==WSAENETRESET || (r)==WSAENOTCONN)
#else
#define ex_errno errno
#endif
#ifndef is_wouldblock_error
#define is_wouldblock_error(r) ((r)==EINTR||(r)==EWOULDBLOCK||(r)==EAGAIN)
#define is_connreset_error(r) ((r)==ECONNRESET || (r)==ECONNABORTED || (r)==ETIMEDOUT || (r)==ENETRESET || (r)==ENOTCONN)
#endif

#ifdef HAVE_OPENSSL_SSL_H

#include <openssl/opensslconf.h>
#include <openssl/opensslv.h>

#define ex_verify_depth 10
#include <openssl/bn.h>
#ifndef OPENSSL_NO_DH
#include <openssl/dh.h>
#endif
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#ifndef OPENSSL_NO_RSA
#include <openssl/rsa.h>
#endif
#include <openssl/tls1.h>
#include <openssl/x509.h>
#if !(OPENSSL_VERSION_NUMBER < 0x10002000L)
#include <openssl/x509v3.h>
#endif

#define SSLDEBUG 1
/*#define PATH "D:/conf/"

#define PASSWORD "23-+Wert"
#define CLIENT_KEYFILE PATH"ckey.pem"
#define CLIENT_CERTFILE PATH"c.pem"
#define SERVER_KEYFILE PATH"skey.pem"
#define SERVER_CERTFILE PATH"s.pem"
#define CA_LIST PATH"cacert.pem"
#define RANDOM  PATH"random.pem"
#define DHFILE PATH"dh1024.pem"*/

#ifdef __APPLE_CC__
#include "TargetConditionals.h"
#endif

#if defined(__APPLE__) && (TARGET_OS_IPHONE==0)
#include <CoreFoundation/CoreFoundation.h>
#include <CoreServices/CoreServices.h>
#include <Security/Security.h>
#endif

#if TARGET_OS_IPHONE
#include <CoreFoundation/CFStream.h>
#include <CFNetwork/CFSocketStream.h>
#define MULTITASKING_ENABLED
#endif

#if OPENSSL_VERSION_NUMBER < 0x10100000L
#define X509_STORE_get0_param(store) (store->param)
#endif

SSL_CTX *initialize_client_ctx (struct eXosip_t *excontext, eXosip_tls_ctx_t * client_ctx, int transport, const char *sni_servernameindication);

SSL_CTX *initialize_server_ctx (eXosip_tls_ctx_t * srv_ctx, int transport);

int verify_cb (int preverify_ok, X509_STORE_CTX * store);


/* persistent connection */
struct _tls_stream {
  int socket;
  struct sockaddr ai_addr;
  socklen_t ai_addrlen;
  char sni_servernameindication[256];
  char remote_ip[65];
  int remote_port;
  char *previous_content;
  int previous_content_len;
  SSL *ssl_conn;
  SSL_CTX *ssl_ctx;
  int ssl_state;
  char *buf;                    /* recv buffer */
  size_t bufsize;               /* allocated size of buf */
  size_t buflen;                /* current length of buf */
  char *sendbuf;                /* send buffer */
  size_t sendbufsize;
  size_t sendbuflen;
#ifdef MULTITASKING_ENABLED
  CFReadStreamRef readStream;
  CFWriteStreamRef writeStream;
#endif
  char natted_ip[65];
  int natted_port;
  int ephemeral_port;
  int invalid;
  int is_server;
  time_t tcp_max_timeout;
  time_t tcp_inprogress_max_timeout;
  char reg_call_id[64];
};

#ifndef SOCKET_TIMEOUT
/* when stream has sequence error: */
/* having SOCKET_TIMEOUT > 0 helps the system to recover */
#define SOCKET_TIMEOUT 0
#endif

#ifndef EXOSIP_MAX_SOCKETS
#define EXOSIP_MAX_SOCKETS 200
#endif

struct eXtltls {

  int tls_socket;
  struct sockaddr_storage ai_addr;
  int ai_addr_len;

  SSL_CTX *server_ctx;
  SSL_CTX *client_ctx;

  struct _tls_stream socket_tab[EXOSIP_MAX_SOCKETS];
};

static int
tls_tl_init (struct eXosip_t *excontext)
{
  struct eXtltls *reserved = (struct eXtltls *) osip_malloc (sizeof (struct eXtltls));

  if (reserved == NULL)
    return OSIP_NOMEM;
  reserved->tls_socket = 0;
  reserved->server_ctx = NULL;
  reserved->client_ctx = NULL;
  memset (&reserved->ai_addr, 0, sizeof (struct sockaddr_storage));
  reserved->ai_addr_len = 0;
  memset (&reserved->socket_tab, 0, sizeof (struct _tls_stream) * EXOSIP_MAX_SOCKETS);

  excontext->eXtltls_reserved = reserved;
  return OSIP_SUCCESS;
}

static void
_tls_tl_close_sockinfo (struct _tls_stream *sockinfo)
{
  if (sockinfo->socket > 0) {
    if (sockinfo->ssl_conn != NULL) {
      SSL_shutdown (sockinfo->ssl_conn);
      SSL_shutdown (sockinfo->ssl_conn);
      SSL_free (sockinfo->ssl_conn);
    }
    if (sockinfo->ssl_ctx != NULL)
      SSL_CTX_free (sockinfo->ssl_ctx);
    _eXosip_closesocket (sockinfo->socket);
  }
  if (sockinfo->buf != NULL)
    osip_free (sockinfo->buf);
  if (sockinfo->sendbuf != NULL)
    osip_free (sockinfo->sendbuf);
#ifdef MULTITASKING_ENABLED
  if (sockinfo->readStream != NULL) {
    CFReadStreamClose (sockinfo->readStream);
    CFRelease (sockinfo->readStream);
  }
  if (sockinfo->writeStream != NULL) {
    CFWriteStreamClose (sockinfo->writeStream);
    CFRelease (sockinfo->writeStream);
  }
#endif
  memset (sockinfo, 0, sizeof (*sockinfo));
}

static int
tls_tl_free (struct eXosip_t *excontext)
{
  struct eXtltls *reserved = (struct eXtltls *) excontext->eXtltls_reserved;
  int pos;

  if (reserved == NULL)
    return OSIP_SUCCESS;

  if (reserved->server_ctx != NULL)
    SSL_CTX_free (reserved->server_ctx);
  reserved->server_ctx = NULL;

  if (reserved->client_ctx != NULL)
    SSL_CTX_free (reserved->client_ctx);
  reserved->client_ctx = NULL;

  for (pos = 0; pos < EXOSIP_MAX_SOCKETS; pos++) {
    _tls_tl_close_sockinfo (&reserved->socket_tab[pos]);
  }

#if OPENSSL_VERSION_NUMBER < 0x10100000L
#if OPENSSL_VERSION_NUMBER >= 0x10000000L
  ERR_remove_thread_state (NULL);
#else
  ERR_remove_state (0);
#endif
#endif

  memset (&reserved->socket_tab, 0, sizeof (struct _tls_stream) * EXOSIP_MAX_SOCKETS);

  memset (&reserved->ai_addr, 0, sizeof (struct sockaddr_storage));
  reserved->ai_addr_len = 0;
  if (reserved->tls_socket > 0)
    _eXosip_closesocket (reserved->tls_socket);
  reserved->tls_socket = 0;

  osip_free (reserved);
  excontext->eXtltls_reserved = NULL;
  return OSIP_SUCCESS;
}

static int
tls_tl_reset (struct eXosip_t *excontext)
{
  struct eXtltls *reserved = (struct eXtltls *) excontext->eXtltls_reserved;
  int pos;

  if (reserved == NULL) {
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "wrong state: create transport layer first\n"));
    return OSIP_WRONG_STATE;
  }

  for (pos = 0; pos < EXOSIP_MAX_SOCKETS; pos++) {
    if (reserved->socket_tab[pos].socket > 0)
      reserved->socket_tab[pos].invalid = 1;
  }
  return OSIP_SUCCESS;
}

static void
tls_dump_cert_info (const char *s, X509 * cert)
{
  char *subj;
  char *issuer;

  subj = X509_NAME_oneline (X509_get_subject_name (cert), 0, 0);
  issuer = X509_NAME_oneline (X509_get_issuer_name (cert), 0, 0);

  OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "%s subject:%s\n", s ? s : "", subj));
  OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "%s issuer: %s\n", s ? s : "", issuer));
  OPENSSL_free (subj);
  OPENSSL_free (issuer);
}

static int
_tls_add_certificates (SSL_CTX * ctx)
{
  int count = 0;

#ifdef HAVE_WINCRYPT_H
  PCCERT_CONTEXT pCertCtx;
  X509 *cert = NULL;
  HCERTSTORE hStore = CertOpenSystemStore (0, L"CA");
  X509_STORE *x509_store;

  for (pCertCtx = CertEnumCertificatesInStore (hStore, NULL); pCertCtx != NULL; pCertCtx = CertEnumCertificatesInStore (hStore, pCertCtx)) {
    cert = d2i_X509 (NULL, (const unsigned char **) &pCertCtx->pbCertEncoded, pCertCtx->cbCertEncoded);
    if (cert == NULL) {
      continue;
    }
    /*tls_dump_cert_info("CA", cert); */

    x509_store = SSL_CTX_get_cert_store (ctx);
    if (x509_store == NULL) {
      X509_free (cert);
      continue;
    }

    if (!X509_STORE_add_cert (x509_store, cert)) {
      X509_free (cert);
      continue;
    }
    count++;
    X509_free (cert);
  }

  CertCloseStore (hStore, 0);

  hStore = CertOpenSystemStore (0, L"ROOT");

  for (pCertCtx = CertEnumCertificatesInStore (hStore, NULL); pCertCtx != NULL; pCertCtx = CertEnumCertificatesInStore (hStore, pCertCtx)) {
    cert = d2i_X509 (NULL, (const unsigned char **) &pCertCtx->pbCertEncoded, pCertCtx->cbCertEncoded);
    if (cert == NULL) {
      continue;
    }
    /*tls_dump_cert_info("ROOT", cert); */
    x509_store = SSL_CTX_get_cert_store (ctx);
    if (x509_store == NULL) {
      X509_free (cert);
      continue;
    }

    if (!X509_STORE_add_cert (x509_store, cert)) {
      X509_free (cert);
      continue;
    }
    count++;
    X509_free (cert);
  }

  CertCloseStore (hStore, 0);

  hStore = CertOpenSystemStore (0, L"MY");

  for (pCertCtx = CertEnumCertificatesInStore (hStore, NULL); pCertCtx != NULL; pCertCtx = CertEnumCertificatesInStore (hStore, pCertCtx)) {
    cert = d2i_X509 (NULL, (const unsigned char **) &pCertCtx->pbCertEncoded, pCertCtx->cbCertEncoded);
    if (cert == NULL) {
      continue;
    }
    /*tls_dump_cert_info("MY", cert); */
    x509_store = SSL_CTX_get_cert_store (ctx);
    if (x509_store == NULL) {
      X509_free (cert);
      continue;
    }

    if (!X509_STORE_add_cert (x509_store, cert)) {
      X509_free (cert);
      continue;
    }
    count++;
    X509_free (cert);
  }

  CertCloseStore (hStore, 0);

  hStore = CertOpenSystemStore (0, L"Trustedpublisher");

  for (pCertCtx = CertEnumCertificatesInStore (hStore, NULL); pCertCtx != NULL; pCertCtx = CertEnumCertificatesInStore (hStore, pCertCtx)) {
    cert = d2i_X509 (NULL, (const unsigned char **) &pCertCtx->pbCertEncoded, pCertCtx->cbCertEncoded);
    if (cert == NULL) {
      continue;
    }
    /*tls_dump_cert_info("Trustedpublisher", cert); */
    x509_store = SSL_CTX_get_cert_store (ctx);
    if (x509_store == NULL) {
      X509_free (cert);
      continue;
    }

    if (!X509_STORE_add_cert (x509_store, cert)) {
      X509_free (cert);
      continue;
    }
    count++;
    X509_free (cert);
  }

  CertCloseStore (hStore, 0);
#elif defined(__APPLE__) && (TARGET_OS_IPHONE==0)
  SecKeychainSearchRef pSecKeychainSearch = NULL;
  SecKeychainRef pSecKeychain;
  OSStatus status = noErr;
  X509 *cert = NULL;
  SInt32 osx_version = 0;
  X509_STORE *x509_store;

  if (Gestalt (gestaltSystemVersion, &osx_version) != noErr) {
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "macosx certificate store: can't get osx version"));
    return 0;
  }
  if (osx_version >= 0x1050) {
    /* Leopard store location */
    status = SecKeychainOpen ("/System/Library/Keychains/SystemRootCertificates.keychain", &pSecKeychain);
  }
  else {
    /* Tiger and below store location */
    status = SecKeychainOpen ("/System/Library/Keychains/X509Anchors", &pSecKeychain);
  }
  if (status != noErr) {
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "macosx certificate store: can't get osx version"));
    return 0;
  }

  status = SecKeychainSearchCreateFromAttributes (pSecKeychain, kSecCertificateItemClass, NULL, &pSecKeychainSearch);
  for (;;) {
    SecKeychainItemRef pSecKeychainItem = nil;

    status = SecKeychainSearchCopyNext (pSecKeychainSearch, &pSecKeychainItem);
    if (status == errSecItemNotFound) {
      break;
    }

    if (status == noErr) {
      void *_pCertData;
      UInt32 _pCertLength;

      status = SecKeychainItemCopyAttributesAndData (pSecKeychainItem, NULL, NULL, NULL, &_pCertLength, &_pCertData);

      if (status == noErr && _pCertData != NULL) {
        unsigned char *ptr;

        ptr = _pCertData;       /*required because d2i_X509 is modifying pointer */
        cert = d2i_X509 (NULL, (const unsigned char **) &ptr, _pCertLength);
        if (cert == NULL) {
          continue;
        }
        /*tls_dump_cert_info("ROOT", cert); */
        x509_store = SSL_CTX_get_cert_store (ctx);
        if (x509_store == NULL) {
          X509_free (cert);
          continue;
        }

        if (!X509_STORE_add_cert (x509_store, cert)) {
          X509_free (cert);
          continue;
        }
        count++;
        X509_free (cert);

        status = SecKeychainItemFreeAttributesAndData (NULL, _pCertData);
      }
    }

    if (pSecKeychainItem != NULL)
      CFRelease (pSecKeychainItem);

    if (status != noErr) {
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "macosx certificate store: can't add certificate (%i)", status));
    }
  }

  CFRelease (pSecKeychainSearch);
  CFRelease (pSecKeychain);

#endif
  return count;
}

int
verify_cb (int preverify_ok, X509_STORE_CTX * store)
{
  char buf[256];
  X509 *err_cert;
  int err, depth;

  err_cert = X509_STORE_CTX_get_current_cert (store);
  err = X509_STORE_CTX_get_error (store);
  depth = X509_STORE_CTX_get_error_depth (store);

  X509_NAME_oneline (X509_get_subject_name (err_cert), buf, 256);

  if (depth > ex_verify_depth /* depth -1 */ ) {
    preverify_ok = 0;
    err = X509_V_ERR_CERT_CHAIN_TOO_LONG;
    X509_STORE_CTX_set_error (store, err);
  }
  if (!preverify_ok) {
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "verify error:num=%d:%s:depth=%d:%s\n", err, X509_verify_cert_error_string (err), depth, buf));
  }
  else {
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO1, NULL, "depth=%d:%s\n", depth, buf));
  }
  /*
   * At this point, err contains the last verification error. We can use
   * it for something special
   */
  if (!preverify_ok && (err == X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT)) {
    X509 *current_cert = X509_STORE_CTX_get_current_cert (store);

    X509_NAME_oneline (X509_get_issuer_name (current_cert), buf, 256);
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "issuer= %s\n", buf));
  }

  return preverify_ok;

#if 0
  if (!preverify_ok && (err == X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN)) {
    X509_NAME_oneline (X509_get_issuer_name (store->current_cert), buf, 256);
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "issuer= %s\n", buf));
    preverify_ok = 1;
    X509_STORE_CTX_set_error (store, X509_V_OK);
  }

  if (!preverify_ok && (err == X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT)) {
    X509_NAME_oneline (X509_get_issuer_name (store->current_cert), buf, 256);
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "issuer= %s\n", buf));
    preverify_ok = 1;
    X509_STORE_CTX_set_error (store, X509_V_OK);
  }

  if (!preverify_ok && (err == X509_V_ERR_CERT_HAS_EXPIRED)) {
    X509_NAME_oneline (X509_get_issuer_name (store->current_cert), buf, 256);
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "issuer= %s\n", buf));
    preverify_ok = 1;
    X509_STORE_CTX_set_error (store, X509_V_OK);
  }

  if (!preverify_ok && (err == X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY)) {
    X509_NAME_oneline (X509_get_issuer_name (store->current_cert), buf, 256);
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "issuer= %s\n", buf));
    preverify_ok = 1;
    X509_STORE_CTX_set_error (store, X509_V_OK);
  }

  if (!preverify_ok && (err == X509_V_ERR_CERT_UNTRUSTED)) {
    X509_NAME_oneline (X509_get_issuer_name (store->current_cert), buf, 256);
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "issuer= %s\n", buf));
    preverify_ok = 1;
    X509_STORE_CTX_set_error (store, X509_V_OK);
  }

  if (!preverify_ok && (err == X509_V_ERR_UNABLE_TO_VERIFY_LEAF_SIGNATURE)) {
    X509_NAME_oneline (X509_get_issuer_name (store->current_cert), buf, 256);
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "issuer= %s\n", buf));
    preverify_ok = 1;
    X509_STORE_CTX_set_error (store, X509_V_OK);
  }

  preverify_ok = 1;             /* configured to accept anyway! */
  return preverify_ok;
#endif
}

static int
password_cb (char *buf, int num, int rwflag, void *userdata)
{
  char *passwd = (char *) userdata;

  if (passwd == NULL || passwd[0] == '\0') {
    return OSIP_SUCCESS;
  }
  strncpy (buf, passwd, num);
  buf[num - 1] = '\0';
  return (int) strlen (buf);
}

static void
load_dh_params (SSL_CTX * ctx, char *file)
{
#ifndef OPENSSL_NO_DH
  DH *ret = 0;
  BIO *bio;

  if ((bio = BIO_new_file (file, "r")) == NULL) {
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "eXosip: Couldn't open DH file!\n"));
  }
  else {
    ret = PEM_read_bio_DHparams (bio, NULL, NULL, NULL);
    BIO_free (bio);
    if (SSL_CTX_set_tmp_dh (ctx, ret) < 0)
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "eXosip: Couldn't set DH param!\n"));
  }
#endif
}

static void
build_dh_params (SSL_CTX * ctx)
{
#ifndef OPENSSL_NO_DH
  int codes = 0;
  DH *dh = DH_new ();

  if (!dh) {
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "eXosip: DH_new failed!\n"));
    return;
  }

  OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO3, NULL, "eXosip: building DH params!\n"));
  if (!DH_generate_parameters_ex (dh, 128, DH_GENERATOR_2, 0)) {
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "eXosip: DH_generate_parameters_ex failed!\n"));
    DH_free (dh);
    return;
  }
  OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO3, NULL, "eXosip: DH params built!\n"));

  if (!DH_check (dh, &codes)) {
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "eXosip: DH_check failed!\n"));
    DH_free (dh);
    return;
  }
  if (!DH_generate_key (dh)) {
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "eXosip: DH_generate_key failed!\n"));
    DH_free (dh);
    return;
  }
  SSL_CTX_set_tmp_dh (ctx, dh);
  DH_free (dh);

  return;
#endif
}

#ifndef OPENSSL_NO_RSA
static RSA *
__RSA_generate_key (int bits, unsigned long e_value, void (*callback) (int, int, void *), void *cb_arg)
{
  int i;
  RSA *rsa = RSA_new ();
  BIGNUM *e = BN_new ();

  if (!rsa || !e)
    goto err;

  i = BN_set_word (e, e_value);
  if (i != 1)
    goto err;

  if (RSA_generate_key_ex (rsa, bits, e, NULL)) {
    BN_free (e);
    return rsa;
  }
err:
  if (e)
    BN_free (e);
  if (rsa)
    RSA_free (rsa);
  return 0;
}

static void
generate_eph_rsa_key (SSL_CTX * ctx)
{
  RSA *rsa;

  rsa = __RSA_generate_key (512, RSA_F4, NULL, NULL);

  if (rsa != NULL) {
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    if (!SSL_CTX_set_tmp_rsa (ctx, rsa))
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "eXosip: Couldn't set RSA key!\n"));
#endif

    RSA_free (rsa);
  }
}
#endif

eXosip_tls_ctx_error
eXosip_set_tls_ctx (struct eXosip_t *excontext, eXosip_tls_ctx_t * ctx)
{
  eXosip_tls_credentials_t *ownClient = &excontext->eXosip_tls_ctx_params.client;
  eXosip_tls_credentials_t *ownServer = &excontext->eXosip_tls_ctx_params.server;

  eXosip_tls_credentials_t *client = &ctx->client;
  eXosip_tls_credentials_t *server = &ctx->server;

  /* check if public AND private keys are valid */
  if (client->cert[0] == '\0' && client->priv_key[0] != '\0') {
    /* no, one is missing */
    return TLS_ERR_MISSING_AUTH_PART;
  }
  if (client->cert[0] != '\0' && client->priv_key[0] == '\0') {
    /* no, one is missing */
    return TLS_ERR_MISSING_AUTH_PART;
  }
  /* check if public AND private keys are valid */
  if (server->cert[0] == '\0' && server->priv_key[0] != '\0') {
    /* no, one is missing */
    return TLS_ERR_MISSING_AUTH_PART;
  }
  if (server->cert[0] != '\0' && server->priv_key[0] == '\0') {
    /* no, one is missing */
    return TLS_ERR_MISSING_AUTH_PART;
  }

  /* clean up configuration */
  memset (&excontext->eXosip_tls_ctx_params, 0, sizeof (eXosip_tls_ctx_t));

  if (client->public_key_pinned[0] != '\0') {
    snprintf (ownClient->public_key_pinned, sizeof (ownClient->public_key_pinned), "%s", client->public_key_pinned);
  }

  /* check if client has own certificate */
  if (client->cert[0] != '\0') {
    snprintf (ownClient->cert, sizeof (ownClient->cert), "%s", client->cert);
    snprintf (ownClient->priv_key, sizeof (ownClient->priv_key), "%s", client->priv_key);
    snprintf (ownClient->priv_key_pw, sizeof (ownClient->priv_key_pw), "%s", client->priv_key_pw);
  }
  /* check if server has own certificate */
  if (server->cert[0] != '\0') {
    snprintf (ownServer->cert, sizeof (ownServer->cert), "%s", server->cert);
    snprintf (ownServer->priv_key, sizeof (ownServer->priv_key), "%s", server->priv_key);
    snprintf (ownServer->priv_key_pw, sizeof (ownServer->priv_key_pw), "%s", server->priv_key_pw);
  }

  snprintf (excontext->eXosip_tls_ctx_params.dh_param, sizeof (ctx->dh_param), "%s", ctx->dh_param);
  snprintf (excontext->eXosip_tls_ctx_params.random_file, sizeof (ctx->random_file), "%s", ctx->random_file);
  snprintf (excontext->eXosip_tls_ctx_params.root_ca_cert, sizeof (ctx->root_ca_cert), "%s", ctx->root_ca_cert);

  return TLS_OK;
}

eXosip_tls_ctx_error
eXosip_tls_verify_certificate (struct eXosip_t * excontext, int _tls_verify_client_certificate)
{
  excontext->tls_verify_client_certificate = _tls_verify_client_certificate;
  return TLS_OK;
}

static void
_tls_load_trusted_certificates (eXosip_tls_ctx_t * exosip_tls_cfg, SSL_CTX * ctx)
{
  char *caFile = 0, *caFolder = 0;

  if (_tls_add_certificates (ctx) <= 0) {
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_WARNING, NULL, "no system certificate loaded\n"));
  }

  if (exosip_tls_cfg->root_ca_cert[0] == '\0')
    return;

  {
#ifdef WIN32
    WIN32_FIND_DATA FileData;
    HANDLE hSearch;
    char szDirPath[1024];
    WCHAR wUnicodeDirPath[2048];

    snprintf (szDirPath, sizeof (szDirPath), "%s", exosip_tls_cfg->root_ca_cert);

    MultiByteToWideChar (CP_UTF8, 0, szDirPath, -1, wUnicodeDirPath, 2048);
    hSearch = FindFirstFileEx (wUnicodeDirPath, FindExInfoStandard, &FileData, FindExSearchNameMatch, NULL, 0);
    if (hSearch != INVALID_HANDLE_VALUE) {
      if ((FileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY)
        caFolder = exosip_tls_cfg->root_ca_cert;
      else
        caFile = exosip_tls_cfg->root_ca_cert;
    }
    else {
      caFile = exosip_tls_cfg->root_ca_cert;
    }
#else
    int fd = open (exosip_tls_cfg->root_ca_cert, O_RDONLY);

    if (fd >= 0) {
      struct stat fileStat;

      if (fstat (fd, &fileStat) < 0) {

      }
      else {
        if (S_ISDIR (fileStat.st_mode)) {
          caFolder = exosip_tls_cfg->root_ca_cert;
        }
        else {
          caFile = exosip_tls_cfg->root_ca_cert;
        }
      }
      close (fd);
    }
#endif
  }

  if (exosip_tls_cfg->root_ca_cert[0] == '\0') {
  }
  else {
    if (SSL_CTX_load_verify_locations (ctx, caFile, caFolder)) {
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "eXosip: trusted CA PEM file loaded [%s]\n", exosip_tls_cfg->root_ca_cert));
    }
    else {
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "eXosip: Couldn't read trusted CA list [%s]\n", exosip_tls_cfg->root_ca_cert));
    }
  }
}

static void
_tls_use_certificate_private_key (const char *log, eXosip_tls_credentials_t * xtc, SSL_CTX * ctx)
{
  /* load from file name in PEM files */
  if (xtc->cert[0] != '\0' && xtc->priv_key[0] != '\0') {
    if (xtc->priv_key_pw[0] != '\0') {
      SSL_CTX_set_default_passwd_cb_userdata (ctx, (void *) xtc->priv_key_pw);
      SSL_CTX_set_default_passwd_cb (ctx, password_cb);
    }

    /* Load our keys and certificates */
    if (SSL_CTX_use_certificate_file (ctx, xtc->cert, SSL_FILETYPE_ASN1)) {
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "eXosip: %s certificate ASN1 file loaded [%s]!\n", log, xtc->cert));
    }
    else if (SSL_CTX_use_certificate_file (ctx, xtc->cert, SSL_FILETYPE_PEM)) {
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "eXosip: %s certificate PEM file loaded [%s]!\n", log, xtc->cert));
    }
    else {
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "eXosip: Couldn't read %s certificate file [%s]!\n", log, xtc->cert));
    }

    if (SSL_CTX_use_PrivateKey_file (ctx, xtc->priv_key, SSL_FILETYPE_ASN1)) {
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "eXosip: %s private key ASN1 file loaded [%s]!\n", log, xtc->priv_key));
    }
    else if (SSL_CTX_use_PrivateKey_file (ctx, xtc->priv_key, SSL_FILETYPE_PEM)) {
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "eXosip: %s private key PEM file loaded [%s]!\n", log, xtc->priv_key));
    }
    else {
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "eXosip: Couldn't read %s private key file [%s]!\n", log, xtc->priv_key));
    }

    if (!SSL_CTX_check_private_key (ctx)) {
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "eXosip: %s private key does not match the public key of your certificate\n", log));
    }
  }
}

static void
_tls_common_setup (eXosip_tls_ctx_t * exosip_tls_cfg, SSL_CTX * ctx)
{
#ifndef SSL_CTRL_SET_ECDH_AUTO
#define SSL_CTRL_SET_ECDH_AUTO 94
#endif
  if (exosip_tls_cfg->dh_param[0] == '\0')
    build_dh_params (ctx);
  else
    load_dh_params (ctx, exosip_tls_cfg->dh_param);

  /* SSL_CTX_set_ecdh_auto (ctx, on) requires OpenSSL 1.0.2 which wraps: */
  if (SSL_CTX_ctrl (ctx, SSL_CTRL_SET_ECDH_AUTO, 1, NULL)) {
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "ctrl_set_ecdh_auto: faster PFS ciphers enabled\n"));
#if !defined(OPENSSL_NO_ECDH) && !(OPENSSL_VERSION_NUMBER < 0x10000000L) && (OPENSSL_VERSION_NUMBER < 0x10100000L)
  }
  else {
    /* enables AES-128 ciphers, to get AES-256 use NID_secp384r1 */
    EC_KEY *ecdh = EC_KEY_new_by_curve_name (NID_X9_62_prime256v1);

    if (ecdh != NULL) {
      if (SSL_CTX_set_tmp_ecdh (ctx, ecdh)) {
        OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "set_tmp_ecdh: faster PFS ciphers enabled (secp256r1)\n"));
      }
      EC_KEY_free (ecdh);
    }
#endif
  }
}

SSL_CTX *
initialize_client_ctx (struct eXosip_t *excontext, eXosip_tls_ctx_t * client_ctx, int transport, const char *sni_servernameindication)
{
  const SSL_METHOD *meth = NULL;
  SSL_CTX *ctx;

  if (transport == IPPROTO_UDP) {
#if !(OPENSSL_VERSION_NUMBER < 0x10002000L)
    meth = DTLS_client_method ();
#elif !(OPENSSL_VERSION_NUMBER < 0x00908000L)
    meth = DTLSv1_client_method ();
#endif
  }
  else if (transport == IPPROTO_TCP) {
    meth = SSLv23_client_method ();
  }
  else {
    return NULL;
  }

  ctx = SSL_CTX_new (meth);

  if (ctx == NULL) {
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "eXosip: Couldn't create SSL_CTX!\n"));
    return NULL;
  }

  _tls_use_certificate_private_key ("client", &client_ctx->client, ctx);

  /* Load the CAs we trust */
  _tls_load_trusted_certificates (client_ctx, ctx);

  {
    int verify_mode = SSL_VERIFY_NONE;

#if !(OPENSSL_VERSION_NUMBER < 0x10002000L)

    if (excontext->tls_verify_client_certificate > 0 && sni_servernameindication != NULL) {
      X509_STORE *pkix_validation_store = SSL_CTX_get_cert_store (ctx);
      const X509_VERIFY_PARAM *param = X509_VERIFY_PARAM_lookup ("ssl_server");

      if (param != NULL) {      /* const value, we have to copy (inherit) */
        X509_VERIFY_PARAM *param_to = X509_STORE_get0_param (pkix_validation_store);

        if (X509_VERIFY_PARAM_inherit (param_to, param)) {
          X509_STORE_set_flags (pkix_validation_store, X509_V_FLAG_TRUSTED_FIRST);
          X509_STORE_set_flags (pkix_validation_store, X509_V_FLAG_PARTIAL_CHAIN);
        }
        else {
          OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "PARAM_inherit: failed for ssl_server\n"));
        }
        if (X509_VERIFY_PARAM_set1_host (param_to, sni_servernameindication, 0)) {
          X509_VERIFY_PARAM_set_hostflags (param_to, X509_CHECK_FLAG_NO_WILDCARDS);
        }
        else {
          OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "PARAM_set1_host: %s failed\n", sni_servernameindication));
        }
      }
      else {
        OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "PARAM_lookup: failed for ssl_server\n"));
      }
    }
#endif

    if (excontext->tls_verify_client_certificate > 0)
      verify_mode = SSL_VERIFY_PEER;
    SSL_CTX_set_verify (ctx, verify_mode, &verify_cb);
    SSL_CTX_set_verify_depth (ctx, ex_verify_depth + 1);
  }

  SSL_CTX_set_options (ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_SINGLE_ECDH_USE | SSL_OP_SINGLE_DH_USE);

  if (!SSL_CTX_set_cipher_list (ctx, "HIGH:-COMPLEMENTOFDEFAULT")) {
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_WARNING, NULL, "set_cipher_list: using DEFAULT list now\n"));
  }

  _tls_common_setup (client_ctx, ctx);

  return ctx;
}

SSL_CTX *
initialize_server_ctx (eXosip_tls_ctx_t * srv_ctx, int transport)
{
  const SSL_METHOD *meth = NULL;
  SSL_CTX *ctx;

  int s_server_session_id_context = 1;

  if (transport == IPPROTO_UDP) {
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO3, NULL, "DTLS-UDP server method\n"));
#if !(OPENSSL_VERSION_NUMBER < 0x10002000L)
    meth = DTLS_server_method ();
#elif !(OPENSSL_VERSION_NUMBER < 0x00908000L)
    meth = DTLSv1_server_method ();
#endif
  }
  else if (transport == IPPROTO_TCP) {
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO3, NULL, "TLS server method\n"));
    meth = SSLv23_server_method ();
  }
  else {
    return NULL;
  }

  ctx = SSL_CTX_new (meth);

  if (ctx == NULL) {
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "eXosip: Couldn't create SSL_CTX!\n"));
    SSL_CTX_free (ctx);
    return NULL;
  }

  if (transport == IPPROTO_UDP) {
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO3, NULL, "DTLS-UDP read ahead\n"));
    SSL_CTX_set_read_ahead (ctx, 1);
  }

  _tls_use_certificate_private_key ("server", &srv_ctx->server, ctx);

  /* Load the CAs we trust */
  _tls_load_trusted_certificates (srv_ctx, ctx);

  if (!SSL_CTX_check_private_key (ctx)) {
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_WARNING, NULL, "check_private_key: either no match, or no cert/key: disable incoming TLS connection\n"));
    SSL_CTX_free (ctx);
    return NULL;
  }

  {
    int verify_mode = SSL_VERIFY_NONE;

    /*verify_mode = SSL_VERIFY_PEER; */

    SSL_CTX_set_verify (ctx, verify_mode, &verify_cb);
    SSL_CTX_set_verify_depth (ctx, ex_verify_depth + 1);
  }

  SSL_CTX_set_options (ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION | SSL_OP_SINGLE_ECDH_USE | SSL_OP_SINGLE_DH_USE | SSL_OP_CIPHER_SERVER_PREFERENCE);

  if (!SSL_CTX_set_cipher_list (ctx, "HIGH:-COMPLEMENTOFDEFAULT")) {
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_WARNING, NULL, "set_cipher_list: using DEFAULT list now\n"));
  }

#if 0
  if (certif_local_cn_name[0] == '\0' && srv_ctx->server.cert[0] == '\0') {
    if (!SSL_CTX_set_cipher_list (ctx, "ADH")) {
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "set_cipher_list: cannot set anonymous DH cipher\n"));
      SSL_CTX_free (ctx);
      return NULL;
    }
  }
#endif

  _tls_common_setup (srv_ctx, ctx);

#ifndef OPENSSL_NO_RSA
  generate_eph_rsa_key (ctx);
#endif

  SSL_CTX_set_session_id_context (ctx, (void *) &s_server_session_id_context, sizeof s_server_session_id_context);

  return ctx;
}

/**
* @brief Initializes the OpenSSL lib and the client/server contexts.
* Depending on the previously initialized eXosip TLS context (see eXosip_set_tls_ctx() ), only the necessary contexts will be initialized.
* The client context will be ALWAYS initialized, the server context only if certificates are available. The following chart should illustrate
* the behaviour.
*
* possible certificates  | Client initialized			  | Server initialized
* -------------------------------------------------------------------------------------
* no certificate		 | yes, no cert used			  | not initialized
* only client cert		 | yes, own cert (client) used    | yes, client cert used
* only server cert		 | yes, server cert used		  | yes, own cert (server) used
* server and client cert | yes, own cert (client) used    | yes, own cert (server) used
*
* The file for seeding the PRNG is only needed on Windows machines. If you compile under a Windows environment, please set W32 oder _WINDOWS as
* Preprocessor directives.
*@return < 0 if an error occured
**/
static int
tls_tl_open (struct eXosip_t *excontext)
{
  struct eXtltls *reserved = (struct eXtltls *) excontext->eXtltls_reserved;
  int res;
  struct addrinfo *addrinfo = NULL;
  struct addrinfo *curinfo;
  int sock = -1;
  char *node = NULL;

  if (reserved == NULL) {
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "wrong state: create transport layer first\n"));
    return OSIP_WRONG_STATE;
  }

  excontext->eXtl_transport.proto_local_port = excontext->eXtl_transport.proto_port;
  if (excontext->eXtl_transport.proto_local_port < 0)
    excontext->eXtl_transport.proto_local_port = 5061;

  /* initialization (outside initialize_server_ctx) */
#if OPENSSL_VERSION_NUMBER < 0x10100000L
  SSL_library_init ();
  SSL_load_error_strings ();
#else
  OPENSSL_init_ssl (OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL);
#endif

  reserved->server_ctx = initialize_server_ctx (&excontext->eXosip_tls_ctx_params, IPPROTO_TCP);

  /* always initialize the client */
  reserved->client_ctx = initialize_client_ctx (excontext, &excontext->eXosip_tls_ctx_params, IPPROTO_TCP, NULL);

/*only necessary under Windows-based OS, unix-like systems use /dev/random or /dev/urandom */
#if defined(HAVE_WINSOCK2_H)

#if 0
  /* check if a file with random data is present --> will be verified when random file is needed */
  if (reserved->eXosip_tls_ctx_params.random_file[0] == '\0') {
    return TLS_ERR_NO_RAND;
  }
#endif

  /* Load randomness */
  if (!(RAND_load_file (excontext->eXosip_tls_ctx_params.random_file, 1024 * 1024)))
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_WARNING, NULL, "eXosip: Couldn't load randomness\n"));
#endif

  if (osip_strcasecmp (excontext->eXtl_transport.proto_ifs, "0.0.0.0") != 0 && osip_strcasecmp (excontext->eXtl_transport.proto_ifs, "::") != 0)
    node = excontext->eXtl_transport.proto_ifs;

  res = _eXosip_get_addrinfo (excontext, &addrinfo, node, excontext->eXtl_transport.proto_local_port, excontext->eXtl_transport.proto_num);
  if (res)
    return -1;

  for (curinfo = addrinfo; curinfo; curinfo = curinfo->ai_next) {
#ifdef ENABLE_MAIN_SOCKET
    socklen_t len;
#endif
    int type;

    if (curinfo->ai_protocol && curinfo->ai_protocol != excontext->eXtl_transport.proto_num) {
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO3, NULL, "eXosip: Skipping protocol %d\n", curinfo->ai_protocol));
      continue;
    }

    type = curinfo->ai_socktype;
#if defined(SOCK_CLOEXEC)
    type = SOCK_CLOEXEC | type;
#endif
    sock = (int) socket (curinfo->ai_family, type, curinfo->ai_protocol);
    if (sock < 0) {
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "eXosip: Cannot create socket %s!\n", strerror (ex_errno)));
      continue;
    }

    if (curinfo->ai_family == AF_INET6) {
#ifdef IPV6_V6ONLY
      if (setsockopt_ipv6only (sock)) {
        _eXosip_closesocket (sock);
        sock = -1;
        OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "eXosip: Cannot set socket option %s!\n", strerror (ex_errno)));
        continue;
      }
#endif /* IPV6_V6ONLY */
    }

    {
      int valopt = 1;

      setsockopt (sock, SOL_SOCKET, SO_REUSEADDR, (void *) &valopt, sizeof (valopt));
    }

#ifdef ENABLE_MAIN_SOCKET
    res = bind (sock, curinfo->ai_addr, (socklen_t) curinfo->ai_addrlen);
    if (res < 0) {
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "eXosip: Cannot bind socket node:%s family:%d %s\n", excontext->eXtl_transport.proto_ifs, curinfo->ai_family, strerror (ex_errno)));
      _eXosip_closesocket (sock);
      sock = -1;
      continue;
    }
    len = sizeof (reserved->ai_addr);
    res = getsockname (sock, (struct sockaddr *) &reserved->ai_addr, &len);
    if (res != 0) {
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "eXosip: Cannot get socket name (%s)\n", strerror (ex_errno)));
      memcpy (&reserved->ai_addr, curinfo->ai_addr, curinfo->ai_addrlen);
    }
    reserved->ai_addr_len = len;

    if (excontext->eXtl_transport.proto_num == IPPROTO_TCP) {
      res = listen (sock, SOMAXCONN);
      if (res < 0) {
        OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "eXosip: Cannot bind socket node:%s family:%d %s\n", excontext->eXtl_transport.proto_ifs, curinfo->ai_family, strerror (ex_errno)));
        _eXosip_closesocket (sock);
        sock = -1;
        continue;
      }
    }
#endif

    break;
  }

  _eXosip_freeaddrinfo (addrinfo);

  if (sock < 0) {
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "eXosip: Cannot bind on port: %i\n", excontext->eXtl_transport.proto_local_port));
    return -1;
  }

  reserved->tls_socket = sock;

  if (excontext->eXtl_transport.proto_local_port == 0) {
    /* get port number from socket */
    if (reserved->ai_addr.ss_family == AF_INET)
      excontext->eXtl_transport.proto_local_port = ntohs (((struct sockaddr_in *) &reserved->ai_addr)->sin_port);
    else
      excontext->eXtl_transport.proto_local_port = ntohs (((struct sockaddr_in6 *) &reserved->ai_addr)->sin6_port);
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO1, NULL, "eXosip: Binding on port %i!\n", excontext->eXtl_transport.proto_local_port));
  }

#ifdef ENABLE_MAIN_SOCKET
#ifdef HAVE_SYS_EPOLL_H
  if (excontext->poll_method == EXOSIP_USE_EPOLL_LT) {
    struct epoll_event ev;

    memset(&ev, 0, sizeof(struct epoll_event));
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = sock;
    res = epoll_ctl (excontext->epfd, EPOLL_CTL_ADD, sock, &ev);
    if (res < 0) {
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "Cannot poll on main tls socket: %i\n", excontext->eXtl_transport.proto_local_port));
      _eXosip_closesocket (sock);
      reserved->tls_socket = -1;
      return -1;
    }
  }
#endif
#endif

  return OSIP_SUCCESS;
}

static int
tls_tl_set_fdset (struct eXosip_t *excontext, fd_set * osip_fdset, fd_set * osip_wrset, int *fd_max)
{
  struct eXtltls *reserved = (struct eXtltls *) excontext->eXtltls_reserved;
  int pos;

  if (reserved == NULL) {
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "wrong state: create transport layer first\n"));
    return OSIP_WRONG_STATE;
  }

#ifdef ENABLE_MAIN_SOCKET
  if (reserved->tls_socket <= 0)
    return -1;

  eXFD_SET (reserved->tls_socket, osip_fdset);

  if (reserved->tls_socket > *fd_max)
    *fd_max = reserved->tls_socket;
#endif

  for (pos = 0; pos < EXOSIP_MAX_SOCKETS; pos++) {
    if (reserved->socket_tab[pos].socket > 0) {
      eXFD_SET (reserved->socket_tab[pos].socket, osip_fdset);
      if (reserved->socket_tab[pos].socket > *fd_max)
        *fd_max = reserved->socket_tab[pos].socket;
      if (reserved->socket_tab[pos].sendbuflen > 0)
        eXFD_SET (reserved->socket_tab[pos].socket, osip_wrset);
      if (reserved->socket_tab[pos].ssl_state == 0)     /* wait for establishment */
        eXFD_SET (reserved->socket_tab[pos].socket, osip_wrset);
    }
  }

  return OSIP_SUCCESS;
}

static int
_tls_print_ssl_error (int err)
{
  switch (err) {
  case SSL_ERROR_NONE:
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "SSL ERROR NONE - OK\n"));
    break;
  case SSL_ERROR_ZERO_RETURN:
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "SSL ERROR ZERO RETURN - SHUTDOWN\n"));
    break;
  case SSL_ERROR_WANT_READ:
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "SSL want read\n"));
    break;
  case SSL_ERROR_WANT_WRITE:
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "SSL want write\n"));
    break;
  case SSL_ERROR_SSL:
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "SSL ERROR\n"));
    break;
  case SSL_ERROR_SYSCALL:
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "SSL ERROR SYSCALL\n"));
    break;
  default:
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "SSL problem\n"));
  }
  return OSIP_SUCCESS;
}


static void
tls_dump_verification_failure (long verification_result)
{
  char tmp[64];

  snprintf (tmp, sizeof (tmp), "unknown errror");
  switch (verification_result) {
  case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT:
    snprintf (tmp, sizeof (tmp), "unable to get issuer certificate");
    break;
  case X509_V_ERR_UNABLE_TO_GET_CRL:
    snprintf (tmp, sizeof (tmp), "unable to get certificate CRL");
    break;
  case X509_V_ERR_UNABLE_TO_DECRYPT_CERT_SIGNATURE:
    snprintf (tmp, sizeof (tmp), "unable to decrypt certificate's signature");
    break;
  case X509_V_ERR_UNABLE_TO_DECRYPT_CRL_SIGNATURE:
    snprintf (tmp, sizeof (tmp), "unable to decrypt CRL's signature");
    break;
  case X509_V_ERR_UNABLE_TO_DECODE_ISSUER_PUBLIC_KEY:
    snprintf (tmp, sizeof (tmp), "unable to decode issuer public key");
    break;
  case X509_V_ERR_CERT_SIGNATURE_FAILURE:
    snprintf (tmp, sizeof (tmp), "certificate signature failure");
    break;
  case X509_V_ERR_CRL_SIGNATURE_FAILURE:
    snprintf (tmp, sizeof (tmp), "CRL signature failure");
    break;
  case X509_V_ERR_CERT_NOT_YET_VALID:
    snprintf (tmp, sizeof (tmp), "certificate is not yet valid");
    break;
  case X509_V_ERR_CERT_HAS_EXPIRED:
    snprintf (tmp, sizeof (tmp), "certificate has expired");
    break;
  case X509_V_ERR_CRL_NOT_YET_VALID:
    snprintf (tmp, sizeof (tmp), "CRL is not yet valid");
    break;
  case X509_V_ERR_CRL_HAS_EXPIRED:
    snprintf (tmp, sizeof (tmp), "CRL has expired");
    break;
  case X509_V_ERR_ERROR_IN_CERT_NOT_BEFORE_FIELD:
    snprintf (tmp, sizeof (tmp), "format error in certificate's notBefore field");
    break;
  case X509_V_ERR_ERROR_IN_CERT_NOT_AFTER_FIELD:
    snprintf (tmp, sizeof (tmp), "format error in certificate's notAfter field");
    break;
  case X509_V_ERR_ERROR_IN_CRL_LAST_UPDATE_FIELD:
    snprintf (tmp, sizeof (tmp), "format error in CRL's lastUpdate field");
    break;
  case X509_V_ERR_ERROR_IN_CRL_NEXT_UPDATE_FIELD:
    snprintf (tmp, sizeof (tmp), "format error in CRL's nextUpdate field");
    break;
  case X509_V_ERR_OUT_OF_MEM:
    snprintf (tmp, sizeof (tmp), "out of memory");
    break;
  case X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT:
    snprintf (tmp, sizeof (tmp), "self signed certificate");
    break;
  case X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN:
    snprintf (tmp, sizeof (tmp), "self signed certificate in certificate chain");
    break;
  case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY:
    snprintf (tmp, sizeof (tmp), "unable to get local issuer certificate");
    break;
  case X509_V_ERR_UNABLE_TO_VERIFY_LEAF_SIGNATURE:
    snprintf (tmp, sizeof (tmp), "unable to verify the first certificate");
    break;
  case X509_V_ERR_CERT_CHAIN_TOO_LONG:
    snprintf (tmp, sizeof (tmp), "certificate chain too long");
    break;
  case X509_V_ERR_CERT_REVOKED:
    snprintf (tmp, sizeof (tmp), "certificate revoked");
    break;
  case X509_V_ERR_INVALID_CA:
    snprintf (tmp, sizeof (tmp), "invalid CA certificate");
    break;
  case X509_V_ERR_PATH_LENGTH_EXCEEDED:
    snprintf (tmp, sizeof (tmp), "path length constraint exceeded");
    break;
  case X509_V_ERR_INVALID_PURPOSE:
    snprintf (tmp, sizeof (tmp), "unsupported certificate purpose");
    break;
  case X509_V_ERR_CERT_UNTRUSTED:
    snprintf (tmp, sizeof (tmp), "certificate not trusted");
    break;
  case X509_V_ERR_CERT_REJECTED:
    snprintf (tmp, sizeof (tmp), "certificate rejected");
    break;
  case X509_V_ERR_SUBJECT_ISSUER_MISMATCH:
    snprintf (tmp, sizeof (tmp), "subject issuer mismatch");
    break;
  case X509_V_ERR_AKID_SKID_MISMATCH:
    snprintf (tmp, sizeof (tmp), "authority and subject key identifier mismatch");
    break;
  case X509_V_ERR_AKID_ISSUER_SERIAL_MISMATCH:
    snprintf (tmp, sizeof (tmp), "authority and issuer serial number mismatch");
    break;
  case X509_V_ERR_KEYUSAGE_NO_CERTSIGN:
    snprintf (tmp, sizeof (tmp), "key usage does not include certificate signing");
    break;
  case X509_V_ERR_APPLICATION_VERIFICATION:
    snprintf (tmp, sizeof (tmp), "application verification failure");
    break;
  }

  OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "verification failure: %s\n", tmp));
}

#ifdef HAVE_SYS_EPOLL_H

static int
_tls_tl_is_connected_epoll (int sock)
{
  int res;
  int valopt;
  socklen_t sock_len;
  int nfds;
  struct epoll_event ep_array;
  int epfd;

  struct epoll_event ev;

  epfd = epoll_create (1);

  memset(&ev, 0, sizeof(struct epoll_event));
  ev.events = EPOLLOUT | EPOLLET;
  ev.data.fd = sock;
  res = epoll_ctl (epfd, EPOLL_CTL_ADD, sock, &ev);
  if (res < 0) {
    _eXosip_closesocket (epfd);
    return -1;
  }

  nfds = epoll_wait (epfd, &ep_array, 1, SOCKET_TIMEOUT);
  if (nfds > 0) {
    sock_len = sizeof (int);
    if (getsockopt (sock, SOL_SOCKET, SO_ERROR, (void *) (&valopt), &sock_len) == 0) {
      //OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "XX Cannot connect socket node [%d]\n", valopt));
      if (valopt == 0) {
	_eXosip_closesocket (epfd);
        return 0;
      }
      if (valopt == EINPROGRESS || valopt == EALREADY) {
        OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "(epoll) Cannot connect socket node(%i) / %s[%d]\n", valopt, strerror (ex_errno), ex_errno));
	_eXosip_closesocket (epfd);
        return 1;
      }
      if (is_wouldblock_error (valopt)) {
        OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "(epoll) Cannot connect socket node(%i) would block / %s[%d]\n", valopt, strerror (ex_errno), ex_errno));
	_eXosip_closesocket (epfd);
        return 1;
      }
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "(epoll) Cannot connect socket node / %s[%d]\n", strerror (valopt), valopt));

      _eXosip_closesocket (epfd);
      return -1;
    }
    else {
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "(epoll) Cannot connect socket node / error in getsockopt %s[%d]\n", strerror (ex_errno), ex_errno));
      _eXosip_closesocket (epfd);
      return -1;
    }
  }
  else if (res < 0) {
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "(epoll) Cannot connect socket node / error in epoll %s[%d]\n", strerror (ex_errno), ex_errno));
    _eXosip_closesocket (epfd);
    return -1;
  }
  else {
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "(epoll) Cannot connect socket node / epoll timeout (%d ms)\n", SOCKET_TIMEOUT));
    _eXosip_closesocket (epfd);
    return 1;
  }
}

#endif

static int
_tls_tl_is_connected (int epoll_method, int sock)
{
  int res;
  struct timeval tv;
  fd_set wrset;
  int valopt;
  socklen_t sock_len;

#ifdef HAVE_SYS_EPOLL_H
  if (epoll_method == EXOSIP_USE_EPOLL_LT) {
    return _tls_tl_is_connected_epoll (sock);
  }
#endif

  tv.tv_sec = SOCKET_TIMEOUT / 1000;
  tv.tv_usec = (SOCKET_TIMEOUT % 1000) * 1000;

  FD_ZERO (&wrset);
  FD_SET (sock, &wrset);

  res = select (sock + 1, NULL, &wrset, NULL, &tv);
  if (res > 0) {
    sock_len = sizeof (int);
    if (getsockopt (sock, SOL_SOCKET, SO_ERROR, (void *) (&valopt), &sock_len) == 0) {
      if (valopt == 0)
        return 0;
#if defined(HAVE_WINSOCK2_H)
      if (ex_errno == WSAEWOULDBLOCK) {
        OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "Cannot connect socket node(%i) / %s[%d]\n", valopt, strerror (ex_errno), ex_errno));
        return 1;
      }
      if (is_wouldblock_error (ex_errno)) {
        OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "Cannot connect socket node(%i) would block / %s[%d]\n", valopt, strerror (ex_errno), ex_errno));
        return 1;
      }
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "Cannot connect socket node / %s[%d]\n", strerror (ex_errno), ex_errno));
#else
      if (valopt == EINPROGRESS || valopt == EALREADY) {
        OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "Cannot connect socket node(%i) / %s[%d]\n", valopt, strerror (ex_errno), ex_errno));
        return 1;
      }
      if (is_wouldblock_error (valopt)) {
        OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "Cannot connect socket node(%i) would block / %s[%d]\n", valopt, strerror (ex_errno), ex_errno));
        return 1;
      }
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "Cannot connect socket node / %s[%d]\n", strerror (valopt), valopt));
#endif

      return -1;
    }
    else {
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "Cannot connect socket node / error in getsockopt %s[%d]\n", strerror (ex_errno), ex_errno));
      return -1;
    }
  }
  else if (res < 0) {
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "Cannot connect socket node / error in select %s[%d]\n", strerror (ex_errno), ex_errno));
    return -1;
  }
  else {
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "Cannot connect socket node / select timeout (%d ms)\n", SOCKET_TIMEOUT));
    return 1;
  }
}

static int
_tls_tl_check_connected (struct eXosip_t *excontext)
{
  struct eXtltls *reserved = (struct eXtltls *) excontext->eXtltls_reserved;
  int pos;
  int res;

  for (pos = 0; pos < EXOSIP_MAX_SOCKETS; pos++) {
    if (reserved->socket_tab[pos].invalid > 0) {
      OSIP_TRACE (osip_trace
                  (__FILE__, __LINE__, OSIP_INFO2, NULL,
                   "_tls_tl_check_connected: socket node is in invalid state:%s:%i, socket %d [pos=%d], family:%d\n",
                   reserved->socket_tab[pos].remote_ip, reserved->socket_tab[pos].remote_port, reserved->socket_tab[pos].socket, pos, reserved->socket_tab[pos].ai_addr.sa_family));
      _tls_tl_close_sockinfo (&reserved->socket_tab[pos]);
      continue;
    }

    if (reserved->socket_tab[pos].socket > 0 && reserved->socket_tab[pos].ai_addrlen > 0) {
      if (reserved->socket_tab[pos].ssl_state > 0) {
        /* already connected */
        reserved->socket_tab[pos].ai_addrlen = 0;
        continue;
      }

      res = _tls_tl_is_connected (excontext->poll_method, reserved->socket_tab[pos].socket);
      if (res > 0) {
#if 0
        /* bug: calling connect several times for TCP is not allowed by specification */
        res = connect (reserved->socket_tab[pos].socket, &reserved->socket_tab[pos].ai_addr, reserved->socket_tab[pos].ai_addrlen);
        if (res < 0) {
          int status = ex_errno;

          OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "_tls_tl_check_connected: connect being called again (res=%i) (errno=%i) (%s)\n", res, status, strerror (status)));
        }
#endif
        OSIP_TRACE (osip_trace
                    (__FILE__, __LINE__, OSIP_INFO2, NULL,
                     "_tls_tl_check_connected: socket node:%s:%i, socket %d [pos=%d], family:%d, in progress\n",
                     reserved->socket_tab[pos].remote_ip, reserved->socket_tab[pos].remote_port, reserved->socket_tab[pos].socket, pos, reserved->socket_tab[pos].ai_addr.sa_family));
        continue;
      }
      else if (res == 0) {
        OSIP_TRACE (osip_trace
                    (__FILE__, __LINE__, OSIP_INFO1, NULL,
                     "_tls_tl_check_connected: socket node:%s:%i , socket %d [pos=%d], family:%d, connected\n",
                     reserved->socket_tab[pos].remote_ip, reserved->socket_tab[pos].remote_port, reserved->socket_tab[pos].socket, pos, reserved->socket_tab[pos].ai_addr.sa_family));
        /* stop calling "connect()" */
        reserved->socket_tab[pos].ai_addrlen = 0;
        reserved->socket_tab[pos].ssl_state = 1;
        continue;
      }
      else {
        OSIP_TRACE (osip_trace
                    (__FILE__, __LINE__, OSIP_INFO2, NULL,
                     "_tls_tl_check_connected: socket node:%s:%i, socket %d [pos=%d], family:%d, error\n",
                     reserved->socket_tab[pos].remote_ip, reserved->socket_tab[pos].remote_port, reserved->socket_tab[pos].socket, pos, reserved->socket_tab[pos].ai_addr.sa_family));
        _tls_tl_close_sockinfo (&reserved->socket_tab[pos]);
        continue;
      }
    }
  }
  return 0;
}

static int
pkp_pin_peer_pubkey (struct eXosip_t *excontext, SSL * ssl)
{
  X509 *cert = NULL;
  FILE *fp = NULL;

  int len1 = 0, len2 = 0;
  unsigned char *buff1 = NULL, *buff2 = NULL;

  int ret = 0, result = -1;

  if (NULL == ssl)
    return -1;

  if (excontext->eXosip_tls_ctx_params.client.public_key_pinned[0] == '\0')
    return 0;
  OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO1, NULL, "eXosip: checking pinned public key for certificate [%s]\n", excontext->eXosip_tls_ctx_params.client.public_key_pinned));

  do {
    unsigned char *temp = NULL;
    long size;

    cert = SSL_get_peer_certificate (ssl);
    if (!(cert != NULL))
      break;                    /* failed */

    /* Begin Gyrations to get the subjectPublicKeyInfo       */
    /* Thanks to Viktor Dukhovni on the OpenSSL mailing list */

    len1 = i2d_X509_PUBKEY (X509_get_X509_PUBKEY (cert), NULL);
    if (!(len1 > 0))
      break;                    /* failed */

    buff1 = temp = OPENSSL_malloc (len1);
    if (!(buff1 != NULL))
      break;                    /* failed */

    len2 = i2d_X509_PUBKEY (X509_get_X509_PUBKEY (cert), &temp);

    if (!((len1 == len2) && (temp != NULL) && ((temp - buff1) == len1)))
      break;                    /* failed */

    /* in order to get your public key file in DER format: */
    /* openssl x509 -in your-base64-certificate.pem -pubkey -noout | openssl enc -base64 -d > publickey.der */
    fp = fopen (excontext->eXosip_tls_ctx_params.client.public_key_pinned, "rb");
    if (NULL == fp)
      fp = fopen (excontext->eXosip_tls_ctx_params.client.public_key_pinned, "r");

    if (!(NULL != fp))
      break;                    /* failed */

    ret = fseek (fp, 0, SEEK_END);
    if (!(0 == ret))
      break;                    /* failed */

    size = ftell (fp);

    if (!(size != -1 && size > 0 && size < 4096))
      break;                    /* failed */

    ret = fseek (fp, 0, SEEK_SET);
    if (!(0 == ret))
      break;                    /* failed */

    buff2 = NULL;
    len2 = (int) size;

    buff2 = OPENSSL_malloc (len2);
    if (!(buff2 != NULL))
      break;                    /* failed */

    ret = (int) fread (buff2, (size_t) len2, 1, fp);
    if (!(ret == 1))
      break;                    /* failed */

    size = len1 < len2 ? len1 : len2;

    if (len1 != (int) size || len2 != (int) size || 0 != memcmp (buff1, buff2, (size_t) size))
      break;                    /* failed */

    result = 0;

  } while (0);

  if (fp != NULL)
    fclose (fp);
  if (NULL != buff2)
    OPENSSL_free (buff2);
  if (NULL != buff1)
    OPENSSL_free (buff1);
  if (NULL != cert)
    X509_free (cert);

  return result;
}

static int
_tls_tl_ssl_connect_socket (struct eXosip_t *excontext, struct _tls_stream *sockinfo)
{
  X509 *cert;
  BIO *sbio;
  int res;
  int tries_left = 100;

  if (sockinfo->ssl_ctx == NULL) {
    sockinfo->ssl_ctx = initialize_client_ctx (excontext, &excontext->eXosip_tls_ctx_params, IPPROTO_TCP, sockinfo->sni_servernameindication);

    /* FIXME: changed parameter from ctx to client_ctx -> works now */
    sockinfo->ssl_conn = SSL_new (sockinfo->ssl_ctx);
    if (sockinfo->ssl_conn == NULL) {
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "SSL_new error\n"));
      return -1;
    }
    sbio = BIO_new_socket (sockinfo->socket, BIO_NOCLOSE);

    if (sbio == NULL) {
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "BIO_new_socket error\n"));
      return -1;
    }
    SSL_set_bio (sockinfo->ssl_conn, sbio, sbio);

#ifndef OPENSSL_NO_TLSEXT
    if (!SSL_set_tlsext_host_name (sockinfo->ssl_conn, sockinfo->sni_servernameindication /* "host.name.before.dns.srv.com" */ )) {
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_WARNING, NULL, "set_tlsext_host_name (SNI): no servername gets indicated\n"));
    }
#endif
  }

  if (SSL_is_init_finished (sockinfo->ssl_conn)) {
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "SSL_is_init_finished already done\n"));
  }
  else {
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "SSL_is_init_finished not already done\n"));
  }

  do {
    struct timeval tv;
    int fd;
    fd_set readfds;

    res = SSL_connect (sockinfo->ssl_conn);
    res = SSL_get_error (sockinfo->ssl_conn, res);
    if (res == SSL_ERROR_NONE) {
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "SSL_connect succeeded\n"));
      break;
    }

    if (res != SSL_ERROR_WANT_READ && res != SSL_ERROR_WANT_WRITE) {
      _tls_print_ssl_error (res);
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "SSL_connect error\n"));

      /* any transaction here should fail? --> but this is called from _tls_tl_recv not the send side... */

      return -1;
    }

    tv.tv_sec = SOCKET_TIMEOUT / 1000;
    tv.tv_usec = (SOCKET_TIMEOUT % 1000) * 1000;
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "SSL_connect retry\n"));

    fd = SSL_get_fd (sockinfo->ssl_conn);
    FD_ZERO (&readfds);
    FD_SET (fd, &readfds);
    res = select (fd + 1, &readfds, NULL, NULL, &tv);
    if (res < 0) {
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "SSL_connect select(read) error (%s)\n", strerror (ex_errno)));
      return -1;
    }
    else if (res > 0) {
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "SSL_connect (read done)\n"));
    }
    else {
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "SSL_connect (timeout not data to read) (%d ms)\n", SOCKET_TIMEOUT));
      return 1;
    }
  } while (!SSL_is_init_finished (sockinfo->ssl_conn) && ((tries_left--) > 0));

  if (SSL_is_init_finished (sockinfo->ssl_conn)) {
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "SSL_is_init_finished done\n"));
  }
  else {
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "SSL_is_init_finished failed\n"));
  }

  cert = SSL_get_peer_certificate (sockinfo->ssl_conn);
  if (cert != 0) {
    long cert_err;

    tls_dump_cert_info ("tls_connect: remote certificate: ", cert);

    cert_err = SSL_get_verify_result (sockinfo->ssl_conn);
    if (cert_err != X509_V_OK) {
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_WARNING, NULL, "Failed to verify remote certificate\n"));
      tls_dump_verification_failure (cert_err);
    }
    X509_free (cert);

    if (pkp_pin_peer_pubkey (excontext, sockinfo->ssl_conn)) {
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "Failed to verify public key for certificate\n"));
      return -1;
    }
  }
  else {
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "No certificate received\n"));
    /* X509_free is not necessary because no cert-object was created -> cert == NULL */
    if (excontext->eXosip_tls_ctx_params.server.cert[0] == '\0') {
#ifdef ENABLE_ADH
      /* how can we guess a user want ADH... specific APIs.. */
      sockinfo->ssl_state = 3;
      return 0;
#endif
    }

    return -1;
  }

  sockinfo->ssl_state = 3;
  return 0;
}

/* Like strstr, but works for haystack that may contain binary data and is
   not NUL-terminated. */
static char *
_tls_buffer_find (const char *haystack, size_t haystack_len, const char *needle)
{
  const char *search = haystack, *end = haystack + haystack_len;
  char *p;
  size_t len = strlen (needle);

  while (search < end && (p = memchr (search, *needle, end - search)) != NULL) {
    if (p + len > end)
      break;
    if (memcmp (p, needle, len) == 0)
      return (p);
    search = p + 1;
  }

  return (NULL);
}

#define END_HEADERS_STR "\r\n\r\n"
#define CLEN_HEADER_STR "\r\ncontent-length:"
#define CLEN_HEADER_COMPACT_STR "\r\nl:"
#define CLEN_HEADER_STR2 "\r\ncontent-length "
#define CLEN_HEADER_COMPACT_STR2 "\r\nl "
#define const_strlen(x) (sizeof((x)) - 1)

/* consume any complete messages in sockinfo->buf and
   return the total number of bytes consumed */
static int
_tls_handle_messages (struct eXosip_t *excontext, struct _tls_stream *sockinfo)
{
  int consumed = 0;
  char *buf = sockinfo->buf;
  size_t buflen = sockinfo->buflen;
  char *end_headers;

  while (buflen > 0 && (end_headers = _tls_buffer_find (buf, buflen, END_HEADERS_STR)) != NULL) {
    int clen, msglen;
    char *clen_header;

    if (buf == end_headers) {
      /* skip tcp standard keep-alive */
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO1, NULL, "socket %s:%i: standard keep alive received (CRLFCRLF)\n", sockinfo->remote_ip, sockinfo->remote_port, buf));
      consumed += 4;
      buflen -= 4;
      buf += 4;
      continue;
    }

    /* stuff a nul in so we can use osip_strcasestr */
    *end_headers = '\0';

    /* ok we have complete headers, find content-length: or l: */
    clen_header = osip_strcasestr (buf, CLEN_HEADER_STR);
    if (!clen_header)
      clen_header = osip_strcasestr (buf, CLEN_HEADER_STR2);
    if (!clen_header)
      clen_header = osip_strcasestr (buf, CLEN_HEADER_COMPACT_STR);
    if (!clen_header)
      clen_header = osip_strcasestr (buf, CLEN_HEADER_COMPACT_STR2);
    if (clen_header != NULL) {
      clen_header = strchr (clen_header, ':');
      clen_header++;
    }
    if (!clen_header) {
      /* Oops, no content-length header.      Presume 0 (below) so we
         consume the headers and make forward progress.  This permits
         server-side keepalive of "\r\n\r\n". */
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO1, NULL, "socket %s:%i: message has no content-length: <%s>\n", sockinfo->remote_ip, sockinfo->remote_port, buf));
    }
    clen = clen_header ? atoi (clen_header) : 0;
    if (clen < 0)
      return (int) sockinfo->buflen;    /* discard data */
    /* undo our overwrite and advance end_headers */
    *end_headers = END_HEADERS_STR[0];
    end_headers += const_strlen (END_HEADERS_STR);

    /* do we have the whole message? */
    msglen = (int) (end_headers - buf + clen);
    if (msglen > buflen) {
      /* nope */
      return consumed;
    }
    /* yep; handle the message */
    _eXosip_handle_incoming_message (excontext, buf, msglen, sockinfo->socket, sockinfo->remote_ip, sockinfo->remote_port, sockinfo->natted_ip, &sockinfo->natted_port);
    consumed += msglen;
    buflen -= msglen;
    buf += msglen;
  }

  return consumed;
}

static int
_tls_tl_recv (struct eXosip_t *excontext, struct _tls_stream *sockinfo)
{
  int r;
  int rlen, err;

  if (!sockinfo->buf) {
    sockinfo->buf = (char *) osip_malloc (SIP_MESSAGE_MAX_LENGTH);
    if (sockinfo->buf == NULL)
      return OSIP_NOMEM;
    sockinfo->bufsize = SIP_MESSAGE_MAX_LENGTH;
    sockinfo->buflen = 0;
  }

  /* buffer is 100% full -> realloc with more size */
  if (sockinfo->bufsize - sockinfo->buflen <= 0) {
    sockinfo->buf = (char *) osip_realloc (sockinfo->buf, sockinfo->bufsize + 5000);
    if (sockinfo->buf == NULL)
      return OSIP_NOMEM;
    sockinfo->bufsize = sockinfo->bufsize + 5000;
  }

  /* buffer is 100% empty-> realloc with initial size */
  if (sockinfo->buflen == 0 && sockinfo->bufsize > SIP_MESSAGE_MAX_LENGTH) {
    osip_free (sockinfo->buf);
    sockinfo->buf = (char *) osip_malloc (SIP_MESSAGE_MAX_LENGTH);
    if (sockinfo->buf == NULL)
      return OSIP_NOMEM;
    sockinfo->bufsize = SIP_MESSAGE_MAX_LENGTH;
  }


  /* do TLS handshake? */

  if (sockinfo->ssl_state == 0) {
    r = _tls_tl_is_connected (excontext->poll_method, sockinfo->socket);
    if (r > 0) {
      return OSIP_SUCCESS;
    }
    else if (r == 0) {
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "socket node:%s , socket %d [pos=%d], connected\n", sockinfo->remote_ip, sockinfo->socket, -1));
      sockinfo->ssl_state = 1;
      sockinfo->ai_addrlen = 0;
    }
    else {
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "socket node:%s, socket %d [pos=%d], socket error\n", sockinfo->remote_ip, sockinfo->socket, -1));
      _tls_tl_close_sockinfo (sockinfo);
      return OSIP_SUCCESS;
    }
  }

  if (sockinfo->ssl_state == 1) {
    r = _tls_tl_ssl_connect_socket (excontext, sockinfo);

    if (r < 0) {
      _tls_tl_close_sockinfo (sockinfo);
      /* force to have an immediate send call? This may accelerate the network callback error */
      _eXosip_mark_registration_ready (excontext, sockinfo->reg_call_id);
      return OSIP_SUCCESS;
    }
    if (sockinfo->ssl_state == 3) {
      /* we are now ready to write in socket, we want send to be called asap */
      _eXosip_mark_registration_ready (excontext, sockinfo->reg_call_id);
    }
  }

  if (sockinfo->ssl_state == 2) {
    r = SSL_do_handshake (sockinfo->ssl_conn);
    if (r <= 0) {
      r = SSL_get_error (sockinfo->ssl_conn, r);
      _tls_print_ssl_error (r);

      _tls_tl_close_sockinfo (sockinfo);
      return OSIP_SUCCESS;
    }
    sockinfo->ssl_state = 3;
  }

  if (sockinfo->ssl_state != 3)
    return OSIP_SUCCESS;



  r = 0;
  rlen = 0;

  do {
    r = SSL_read (sockinfo->ssl_conn, sockinfo->buf + sockinfo->buflen + rlen, (int) (sockinfo->bufsize - sockinfo->buflen - rlen));
    if (r <= 0) {
      err = SSL_get_error (sockinfo->ssl_conn, r);
      if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
        break;
      }
      else {
        _tls_print_ssl_error (err);
        /*
           The TLS/SSL connection has been closed.  If the protocol version
           is SSL 3.0 or TLS 1.0, this result code is returned only if a
           closure alert has occurred in the protocol, i.e. if the
           connection has been closed cleanly. Note that in this case
           SSL_ERROR_ZERO_RETURN does not necessarily indicate that the
           underlying transport has been closed. */
        OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_WARNING, NULL, "TLS closed\n"));

        _eXosip_mark_registration_expired (excontext, sockinfo->reg_call_id);
        _tls_tl_close_sockinfo (sockinfo);

        rlen = 0;               /* discard any remaining data ? */
        break;
      }
    }
    else {
      rlen += r;
      break;
    }
  }
  while (SSL_pending (sockinfo->ssl_conn));

  if (r == 0) {
    return OSIP_UNDEFINED_ERROR;
  }
  else if (r < 0) {
    return OSIP_UNDEFINED_ERROR;
  }
  else {
    int consumed;
    int err = OSIP_SUCCESS;

    if (SSL_pending (sockinfo->ssl_conn))
      err = -999;

    sockinfo->tcp_max_timeout = 0;
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO1, NULL, "socket %s:%i: read %d bytes\n", sockinfo->remote_ip, sockinfo->remote_port, r));
    sockinfo->buflen += rlen;
    consumed = _tls_handle_messages (excontext, sockinfo);
    if (consumed != 0) {
      if (sockinfo->buflen > consumed) {
        memmove (sockinfo->buf, sockinfo->buf + consumed, sockinfo->buflen - consumed);
        sockinfo->buflen -= consumed;
      }
      else {
        sockinfo->buflen = 0;
      }
    }

    return err;                 /* if -999 is returned, internal buffer of SSL still contains some data */
  }
}

static int
_tls_read_tls_main_socket (struct eXosip_t *excontext)
{
  struct eXtltls *reserved = (struct eXtltls *) excontext->eXtltls_reserved;

  /* accept incoming connection */
  char src6host[NI_MAXHOST];
  int recvport = 0;
  struct sockaddr_storage sa;
  int sock;
  int i;

  socklen_t slen;
  int pos;

  SSL *ssl = NULL;
  BIO *sbio;

  if (reserved->ai_addr.ss_family == AF_INET)
    slen = sizeof (struct sockaddr_in);
  else
    slen = sizeof (struct sockaddr_in6);

  for (pos = 0; pos < EXOSIP_MAX_SOCKETS; pos++) {
    if (reserved->socket_tab[pos].socket <= 0)
      break;
  }
  if (pos == EXOSIP_MAX_SOCKETS) {
    /* delete an old one! */
    pos = 0;
    if (reserved->socket_tab[pos].socket > 0) {
      _tls_tl_close_sockinfo (&reserved->socket_tab[pos]);
    }
    memset (&reserved->socket_tab[pos], 0, sizeof (struct _tls_stream));
  }

  OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO3, NULL, "creating TLS socket at index: %i\n", pos));

  sock = (int) accept (reserved->tls_socket, (struct sockaddr *) &sa, (socklen_t *) & slen);
  if (sock < 0) {
#if defined(EBADF)
    int status = ex_errno;
#endif
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "Error accepting TLS socket\n"));
#if defined(EBADF)
    if (status == EBADF) {
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "Error accepting TLS socket: EBADF\n"));
      memset (&reserved->ai_addr, 0, sizeof (struct sockaddr_storage));
      if (reserved->tls_socket > 0) {
        _eXosip_closesocket (reserved->tls_socket);
        for (i = 0; i < EXOSIP_MAX_SOCKETS; i++) {
          if (reserved->socket_tab[i].socket > 0 && reserved->socket_tab[i].is_server > 0)
            _tls_tl_close_sockinfo (&reserved->socket_tab[i]);
        }
      }
      tls_tl_open (excontext);
    }
#endif
  }
  else {
    if (reserved->server_ctx == NULL) {
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO1, NULL, "TLS connection rejected\n"));
      _eXosip_closesocket (sock);
      return -1;
    }

    if (!SSL_CTX_check_private_key (reserved->server_ctx)) {
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "SSL CTX private key check error\n"));
    }

    ssl = SSL_new (reserved->server_ctx);
    if (ssl == NULL) {
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "Cannot create ssl connection context\n"));
      return -1;
    }

    if (!SSL_check_private_key (ssl)) {
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "SSL private key check error\n"));
    }

    sbio = BIO_new_socket (sock, BIO_NOCLOSE);
    if (sbio == NULL) {
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "BIO_new_socket error\n"));
    }

    SSL_set_bio (ssl, sbio, sbio);      /* cannot fail */

    i = SSL_accept (ssl);
    if (i <= 0) {
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "SSL_accept error: %s\n", ERR_error_string (ERR_get_error (), NULL)));
      i = SSL_get_error (ssl, i);
      _tls_print_ssl_error (i);

      SSL_shutdown (ssl);
      _eXosip_closesocket (sock);
      SSL_free (ssl);
      return -1;
    }

    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO1, NULL, "New TLS connection accepted\n"));

    reserved->socket_tab[pos].socket = sock;
    reserved->socket_tab[pos].is_server = 1;
    reserved->socket_tab[pos].ssl_conn = ssl;
    reserved->socket_tab[pos].ssl_state = 2;

    {
      int valopt = 1;

      setsockopt (sock, SOL_SOCKET, SO_REUSEADDR, (void *) &valopt, sizeof (valopt));
    }

    memset (src6host, 0, NI_MAXHOST);
    recvport = _eXosip_getport ((struct sockaddr *) &sa);
    _eXosip_getnameinfo ((struct sockaddr *) &sa, slen, src6host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);

    _eXosip_transport_set_dscp (excontext, sa.ss_family, sock);

    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO1, NULL, "Message received from: %s:%i\n", src6host, recvport));
    osip_strncpy (reserved->socket_tab[pos].remote_ip, src6host, sizeof (reserved->socket_tab[pos].remote_ip) - 1);
    reserved->socket_tab[pos].remote_port = recvport;

#ifdef HAVE_SYS_EPOLL_H
    if (excontext->poll_method == EXOSIP_USE_EPOLL_LT) {
      struct epoll_event ev;
      int res;

      memset(&ev, 0, sizeof(struct epoll_event));
      ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
      ev.data.fd = sock;
      res = epoll_ctl (excontext->epfd, EPOLL_CTL_ADD, sock, &ev);
      if (res < 0) {
        _tls_tl_close_sockinfo (&reserved->socket_tab[pos]);
        return -1;
      }
    }
#endif

  }
  return OSIP_SUCCESS;
}

#ifdef HAVE_SYS_EPOLL_H

static int
tls_tl_epoll_read_message (struct eXosip_t *excontext, int nfds, struct epoll_event *ep_array)
{
  struct eXtltls *reserved = (struct eXtltls *) excontext->eXtltls_reserved;
  int pos = 0;
  int n;

  if (reserved == NULL) {
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "wrong state: create transport layer first\n"));
    return OSIP_WRONG_STATE;
  }

  for (n = 0; n < nfds; ++n) {

    if (ep_array[n].data.fd == reserved->tls_socket) {
      _tls_read_tls_main_socket (excontext);
      continue;
    }

    for (pos = 0; pos < EXOSIP_MAX_SOCKETS; pos++) {
      if (reserved->socket_tab[pos].socket > 0) {
        if (ep_array[n].data.fd == reserved->socket_tab[pos].socket) {
          if ((ep_array[n].events & EPOLLIN) || ep_array[n].events & EPOLLOUT) {
            int err = -999;
            int max = 5;

            while (err == -999 && max > 0) {
              err = _tls_tl_recv (excontext, &reserved->socket_tab[pos]);
              max--;
            }
          }
        }
      }
    }
  }

  return OSIP_SUCCESS;
}

#endif

static int
tls_tl_read_message (struct eXosip_t *excontext, fd_set * osip_fdset, fd_set * osip_wrset)
{
  struct eXtltls *reserved = (struct eXtltls *) excontext->eXtltls_reserved;
  int pos = 0;

  if (reserved == NULL) {
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "wrong state: create transport layer first\n"));
    return OSIP_WRONG_STATE;
  }

  if (FD_ISSET (reserved->tls_socket, osip_fdset)) {
    _tls_read_tls_main_socket (excontext);
  }

  for (pos = 0; pos < EXOSIP_MAX_SOCKETS; pos++) {
    if (reserved->socket_tab[pos].socket > 0) {
      if (FD_ISSET (reserved->socket_tab[pos].socket, osip_fdset) || FD_ISSET (reserved->socket_tab[pos].socket, osip_wrset)) {
        int err = -999;
        int max = 5;

        while (err == -999 && max > 0) {
          err = _tls_tl_recv (excontext, &reserved->socket_tab[pos]);
          max--;
        }
      }
    }
  }

  return OSIP_SUCCESS;
}


static int
_tls_tl_find_socket (struct eXosip_t *excontext, char *host, int port)
{
  struct eXtltls *reserved = (struct eXtltls *) excontext->eXtltls_reserved;
  int pos;

  for (pos = 0; pos < EXOSIP_MAX_SOCKETS; pos++) {
    if (reserved->socket_tab[pos].socket != 0) {
      if (0 == osip_strcasecmp (reserved->socket_tab[pos].remote_ip, host)
          && port == reserved->socket_tab[pos].remote_port)
        return pos;
    }
  }
  return -1;
}


static int
_tls_tl_connect_socket (struct eXosip_t *excontext, char *host, int port, int retry, const char *sni_servernameindication)
{
  struct eXtltls *reserved = (struct eXtltls *) excontext->eXtltls_reserved;
  int pos;
  int res;
  struct addrinfo *addrinfo = NULL;
  struct addrinfo *curinfo;
  int sock = -1;
  int ssl_state = 0;
  struct sockaddr selected_ai_addr;
  socklen_t selected_ai_addrlen;

  char src6host[NI_MAXHOST];

  memset (src6host, 0, sizeof (src6host));

  selected_ai_addrlen = 0;
  memset (&selected_ai_addr, 0, sizeof (struct sockaddr));

  for (pos = 0; pos < EXOSIP_MAX_SOCKETS; pos++) {
    if (reserved->socket_tab[pos].socket == 0) {
      break;
    }
  }

  if (pos == EXOSIP_MAX_SOCKETS)
    return -1;

  res = _eXosip_get_addrinfo (excontext, &addrinfo, host, port, IPPROTO_TCP);
  if (res)
    return -1;

  for (curinfo = addrinfo; curinfo; curinfo = curinfo->ai_next) {
    int i;

    if (curinfo->ai_protocol && curinfo->ai_protocol != IPPROTO_TCP)
      continue;

    res = _eXosip_getnameinfo ((struct sockaddr *) curinfo->ai_addr, (socklen_t) curinfo->ai_addrlen, src6host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
    if (res != 0)
      continue;

    i = _tls_tl_find_socket (excontext, src6host, port);
    if (i >= 0) {
      _eXosip_freeaddrinfo (addrinfo);
      return i;
    }
  }

  if (retry > 0)
    return -1;

  for (curinfo = addrinfo; curinfo; curinfo = curinfo->ai_next) {
    int type;

    if (curinfo->ai_protocol && curinfo->ai_protocol != IPPROTO_TCP) {
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "eXosip: Skipping protocol %d\n", curinfo->ai_protocol));
      continue;
    }

    res = _eXosip_getnameinfo ((struct sockaddr *) curinfo->ai_addr, (socklen_t) curinfo->ai_addrlen, src6host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);

    if (res == 0) {
      int i = _tls_tl_find_socket (excontext, src6host, port);

      if (i >= 0) {
        _eXosip_freeaddrinfo (addrinfo);
        return i;
      }
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO1, NULL, "New binding with %s\n", src6host));
    }

    type = curinfo->ai_socktype;
#if defined(SOCK_CLOEXEC)
    type = SOCK_CLOEXEC | type;
#endif
    sock = (int) socket (curinfo->ai_family, type, curinfo->ai_protocol);
    if (sock < 0) {
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "eXosip: Cannot create socket %s!\n", strerror (ex_errno)));
      continue;
    }

    if (curinfo->ai_family == AF_INET6) {
#ifdef IPV6_V6ONLY
      if (setsockopt_ipv6only (sock)) {
        _eXosip_closesocket (sock);
        sock = -1;
        OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "eXosip: Cannot set socket option %s!\n", strerror (ex_errno)));
        continue;
      }
#endif /* IPV6_V6ONLY */
    }

    if (reserved->ai_addr_len > 0) {
      if (excontext->reuse_tcp_port > 0) {
        struct sockaddr_storage ai_addr;
        int valopt = 1;

        setsockopt (sock, SOL_SOCKET, SO_REUSEADDR, (void *) &valopt, sizeof (valopt));

        memcpy (&ai_addr, &reserved->ai_addr, reserved->ai_addr_len);
        if (ai_addr.ss_family == AF_INET)
          ((struct sockaddr_in *) &ai_addr)->sin_port = htons (excontext->eXtl_transport.proto_local_port);
        else
          ((struct sockaddr_in6 *) &ai_addr)->sin6_port = htons (excontext->eXtl_transport.proto_local_port);
        res = bind (sock, (const struct sockaddr *) &ai_addr, reserved->ai_addr_len);
        if (res < 0) {
          OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_WARNING, NULL, "Cannot bind socket node:%s family:%d %s\n", excontext->eXtl_transport.proto_ifs, ai_addr.ss_family, strerror (ex_errno)));
        }
      }
      else if (excontext->oc_local_address[0] == '\0') {
        if (reserved->ai_addr.ss_family == curinfo->ai_family) {
          struct sockaddr_storage ai_addr;
          int count = 0;

          memcpy (&ai_addr, &reserved->ai_addr, reserved->ai_addr_len);
          while (count < 100) {
            if (excontext->oc_local_port_range[0] < 1024) {
              if (ai_addr.ss_family == AF_INET)
                ((struct sockaddr_in *) &ai_addr)->sin_port = htons (0);
              else
                ((struct sockaddr_in6 *) &ai_addr)->sin6_port = htons (0);
            }
            else {
              if (excontext->oc_local_port_current == 0)
                excontext->oc_local_port_current = excontext->oc_local_port_range[0];
              /* reset value */
              if (excontext->oc_local_port_current >= excontext->oc_local_port_range[1])
                excontext->oc_local_port_current = excontext->oc_local_port_range[0];

              if (ai_addr.ss_family == AF_INET)
                ((struct sockaddr_in *) &ai_addr)->sin_port = htons (excontext->oc_local_port_current);
              else
                ((struct sockaddr_in6 *) &ai_addr)->sin6_port = htons (excontext->oc_local_port_current);

            }
            res = bind (sock, (const struct sockaddr *) &ai_addr, reserved->ai_addr_len);
            if (res < 0) {
              OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_WARNING, NULL, "Cannot bind socket node:%s family:%d (port=%i) %s\n", excontext->eXtl_transport.proto_ifs, ai_addr.ss_family, excontext->oc_local_port_current, strerror (ex_errno)));
              count++;
              if (excontext->oc_local_port_range[0] >= 1024)
                excontext->oc_local_port_current++;
              continue;
            }
            if (excontext->oc_local_port_range[0] >= 1024)
              excontext->oc_local_port_current++;
            break;
          }
        }
      }
      else {
        int count = 0;

        if (excontext->oc_local_port_range[0] < 1024)
          excontext->oc_local_port_range[0] = 0;
        while (count < 100) {
          struct addrinfo *oc_addrinfo = NULL;
          struct addrinfo *oc_curinfo;

          if (excontext->oc_local_port_current == 0)
            excontext->oc_local_port_current = excontext->oc_local_port_range[0];
          if (excontext->oc_local_port_current >= excontext->oc_local_port_range[1])
            excontext->oc_local_port_current = excontext->oc_local_port_range[0];

          _eXosip_get_addrinfo (excontext, &oc_addrinfo, excontext->oc_local_address, excontext->oc_local_port_current, IPPROTO_TCP);

          for (oc_curinfo = oc_addrinfo; oc_curinfo; oc_curinfo = oc_curinfo->ai_next) {
            if (oc_curinfo->ai_protocol && oc_curinfo->ai_protocol != IPPROTO_TCP) {
              OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "Skipping protocol %d\n", oc_curinfo->ai_protocol));
              continue;
            }
            break;
          }
          if (oc_curinfo == NULL) {
            OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "not able to find any address to bind\n"));
            _eXosip_freeaddrinfo (oc_addrinfo);
            break;
          }
          res = bind (sock, (const struct sockaddr *) oc_curinfo->ai_addr, (socklen_t) oc_curinfo->ai_addrlen);
          if (res < 0) {
            OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_WARNING, NULL, "Cannot bind socket node:%s family:%d (port=%i) %s\n", excontext->oc_local_address, curinfo->ai_addr->sa_family, excontext->oc_local_port_current, strerror (ex_errno)));
            count++;
            if (excontext->oc_local_port_range[0] != 0)
              excontext->oc_local_port_current++;
            continue;
          }
          _eXosip_freeaddrinfo (oc_addrinfo);
          if (excontext->oc_local_port_range[0] != 0)
            excontext->oc_local_port_current++;
          break;
        }
      }
    }


#if defined(HAVE_WINSOCK2_H)
    {
      unsigned long nonBlock = 1;
      int val;

      ioctlsocket (sock, FIONBIO, &nonBlock);

      val = 1;
      if (setsockopt (sock, SOL_SOCKET, SO_KEEPALIVE, (char *) &val, sizeof (val)) == -1) {
        OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "Cannot set socket SO_KEEPALIVE!\n!\n"));
      }
    }
#ifdef HAVE_MSTCPIP_H
    {
      DWORD err = 0L;
      DWORD dwBytes = 0L;
      struct tcp_keepalive kalive = { 0 };
      struct tcp_keepalive kaliveOut = { 0 };
      kalive.onoff = 1;
      kalive.keepalivetime = 30000;     /* Keep Alive in 5.5 sec. */
      kalive.keepaliveinterval = 3000;  /* Resend if No-Reply */
      err = WSAIoctl (sock, SIO_KEEPALIVE_VALS, &kalive, sizeof (kalive), &kaliveOut, sizeof (kaliveOut), &dwBytes, NULL, NULL);
      if (err != 0) {
        OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_WARNING, NULL, "Cannot set keepalive interval!\n"));
      }
    }
#endif
#else
    {
      int val;

      val = fcntl (sock, F_GETFL);
      if (val < 0) {
        _eXosip_closesocket (sock);
        sock = -1;
        OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "Cannot get socket flag!\n"));
        continue;
      }
      val |= O_NONBLOCK;
      if (fcntl (sock, F_SETFL, val) < 0) {
        _eXosip_closesocket (sock);
        sock = -1;
        OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "Cannot set socket flag!\n"));
        continue;
      }
#if SO_KEEPALIVE
      val = 1;
      if (setsockopt (sock, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof (val)) == -1) {
      }
#endif
#if 0
      val = 30;                 /* 30 sec before starting probes */
      setsockopt (sock, SOL_TCP, TCP_KEEPIDLE, &val, sizeof (val));
      val = 2;                  /* 2 probes max */
      setsockopt (sock, SOL_TCP, TCP_KEEPCNT, &val, sizeof (val));
      val = 10;                 /* 10 seconds between each probe */
      setsockopt (sock, SOL_TCP, TCP_KEEPINTVL, &val, sizeof (val));
#endif
#if SO_NOSIGPIPE
      val = 1;
      setsockopt (sock, SOL_SOCKET, SO_NOSIGPIPE, (void *) &val, sizeof (int));
#endif

#if TCP_NODELAY
      val = 1;
      if (setsockopt (sock, IPPROTO_TCP, TCP_NODELAY, (char *) &val, sizeof (int)) != 0) {
        OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "Cannot set socket flag (TCP_NODELAY)\n"));
      }
#endif
    }
#endif

    _eXosip_transport_set_dscp (excontext, curinfo->ai_family, sock);

    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "eXosip: socket node:%s , socket %d, family:%d set to non blocking mode\n", host, sock, curinfo->ai_family));
    res = connect (sock, curinfo->ai_addr, (socklen_t) curinfo->ai_addrlen);
    if (res < 0) {
#ifdef HAVE_WINSOCK2_H
      if (ex_errno != WSAEWOULDBLOCK) {
#else
      if (ex_errno != EINPROGRESS) {
#endif
        OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "Cannot connect socket node:%s family:%d %s[%d]\n", host, curinfo->ai_family, strerror (ex_errno), ex_errno));

        _eXosip_closesocket (sock);
        sock = -1;
        continue;
      }
      else {
        res = _tls_tl_is_connected (excontext->poll_method, sock);
        if (res > 0) {
          OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "socket node:%s, socket %d [pos=%d], family:%d, in progress\n", host, sock, pos, curinfo->ai_family));
          selected_ai_addrlen = (socklen_t) curinfo->ai_addrlen;
          memcpy (&selected_ai_addr, curinfo->ai_addr, sizeof (struct sockaddr));
          break;
        }
        else if (res == 0) {
#ifdef MULTITASKING_ENABLED
          reserved->socket_tab[pos].readStream = NULL;
          reserved->socket_tab[pos].writeStream = NULL;
          CFStreamCreatePairWithSocket (kCFAllocatorDefault, sock, &reserved->socket_tab[pos].readStream, &reserved->socket_tab[pos].writeStream);
          if (reserved->socket_tab[pos].readStream != NULL)
            CFReadStreamSetProperty (reserved->socket_tab[pos].readStream, kCFStreamNetworkServiceType, kCFStreamNetworkServiceTypeVoIP);
          if (reserved->socket_tab[pos].writeStream != NULL)
            CFWriteStreamSetProperty (reserved->socket_tab[pos].writeStream, kCFStreamNetworkServiceType, kCFStreamNetworkServiceTypeVoIP);
          if (CFReadStreamOpen (reserved->socket_tab[pos].readStream)) {
            OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "CFReadStreamOpen Succeeded!\n"));
          }

          CFWriteStreamOpen (reserved->socket_tab[pos].writeStream);
#endif
          OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "socket node:%s , socket %d [pos=%d], family:%d, connected\n", host, sock, pos, curinfo->ai_family));
          selected_ai_addrlen = 0;
          memcpy (&selected_ai_addr, curinfo->ai_addr, sizeof (struct sockaddr));
          ssl_state = 1;
          break;
        }
        else {
          _eXosip_closesocket (sock);
          sock = -1;
          continue;
        }
      }
    }

    break;
  }

  _eXosip_freeaddrinfo (addrinfo);

  if (sock > 0) {
    reserved->socket_tab[pos].socket = sock;

    reserved->socket_tab[pos].ai_addrlen = selected_ai_addrlen;
    memset (&reserved->socket_tab[pos].ai_addr, 0, sizeof (struct sockaddr));
    if (selected_ai_addrlen > 0)
      memcpy (&reserved->socket_tab[pos].ai_addr, &selected_ai_addr, selected_ai_addrlen);

    if (src6host[0] == '\0')
      osip_strncpy (reserved->socket_tab[pos].remote_ip, host, sizeof (reserved->socket_tab[pos].remote_ip) - 1);
    else
      osip_strncpy (reserved->socket_tab[pos].remote_ip, src6host, sizeof (reserved->socket_tab[pos].remote_ip) - 1);

    reserved->socket_tab[pos].remote_port = port;
    reserved->socket_tab[pos].ssl_conn = NULL;
    reserved->socket_tab[pos].ssl_state = ssl_state;
    reserved->socket_tab[pos].ssl_ctx = NULL;

    /* sni should be set to the domain portion of the "Application Unique String (AUS)" */
    /* Usually, this should end up being the domain of the "From" header (but if a Route is set in exosip, it will be the domain from the Route) */
    /* this code prevents Man-In-The-Middle Attack where the attacker is modifying the NAPTR result to route the request somewhere else */
    if (sni_servernameindication !=NULL)
      osip_strncpy (reserved->socket_tab[pos].sni_servernameindication, sni_servernameindication, sizeof (reserved->socket_tab[pos].sni_servernameindication) - 1);
    else
      osip_strncpy(reserved->socket_tab[pos].sni_servernameindication, host, sizeof(reserved->socket_tab[pos].sni_servernameindication) - 1);

    {
      struct sockaddr_storage local_ai_addr;
      socklen_t selected_ai_addrlen;

      memset (&local_ai_addr, 0, sizeof (struct sockaddr_storage));
      selected_ai_addrlen = sizeof (struct sockaddr_storage);
      res = getsockname (sock, (struct sockaddr *) &local_ai_addr, &selected_ai_addrlen);
      if (res == 0) {
        if (local_ai_addr.ss_family == AF_INET)
          reserved->socket_tab[pos].ephemeral_port = ntohs (((struct sockaddr_in *) &local_ai_addr)->sin_port);
        else
          reserved->socket_tab[pos].ephemeral_port = ntohs (((struct sockaddr_in6 *) &local_ai_addr)->sin6_port);
        OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "Outgoing socket created on port %i!\n", reserved->socket_tab[pos].ephemeral_port));
      }
    }

    reserved->socket_tab[pos].tcp_inprogress_max_timeout = osip_getsystemtime (NULL) + 32;

#ifdef HAVE_SYS_EPOLL_H
    if (excontext->poll_method == EXOSIP_USE_EPOLL_LT) {
      struct epoll_event ev;

      memset(&ev, 0, sizeof(struct epoll_event));
      ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
      ev.data.fd = sock;
      res = epoll_ctl (excontext->epfd, EPOLL_CTL_ADD, sock, &ev);
      if (res < 0) {
        _tls_tl_close_sockinfo (&reserved->socket_tab[pos]);
        return -1;
      }
    }
#endif

    if (reserved->socket_tab[pos].ssl_state == 1) {     /* TCP connected but not TLS connected */
      res = _tls_tl_ssl_connect_socket (excontext, &reserved->socket_tab[pos]);
      if (res < 0) {
        _tls_tl_close_sockinfo (&reserved->socket_tab[pos]);
        return -1;
      }
    }
    return pos;
  }

  return -1;
}

static int
_tls_tl_update_contact (struct eXosip_t *excontext, osip_message_t * req, char *natted_ip, int natted_port)
{
  if (req->application_data != (void *) 0x1)
    return OSIP_SUCCESS;

  if ((natted_ip != NULL && natted_ip[0] != '\0') || natted_port > 0) {
    osip_list_iterator_t it;
    osip_contact_t *co = (osip_contact_t *) osip_list_get_first (&req->contacts, &it);

    while (co != NULL) {
      if (co != NULL && co->url != NULL && co->url->host != NULL) {
        if (natted_port > 0) {
          if (co->url->port)
            osip_free (co->url->port);
          co->url->port = (char *) osip_malloc (10);
          snprintf (co->url->port, 9, "%i", natted_port);
          osip_message_force_update (req);
        }
        if (natted_ip != NULL && natted_ip[0] != '\0') {
          osip_free (co->url->host);
          co->url->host = osip_strdup (natted_ip);
          osip_message_force_update (req);
        }
      }
      co = (osip_contact_t *) osip_list_get_next (&it);
    }
  }

  return OSIP_SUCCESS;
}

static int
tls_tl_send_message (struct eXosip_t *excontext, osip_transaction_t * tr, osip_message_t * sip, char *host, int port, int out_socket)
{
  struct eXtltls *reserved = (struct eXtltls *) excontext->eXtltls_reserved;
  size_t length = 0;
  char *message;
  char *ptr;
  int i;

  int pos;
  osip_naptr_t *naptr_record = NULL;

  SSL *ssl = NULL;

  if (reserved == NULL) {
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "wrong state: create transport layer first\n"));
    return OSIP_WRONG_STATE;
  }

  if (host == NULL) {
    host = sip->req_uri->host;
    if (sip->req_uri->port != NULL)
      port = osip_atoi (sip->req_uri->port);
    else
      port = 5061;
  }

  if (port == 5060)
    port = 5061;

  i = -1;
  if (tr == NULL) {
    _eXosip_srv_lookup (excontext, sip, &naptr_record);

    if (naptr_record != NULL) {
      eXosip_dnsutils_dns_process (naptr_record, 1);
      if (naptr_record->naptr_state == OSIP_NAPTR_STATE_NAPTRDONE || naptr_record->naptr_state == OSIP_NAPTR_STATE_SRVINPROGRESS)
        eXosip_dnsutils_dns_process (naptr_record, 1);
    }

    if (naptr_record != NULL && naptr_record->naptr_state == OSIP_NAPTR_STATE_SRVDONE) {
      /* 4: check if we have the one we want... */
      if (naptr_record->siptls_record.name[0] != '\0' && naptr_record->siptls_record.srventry[naptr_record->siptls_record.index].srv[0] != '\0') {
        /* always choose the first here.
           if a network error occur, remove first entry and
           replace with next entries.
         */
        osip_srv_entry_t *srv;

        srv = &naptr_record->siptls_record.srventry[naptr_record->siptls_record.index];
        if (srv->ipaddress[0]) {
          host = srv->ipaddress;
          port = srv->port;
        }
        else {
          host = srv->srv;
          port = srv->port;
        }
      }
    }

    if (naptr_record != NULL && naptr_record->keep_in_cache == 0)
      osip_free (naptr_record);
    naptr_record = NULL;
  }
  else {
    naptr_record = tr->naptr_record;
  }

  if (naptr_record != NULL) {
    /* 1: make sure there is no pending DNS */
    eXosip_dnsutils_dns_process (naptr_record, 0);
    if (naptr_record->naptr_state == OSIP_NAPTR_STATE_NAPTRDONE || naptr_record->naptr_state == OSIP_NAPTR_STATE_SRVINPROGRESS)
      eXosip_dnsutils_dns_process (naptr_record, 0);

    if (naptr_record->naptr_state == OSIP_NAPTR_STATE_UNKNOWN) {
      /* fallback to DNS A */
      if (naptr_record->keep_in_cache == 0)
        osip_free (naptr_record);
      naptr_record = NULL;
      if (tr != NULL)
        tr->naptr_record = NULL;
      /* must never happen? */
    }
    else if (naptr_record->naptr_state == OSIP_NAPTR_STATE_INPROGRESS) {
      /* 2: keep waiting (naptr answer not received) */
      return OSIP_SUCCESS + 1;
    }
    else if (naptr_record->naptr_state == OSIP_NAPTR_STATE_NAPTRDONE) {
      /* 3: keep waiting (naptr answer received/no srv answer received) */
      return OSIP_SUCCESS + 1;
    }
    else if (naptr_record->naptr_state == OSIP_NAPTR_STATE_SRVINPROGRESS) {
      /* 3: keep waiting (naptr answer received/no srv answer received) */
      return OSIP_SUCCESS + 1;
    }
    else if (naptr_record->naptr_state == OSIP_NAPTR_STATE_SRVDONE) {
      /* 4: check if we have the one we want... */
      if (naptr_record->siptls_record.name[0] != '\0' && naptr_record->siptls_record.srventry[naptr_record->siptls_record.index].srv[0] != '\0') {
        /* always choose the first here.
           if a network error occur, remove first entry and
           replace with next entries.
         */
        osip_srv_entry_t *srv;

        if (MSG_IS_REGISTER (sip) || MSG_IS_OPTIONS (sip)) {
          /* activate the failover capability: for no answer OR 503 */
          if (naptr_record->siptls_record.srventry[naptr_record->siptls_record.index].srv_is_broken.tv_sec > 0) {
            naptr_record->siptls_record.srventry[naptr_record->siptls_record.index].srv_is_broken.tv_sec = 0;
            naptr_record->siptls_record.srventry[naptr_record->siptls_record.index].srv_is_broken.tv_usec = 0;
            if (eXosip_dnsutils_rotate_srv (&naptr_record->siptls_record) > 0) {
              OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO1, NULL,
                                      "Doing TLS failover: %s:%i->%s:%i\n", host, port, naptr_record->siptls_record.srventry[naptr_record->siptls_record.index].srv, naptr_record->siptls_record.srventry[naptr_record->siptls_record.index].port));
            }
          }
        }

        srv = &naptr_record->siptls_record.srventry[naptr_record->siptls_record.index];
        if (srv->ipaddress[0]) {
          host = srv->ipaddress;
          port = srv->port;
        }
        else {
          host = srv->srv;
          port = srv->port;
        }
      }
    }
    else if (naptr_record->naptr_state == OSIP_NAPTR_STATE_NOTSUPPORTED || naptr_record->naptr_state == OSIP_NAPTR_STATE_RETRYLATER) {
      /* 5: fallback to DNS A */
      if (naptr_record->keep_in_cache == 0)
        osip_free (naptr_record);
      naptr_record = NULL;
      if (tr != NULL)
        tr->naptr_record = NULL;
    }
  }

  /* verify all current connections */
  _tls_tl_check_connected (excontext);

  if (out_socket > 0) {
    for (pos = 0; pos < EXOSIP_MAX_SOCKETS; pos++) {
      if (reserved->socket_tab[pos].socket != 0) {
        if (reserved->socket_tab[pos].socket == out_socket) {
          out_socket = reserved->socket_tab[pos].socket;
          ssl = reserved->socket_tab[pos].ssl_conn;
          OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO1, NULL, "reusing REQUEST connection (to dest=%s:%i)\n", reserved->socket_tab[pos].remote_ip, reserved->socket_tab[pos].remote_port));
          break;
        }
      }
    }
    if (pos == EXOSIP_MAX_SOCKETS)
      out_socket = 0;

    if (out_socket > 0) {
      int pos2;

      /* If we have SEVERAL sockets to same destination with different port
         number, we search for the one with "SAME port" number.
         The specification is not clear about re-using the existing transaction
         in that use-case...
         Such test, will help mainly with server having 2 sockets: one for
         incoming transaction and one for outgoing transaction?
       */
      pos2 = _tls_tl_find_socket (excontext, host, port);
      if (pos2 >= 0) {
        out_socket = reserved->socket_tab[pos2].socket;
        pos = pos2;
        OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO1, NULL, "reusing connection --with exact port--: (to dest=%s:%i)\n", reserved->socket_tab[pos].remote_ip, reserved->socket_tab[pos].remote_port));
      }
    }
  }

  /* Step 1: find existing socket to send message */
  if (out_socket <= 0) {
    pos = _tls_tl_find_socket (excontext, host, port);

    /* Step 2: create new socket with host:port */
    if (pos < 0) {
      const char *sni = NULL;
      if (naptr_record != NULL) {
        sni = naptr_record->domain;
      }
      if (tr == NULL) {
        pos = _tls_tl_connect_socket (excontext, host, port, 0, sni);
        OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO1, NULL, "message out of transaction: trying to send to %s:%i\n", host, port));
        if (pos < 0) {
          return -1;
        }
      }
      else {
        pos = _tls_tl_connect_socket (excontext, host, port, 0, sni);
        if (pos < 0) {
          if (naptr_record != NULL && MSG_IS_REGISTER (sip)) {
            if (eXosip_dnsutils_rotate_srv (&naptr_record->siptls_record) > 0) {
              /* reg_call_id is not set! */
              _eXosip_mark_registration_expired (excontext, sip->call_id->number);
              if (pos >= 0)
                _tls_tl_close_sockinfo (&reserved->socket_tab[pos]);
              OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO1, NULL,
                                      "Doing TLS failover: %s:%i->%s:%i\n", host, port, naptr_record->siptls_record.srventry[naptr_record->siptls_record.index].srv, naptr_record->siptls_record.srventry[naptr_record->siptls_record.index].port));
            }
          }
          return -1;
        }
      }
    }
    if (pos >= 0) {

      if (MSG_IS_REGISTER (sip)) {
        /* this value is saved: when a connection breaks, we will ask to retry the registration */
        snprintf (reserved->socket_tab[pos].reg_call_id, sizeof (reserved->socket_tab[pos].reg_call_id), "%s", sip->call_id->number);
      }
      out_socket = reserved->socket_tab[pos].socket;
      ssl = reserved->socket_tab[pos].ssl_conn;
    }
  }

  if (out_socket <= 0) {
    if (naptr_record != NULL && MSG_IS_REGISTER (sip)) {
      if (eXosip_dnsutils_rotate_srv (&naptr_record->siptls_record) > 0) {
        /* reg_call_id is not set! */
        _eXosip_mark_registration_expired (excontext, sip->call_id->number);
        OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO1, NULL,
                                "Doing TLS failover: %s:%i->%s:%i\n", host, port, naptr_record->siptls_record.srventry[naptr_record->siptls_record.index].srv, naptr_record->siptls_record.srventry[naptr_record->siptls_record.index].port));
      }
    }
    return -1;
  }

  if (reserved->socket_tab[pos].ssl_state == 0) {
    i = _tls_tl_is_connected (excontext->poll_method, out_socket);
    if (i > 0) {
      time_t now;

      if (tr != NULL) {
        now = osip_getsystemtime (NULL);
        OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "socket node:%s, socket %d [pos=%d], in progress\n", host, out_socket, pos));
        if (tr != NULL && now - tr->birth_time > 10) {
          if (naptr_record != NULL && (MSG_IS_REGISTER (sip) || MSG_IS_OPTIONS (sip))) {
            if (eXosip_dnsutils_rotate_srv (&naptr_record->siptls_record) > 0) {
              _eXosip_mark_registration_expired (excontext, reserved->socket_tab[pos].reg_call_id);
              if (pos >= 0)
                _tls_tl_close_sockinfo (&reserved->socket_tab[pos]);
              OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO1, NULL,
                                      "Doing TLS failover: %s:%i->%s:%i\n", host, port, naptr_record->siptls_record.srventry[naptr_record->siptls_record.index].srv, naptr_record->siptls_record.srventry[naptr_record->siptls_record.index].port));
              return -1;
            }
          }
          return -1;
        }
      }
      return 1;
    }
    else if (i == 0) {
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "socket node:%s , socket %d [pos=%d], connected\n", host, out_socket, pos));
      reserved->socket_tab[pos].ssl_state = 1;
      reserved->socket_tab[pos].ai_addrlen = 0;
    }
    else {
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "socket node:%s, socket %d [pos=%d], socket error\n", host, out_socket, pos));
      _tls_tl_close_sockinfo (&reserved->socket_tab[pos]);
      return -1;
    }
  }

  if (reserved->socket_tab[pos].ssl_state == 1) {       /* TCP connected but not TLS connected */
    i = _tls_tl_ssl_connect_socket (excontext, &reserved->socket_tab[pos]);
    if (i < 0) {
      _tls_tl_close_sockinfo (&reserved->socket_tab[pos]);
      return -1;
    }
    else if (i > 0) {
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "socket node:%s, socket %d [pos=%d], connected (ssl in progress)\n", host, out_socket, pos));
      return 1;
    }
    ssl = reserved->socket_tab[pos].ssl_conn;
  }

  if (ssl == NULL) {
    return -1;
  }

#ifdef MULTITASKING_ENABLED
  if (reserved->socket_tab[pos].readStream == NULL) {
    reserved->socket_tab[pos].readStream = NULL;
    reserved->socket_tab[pos].writeStream = NULL;
    CFStreamCreatePairWithSocket (kCFAllocatorDefault, reserved->socket_tab[pos].socket, &reserved->socket_tab[pos].readStream, &reserved->socket_tab[pos].writeStream);
    if (reserved->socket_tab[pos].readStream != NULL)
      CFReadStreamSetProperty (reserved->socket_tab[pos].readStream, kCFStreamNetworkServiceType, kCFStreamNetworkServiceTypeVoIP);
    if (reserved->socket_tab[pos].writeStream != NULL)
      CFWriteStreamSetProperty (reserved->socket_tab[pos].writeStream, kCFStreamNetworkServiceType, kCFStreamNetworkServiceTypeVoIP);
    if (CFReadStreamOpen (reserved->socket_tab[pos].readStream)) {
      OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "CFReadStreamOpen Succeeded!\n"));
    }

    CFWriteStreamOpen (reserved->socket_tab[pos].writeStream);
  }
#endif

  _eXosip_request_viamanager (excontext, sip, reserved->socket_tab[pos].ai_addr.sa_family, IPPROTO_TCP, NULL, reserved->socket_tab[pos].ephemeral_port, reserved->socket_tab[pos].socket, host);
  if (excontext->use_ephemeral_port == 1)
    _eXosip_message_contactmanager (excontext, sip, reserved->socket_tab[pos].ai_addr.sa_family, IPPROTO_TCP, NULL, reserved->socket_tab[pos].ephemeral_port, reserved->socket_tab[pos].socket, host);
  else
    _eXosip_message_contactmanager (excontext, sip, reserved->socket_tab[pos].ai_addr.sa_family, IPPROTO_TCP, NULL, excontext->eXtl_transport.proto_local_port, reserved->socket_tab[pos].socket, host);
  if (excontext->tls_firewall_ip[0] != '\0' || excontext->auto_masquerade_contact > 0)
    _tls_tl_update_contact (excontext, sip, reserved->socket_tab[pos].natted_ip, reserved->socket_tab[pos].natted_port);

  /* remove preloaded route if there is no tag in the To header
   */
  {
    osip_route_t *route = NULL;
    osip_generic_param_t *tag = NULL;

    if (excontext->remove_prerouteset > 0) {
      osip_message_get_route (sip, 0, &route);
      osip_to_get_tag (sip->to, &tag);
      if (tag == NULL && route != NULL && route->url != NULL) {
        osip_list_remove (&sip->routes, 0);
        osip_message_force_update (sip);
      }
    }
    i = osip_message_to_str (sip, &message, &length);
    if (tag == NULL && route != NULL && route->url != NULL) {
      osip_list_add (&sip->routes, route, 0);
    }
  }

  if (i != 0 || length <= 0) {
    return -1;
  }

  OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO1, NULL, "Message sent: ([length=%d] to dest=%s:%i) \n%s\n", length, host, port, message));

  if (pos >= 0 && excontext->enable_dns_cache == 1 && osip_strcasecmp (host, reserved->socket_tab[pos].remote_ip) != 0 && MSG_IS_REQUEST (sip)) {
    if (MSG_IS_REGISTER (sip)) {
      struct eXosip_dns_cache entry;

      memset (&entry, 0, sizeof (struct eXosip_dns_cache));
      snprintf (entry.host, sizeof (entry.host), "%s", host);
      snprintf (entry.ip, sizeof (entry.ip), "%s", reserved->socket_tab[pos].remote_ip);
      eXosip_set_option (excontext, EXOSIP_OPT_ADD_DNS_CACHE, (void *) &entry);
    }
  }

  SSL_set_mode (ssl, SSL_MODE_AUTO_RETRY);

  ptr = message;
  while (length > 0) {
#if TARGET_OS_IPHONE            /* avoid ssl error on large message */
    int max = (length > 500) ? 500 : length;

    i = SSL_write (ssl, (const void *) ptr, (int) max);
#else
    i = SSL_write (ssl, (const void *) ptr, (int) length);
#endif
    if (i <= 0) {
      i = SSL_get_error (ssl, i);
      if (i == SSL_ERROR_WANT_READ || i == SSL_ERROR_WANT_WRITE)
        continue;
      _tls_print_ssl_error (i);

      osip_free (message);
      if (pos >= 0)
        _tls_tl_close_sockinfo (&reserved->socket_tab[pos]);
      return -1;
    }
    length = length - i;
    ptr += i;
  }

  osip_free (message);

  if (tr != NULL && MSG_IS_REGISTER (sip) && pos >= 0) {
    /* start a timeout to destroy connection if no answer */
    reserved->socket_tab[pos].tcp_max_timeout = osip_getsystemtime (NULL) + 32;
  }

  return OSIP_SUCCESS;
}

static int
tls_tl_keepalive (struct eXosip_t *excontext)
{
  struct eXtltls *reserved = (struct eXtltls *) excontext->eXtltls_reserved;
  char buf[5] = "\r\n\r\n";
  int pos;
  int i;

  if (reserved == NULL) {
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "wrong state: create transport layer first\n"));
    return OSIP_WRONG_STATE;
  }

  if (reserved->tls_socket <= 0)
    return OSIP_UNDEFINED_ERROR;

  for (pos = 0; pos < EXOSIP_MAX_SOCKETS; pos++) {

    if (excontext->ka_interval > 0) {
      if (reserved->socket_tab[pos].socket > 0 && reserved->socket_tab[pos].ssl_state > 2) {
        SSL_set_mode (reserved->socket_tab[pos].ssl_conn, SSL_MODE_AUTO_RETRY);

        while (1) {
          i = SSL_write (reserved->socket_tab[pos].ssl_conn, (const void *) buf, 4);

          if (i <= 0) {
            i = SSL_get_error (reserved->socket_tab[pos].ssl_conn, i);
            if (i == SSL_ERROR_WANT_READ || i == SSL_ERROR_WANT_WRITE)
              continue;
            _tls_print_ssl_error (i);
          }
          break;
        }
      }
    }

  }
  return OSIP_SUCCESS;
}

static int
tls_tl_set_socket (struct eXosip_t *excontext, int socket)
{
  struct eXtltls *reserved = (struct eXtltls *) excontext->eXtltls_reserved;

  if (reserved == NULL) {
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "wrong state: create transport layer first\n"));
    return OSIP_WRONG_STATE;
  }

  reserved->tls_socket = socket;

  return OSIP_SUCCESS;
}

static int
tls_tl_masquerade_contact (struct eXosip_t *excontext, const char *public_address, int port)
{
  if (public_address == NULL || public_address[0] == '\0') {
    memset (excontext->tls_firewall_ip, '\0', sizeof (excontext->tls_firewall_ip));
    memset (excontext->tls_firewall_port, '\0', sizeof (excontext->tls_firewall_port));
    return OSIP_SUCCESS;
  }
  snprintf (excontext->tls_firewall_ip, sizeof (excontext->tls_firewall_ip), "%s", public_address);
  if (port > 0) {
    snprintf (excontext->tls_firewall_port, sizeof (excontext->tls_firewall_port), "%i", port);
  }
  return OSIP_SUCCESS;
}

static int
tls_tl_get_masquerade_contact (struct eXosip_t *excontext, char *ip, int ip_size, char *port, int port_size)
{
  struct eXtltls *reserved = (struct eXtltls *) excontext->eXtltls_reserved;

  memset (ip, 0, ip_size);
  memset (port, 0, port_size);

  if (reserved == NULL) {
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "wrong state: create transport layer first\n"));
    return OSIP_WRONG_STATE;
  }

  if (excontext->tls_firewall_ip[0] != '\0')
    snprintf (ip, ip_size, "%s", excontext->tls_firewall_ip);

  if (excontext->tls_firewall_port[0] != '\0')
    snprintf (port, port_size, "%s", excontext->tls_firewall_port);
  return OSIP_SUCCESS;
}


static int
tls_tl_update_contact (struct eXosip_t *excontext, osip_message_t * req)
{
  req->application_data = (void *) 0x1; /* request for masquerading */
  return OSIP_SUCCESS;
}

static int
tls_tl_check_connection (struct eXosip_t *excontext)
{
  struct eXtltls *reserved = (struct eXtltls *) excontext->eXtltls_reserved;
  int pos;

  if (reserved == NULL) {
    OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_ERROR, NULL, "wrong state: create transport layer first\n"));
    return OSIP_WRONG_STATE;
  }

  if (reserved->tls_socket <= 0)
    return OSIP_UNDEFINED_ERROR;

  for (pos = 0; pos < EXOSIP_MAX_SOCKETS; pos++) {

    if (reserved->socket_tab[pos].socket > 0 && reserved->socket_tab[pos].ssl_state > 2)
      reserved->socket_tab[pos].tcp_inprogress_max_timeout = 0; /* reset value */

    if (reserved->socket_tab[pos].socket > 0 && reserved->socket_tab[pos].ssl_state <= 2 && reserved->socket_tab[pos].tcp_inprogress_max_timeout > 0) {
      time_t now = osip_getsystemtime (NULL);

      if (now > reserved->socket_tab[pos].tcp_inprogress_max_timeout) {
        OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "tls_tl_check_connection socket is in progress since 32 seconds / close socket\n"));
        reserved->socket_tab[pos].tcp_inprogress_max_timeout = 0;
        _eXosip_mark_registration_expired (excontext, reserved->socket_tab[pos].reg_call_id);
        _tls_tl_close_sockinfo (&reserved->socket_tab[pos]);
        continue;
      }
    }

    if (reserved->socket_tab[pos].socket > 0 && reserved->socket_tab[pos].ssl_state > 2 && reserved->socket_tab[pos].tcp_max_timeout > 0) {
      time_t now = osip_getsystemtime (NULL);

      if (now > reserved->socket_tab[pos].tcp_max_timeout) {
        OSIP_TRACE (osip_trace (__FILE__, __LINE__, OSIP_INFO2, NULL, "tls_tl_check_connection we expected a reply on established sockets / close socket\n"));
        reserved->socket_tab[pos].tcp_max_timeout = 0;
        _eXosip_mark_registration_expired (excontext, reserved->socket_tab[pos].reg_call_id);
        _tls_tl_close_sockinfo (&reserved->socket_tab[pos]);
        continue;
      }
    }

    if (reserved->socket_tab[pos].socket > 0 && reserved->socket_tab[pos].ssl_state > 2 && reserved->socket_tab[pos].tcp_max_timeout == 0) {
      int i;

      i = _tls_tl_is_connected (excontext->poll_method, reserved->socket_tab[pos].socket);
      if (i > 0) {
        OSIP_TRACE (osip_trace
                    (__FILE__, __LINE__, OSIP_INFO2, NULL, "tls_tl_check_connection socket node:%s:%i, socket %d [pos=%d], in progress\n", reserved->socket_tab[pos].remote_ip, reserved->socket_tab[pos].remote_port, reserved->socket_tab[pos].socket, pos));
        continue;
      }
      else if (i == 0) {
        OSIP_TRACE (osip_trace
                    (__FILE__, __LINE__, OSIP_INFO2, NULL, "tls_tl_check_connection socket node:%s:%i , socket %d [pos=%d], connected\n", reserved->socket_tab[pos].remote_ip, reserved->socket_tab[pos].remote_port, reserved->socket_tab[pos].socket, pos));
        continue;
      }
      else {
        OSIP_TRACE (osip_trace
                    (__FILE__, __LINE__, OSIP_ERROR, NULL, "tls_tl_check_connection socket node:%s:%i, socket %d [pos=%d], socket error\n", reserved->socket_tab[pos].remote_ip, reserved->socket_tab[pos].remote_port, reserved->socket_tab[pos].socket,
                     pos));
        _eXosip_mark_registration_expired (excontext, reserved->socket_tab[pos].reg_call_id);
        _tls_tl_close_sockinfo (&reserved->socket_tab[pos]);
        continue;
      }
    }
  }

  return OSIP_SUCCESS;
}

static struct eXtl_protocol eXtl_tls = {
  1,
  5061,
  "TLS",
  "0.0.0.0",
  IPPROTO_TCP,
  AF_INET,
  0,
  0,
  0,

  &tls_tl_init,
  &tls_tl_free,
  &tls_tl_open,
  &tls_tl_set_fdset,
  &tls_tl_read_message,
#ifdef HAVE_SYS_EPOLL_H
  &tls_tl_epoll_read_message,
#endif
  &tls_tl_send_message,
  &tls_tl_keepalive,
  &tls_tl_set_socket,
  &tls_tl_masquerade_contact,
  &tls_tl_get_masquerade_contact,
  &tls_tl_update_contact,
  &tls_tl_reset,
  &tls_tl_check_connection
};

void
eXosip_transport_tls_init (struct eXosip_t *excontext)
{
  memcpy (&excontext->eXtl_transport, &eXtl_tls, sizeof (struct eXtl_protocol));
}

#else

eXosip_tls_ctx_error
eXosip_tls_verify_certificate (struct eXosip_t *excontext, int
                               _tls_verify_client_certificate)
{
  return -1;                    /* NOT IMPLEMENTED */
}

eXosip_tls_ctx_error
eXosip_set_tls_ctx (struct eXosip_t * excontext, eXosip_tls_ctx_t * ctx)
{
  return -1;                    /* NOT IMPLEMENTED */
}

#endif

/**
  Various explanation on using openssl with eXosip2. (Written on June 26, 2018)

  First, you should understand that it is unlikely that you need to configure
  the "server" side of eXosip. eXosip2 is a User-Agent and will receive in 99%
  of use-case incoming message on "client initiated connection". It should even
  be in 100% of use-case. Thus, I advise you to compile eXosip2 without
  #define ENABLE_MAIN_SOCKET. This will prevent people connecting to your User-Agent
  via unsecured TLS connection. I also don't maintain this server side socket code
  security, so if you want to use it, you should really make sure you do things
  correctly. (may be with code modifications?)

  CAUTIOUS: Please understand that if you use an old version of openssl, we will get unsecure
  cipher, vulnerabilities, etc...
  Configuration of client side sockets:

1/ default configuration.
   By default, eXosip2 will load certificates from WINDOWS STORE on WINDOWS ONLY.
   By default, eXosip2 will load certificates from MACOSX STORE on MACOSX ONLY.
   By default, eXosip2 won't verify certificates

2/ on WINDOWS and MACOSX, to enable certificate verification:
   int optval = 1;
   eXosip_set_option (exosip_context, EXOSIP_OPT_SET_TLS_VERIFY_CERTIFICATE, &optval);

3/ on OTHER platforms, you need to add either ONE or a full list of ROOT CERTIFICATES
   and configure to require certificate verification:

   Google is providing a file containing a list of known trusted root certificates. This
   is the easiest way you can retreive an up-to-date file.
   https://pki.google.com/roots.pem
   SIDENOTE: you need to download this file REGULARLY. Because some of the root certificates may
   be revoked.

   eXosip_tls_ctx_t _tls_description;
   memset(&_tls_description, 0, sizeof(eXosip_tls_ctx_t));
   snprintf(_tls_description.root_ca_cert, sizeof(_tls_description.root_ca_cert), "%s", "roots.pem");
   eXosip_set_option(exosip_context, EXOSIP_OPT_SET_TLS_CERTIFICATES_INFO, (void*)&_tls_description);

   int optval = 1;
   eXosip_set_option (exosip_context, EXOSIP_OPT_SET_TLS_VERIFY_CERTIFICATE, &optval);

4/ If your service(server) request a client certificate from you, you will need to configure one
   by configuring your certificate, private key and password.

   eXosip_tls_ctx_t _tls_description;
   memset(&_tls_description, 0, sizeof(eXosip_tls_ctx_t));
   snprintf(_tls_description.root_ca_cert, sizeof(_tls_description.root_ca_cert), "%s", "roots.pem");

   snprintf(_tls_description.client.priv_key_pw, sizeof(_tls_description.client.priv_key_pw), "%s", "hello");
   snprintf(_tls_description.client.priv_key, sizeof(_tls_description.client.priv_key), "%s", "selfsigned-key.pem");
   snprintf(_tls_description.client.cert, sizeof(_tls_description.client.cert), "%s", "selfsigned-cert.pem");

   eXosip_set_option(exosip_context, EXOSIP_OPT_SET_TLS_CERTIFICATES_INFO, (void*)&_tls_description);

5/ Today, I have removed the ability to use a client certificate from windows store: the feature was limited
   to RSA with SHA1 which is never negociated if you wish to have correct security. This makes the feature obsolete
   and mostly not working. So... just removed. EXOSIP_OPT_SET_TLS_CLIENT_CERTIFICATE_NAME
   and EXOSIP_OPT_SET_TLS_SERVER_CERTIFICATE_NAME have been kept, but returns -1 only.

6/ A recent feature has been introduced: Certificate pinning.

   In order to get your public key file in DER format, which is required for eXosip2 code, you
   can use the following command line to retreive the publickey from your certificate and encode
   it into base64:

   $> openssl x509 -in your-base64-certificate.pem -pubkey -noout | openssl enc -base64 -d > publickey.der

   In order to activate the check, you need to configure the "public_key_pinned" parameter:

   eXosip_tls_ctx_t _tls_description;
   memset(&_tls_description, 0, sizeof(eXosip_tls_ctx_t));
   snprintf(_tls_description.root_ca_cert, sizeof(_tls_description.root_ca_cert), "%s", "roots.pem");

   snprintf(_tls_description.client.priv_key_pw, sizeof(_tls_description.client.priv_key_pw), "%s", "hello");
   snprintf(_tls_description.client.priv_key, sizeof(_tls_description.client.priv_key), "%s", "selfsigned-key.pem");
   snprintf(_tls_description.client.cert, sizeof(_tls_description.client.cert), "%s", "selfsigned-cert.pem");

   snprintf(_tls_description.client.cert, sizeof(_tls_description.client.public_key_pinned), "%s", "pub_key.der");

   eXosip_set_option(exosip_context, EXOSIP_OPT_SET_TLS_CERTIFICATES_INFO, (void*)&_tls_description);

7/ Depending on the openssl version you use, you will NOT have the same behavior and features.
   I advise you to use the LATEST openssl version. This is for security purpose (openssl vulnerabilities)
   as well as to use the latest secured cipher list.

   There are other features only enabled or disabled with recent versions of openssl. Among them:
   * SNI server verification: v1.0.2 -> OPENSSL_VERSION_NUMBER >= 0x10002000L
   * RSA is removed since v1.1.0 OPENSSL_VERSION_NUMBER < 0x10100000L
   * ECDHE based cipher suites faster than DHE since 1.0.0 OPENSSL_VERSION_NUMBER > 0x10000000L
   * ...

   If your app accept old/deprecated/unsecure ciphers, please: update your openssl version. If you
   have no choice, you can update the internal code to specify, or remove ciphers. The default in
   eXosip is always the "HIGH:-COMPLEMENTOFDEFAULT" which is expected to be the most secure configuration
   known by openssl upon releasing the openssl version.

     SSL_CTX_set_cipher_list (ctx, "HIGH:-COMPLEMENTOFDEFAULT")

  SIDEINFO for testing purpose: If you wish to test client certifiate with
  kamailio, here is a possible configuration on a specific port number. The
  ca_list contains the selfsigned certificate that is configured on client
  side in eXosip2. (server-key and server-certificate are the server side info)

  [server:91.121.30.149:29091]
  method = TLSv1+
  verify_certificate = no
  require_certificate = yes
  private_key = /etc/kamailio/server-key.key
  certificate = /etc/kamailio/server-certificate.pem
  ca_list = /etc/kamailio/client-selfsigned-cert-aymeric.pem
  cipher_list = HIGH:-COMPLEMENTOFDEFAULT

  */
