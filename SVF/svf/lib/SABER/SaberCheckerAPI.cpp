//===- SaberCheckerAPI.cpp -- API for checkers-------------------------------//
//
//                     SVF: Static Value-Flow Analysis
//
// Copyright (C) <2013->  <Yulei Sui>
//

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.

// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
//===----------------------------------------------------------------------===//

/*
 * SaberCheckerAPI.cpp
 *
 *  Created on: Apr 23, 2014
 *      Author: Yulei Sui
 */
#include "SABER/SaberCheckerAPI.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdio.h>

using namespace std;
using namespace SVF;

SaberCheckerAPI* SaberCheckerAPI::ckAPI = nullptr;

namespace
{

/// string and type pair
struct ei_pair
{
    const char *n;
    SaberCheckerAPI::CHECKER_TYPE t;
};

} // End anonymous namespace

static string trimAPIConfigToken(const string& token)
{
    string::size_type begin = 0;
    string::size_type end = token.size();
    while(begin < end && !isalnum(static_cast<unsigned char>(token[begin])) && token[begin] != '_')
        ++begin;
    while(end > begin && !isalnum(static_cast<unsigned char>(token[end - 1])) && token[end - 1] != '_')
        --end;
    return token.substr(begin, end - begin);
}

static string normalizeAPIConfigToken(string token)
{
    transform(token.begin(), token.end(), token.begin(),
              [](unsigned char ch) { return static_cast<char>(tolower(ch)); });
    return token;
}

static bool parseCheckerTypeToken(const string& token, SaberCheckerAPI::CHECKER_TYPE& type)
{
    const string normalized = normalizeAPIConfigToken(token);
    if(normalized == "ck_alloc" || normalized == "alloc" ||
            normalized == "allocator" || normalized == "malloc")
    {
        type = SaberCheckerAPI::CK_ALLOC;
        return true;
    }
    if(normalized == "ck_free" || normalized == "free" ||
            normalized == "releaser" || normalized == "destroyer" ||
            normalized == "dealloc" || normalized == "deallocator")
    {
        type = SaberCheckerAPI::CK_FREE;
        return true;
    }
    if(normalized == "ck_fopen" || normalized == "fopen" ||
            normalized == "fileopen" || normalized == "file_open")
    {
        type = SaberCheckerAPI::CK_FOPEN;
        return true;
    }
    if(normalized == "ck_fclose" || normalized == "fclose" ||
            normalized == "fileclose" || normalized == "file_close")
    {
        type = SaberCheckerAPI::CK_FCLOSE;
        return true;
    }
    return false;
}

//Each (name, type) pair will be inserted into the map.
//All entries of the same type must occur together (for error detection).
static const ei_pair ei_pairs[]=
{
    {"alloc", SaberCheckerAPI::CK_ALLOC},
    {"alloc_check", SaberCheckerAPI::CK_ALLOC},
    {"alloc_clear", SaberCheckerAPI::CK_ALLOC},
    {"calloc", SaberCheckerAPI::CK_ALLOC},
    {"jpeg_alloc_huff_table", SaberCheckerAPI::CK_ALLOC},
    {"jpeg_alloc_quant_table", SaberCheckerAPI::CK_ALLOC},
    {"lalloc", SaberCheckerAPI::CK_ALLOC},
    {"lalloc_clear", SaberCheckerAPI::CK_ALLOC},
    {"malloc", SaberCheckerAPI::CK_ALLOC},
    {"nhalloc", SaberCheckerAPI::CK_ALLOC},
    {"oballoc", SaberCheckerAPI::CK_ALLOC},
    {"permalloc", SaberCheckerAPI::CK_ALLOC},
    {"png_create_info_struct", SaberCheckerAPI::CK_ALLOC},
    {"png_create_write_struct", SaberCheckerAPI::CK_ALLOC},
    {"safe_calloc", SaberCheckerAPI::CK_ALLOC},
    {"safe_malloc", SaberCheckerAPI::CK_ALLOC},
    {"safecalloc", SaberCheckerAPI::CK_ALLOC},
    {"safemalloc", SaberCheckerAPI::CK_ALLOC},
    {"safexcalloc", SaberCheckerAPI::CK_ALLOC},
    {"safexmalloc", SaberCheckerAPI::CK_ALLOC},
    {"savealloc", SaberCheckerAPI::CK_ALLOC},
    {"xalloc", SaberCheckerAPI::CK_ALLOC},
    {"xcalloc", SaberCheckerAPI::CK_ALLOC},
    {"xmalloc", SaberCheckerAPI::CK_ALLOC},
    {"SSL_CTX_new", SaberCheckerAPI::CK_ALLOC},
    {"SSL_new", SaberCheckerAPI::CK_ALLOC},
    {"VOS_MemAlloc", SaberCheckerAPI::CK_ALLOC},
    /* NSPA_AUTO_OPENSSL_CK_ALLOC_BEGIN */
    /* NSPA: openssl project-specific CK_ALLOC entries */
    {"ASN1_d2i_bio", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, crypto/asn1/a_d2i_fp.c
    {"ASN1_d2i_fp", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, crypto/asn1/a_d2i_fp.c
    {"ASN1_item_dup", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/asn1/a_dup.c
    {"ASN1_item_ex_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, crypto/asn1/tasn_new.c
    {"ASN1_item_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.92, crypto/asn1/tasn_new.c
    {"ASN1_item_new_ex", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.92, crypto/asn1/tasn_new.c
    {"BIO_dup_chain", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.92, crypto/bio/bio_lib.c
    {"BIO_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/bio/bio_lib.c
    {"BIO_new_CMS", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.93, crypto/cms/cms_io.c
    {"BIO_new_NDEF", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/asn1/bio_ndef.c
    {"BIO_new_PKCS7", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.93, crypto/pkcs7/bio_pk7.c
    {"BIO_new_accept", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.93, crypto/bio/bss_acpt.c
    {"BIO_new_bio_dgram_pair", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.92, crypto/bio/bss_dgram_pair.c
    {"BIO_new_bio_pair", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.92, crypto/bio/bss_bio.c
    {"BIO_new_buffer_ssl_connect", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, ssl/bio_ssl.c
    {"BIO_new_connect", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.93, crypto/bio/bss_conn.c
    {"BIO_new_dgram", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/bio/bss_dgram.c
    {"BIO_new_dgram_sctp", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.93, crypto/bio/bss_dgram.c
    {"BIO_new_ex", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/bio/bio_lib.c
    {"BIO_new_file", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/bio/bss_file.c
    {"BIO_new_fp", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/bio/bss_file.c
    {"BIO_new_from_core_bio", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.93, crypto/bio/bss_core.c
    {"BIO_new_mem_buf", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/bio/bss_mem.c
    {"BIO_new_ssl", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, ssl/bio_ssl.c
    {"BIO_new_ssl_connect", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, ssl/bio_ssl.c
    {"CMS_AuthEnvelopedData_create", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.97, crypto/cms/cms_env.c
    {"CMS_AuthEnvelopedData_create_ex", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/cms/cms_env.c
    {"CMS_ContentInfo_new_ex", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.97, crypto/cms/cms_lib.c
    {"CMS_EnvelopedData_create", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.97, crypto/cms/cms_env.c
    {"CMS_EnvelopedData_create_ex", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/cms/cms_env.c
    {"CMS_data_create", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.97, crypto/cms/cms_smime.c
    {"CMS_data_create_ex", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/cms/cms_smime.c
    {"CMS_digest_create", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.97, crypto/cms/cms_smime.c
    {"CMS_digest_create_ex", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/cms/cms_smime.c
    {"CRL_from_strings", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, test/testutil/load.c
    {"CRYPTO_aligned_alloc", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.97, crypto/mem.c
    {"CRYPTO_aligned_alloc_array", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/array_alloc.c
    {"CRYPTO_alloc_ex_data", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, crypto/ex_data.c
    {"CRYPTO_clear_realloc", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.93, crypto/mem.c
    {"CRYPTO_clear_realloc_array", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.88, crypto/array_alloc.c
    {"CRYPTO_malloc", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=1, crypto/mem.c
    {"CRYPTO_malloc_array", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/array_alloc.c
    {"CRYPTO_realloc", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/mem.c
    {"CRYPTO_realloc_array", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.92, crypto/array_alloc.c
    {"CRYPTO_secure_malloc", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.97, crypto/mem_sec.c
    {"CRYPTO_secure_malloc_array", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/array_alloc.c
    {"CTLOG_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/ct/ct_log.c
    {"CTLOG_new_ex", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.97, crypto/ct/ct_log.c
    {"CTLOG_new_from_base64", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.93, crypto/ct/ct_b64.c
    {"CTLOG_new_from_base64_ex", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/ct/ct_b64.c
    {"DH_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.98, crypto/dh/dh_lib.c
    {"DH_new_method", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.97, crypto/dh/dh_lib.c
    {"DHparams_dup", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/dh/dh_ameth.c
    {"DSA_dup_DH", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/dsa/dsa_lib.c
    {"DSA_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.98, crypto/dsa/dsa_lib.c
    {"DSA_new_method", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.97, crypto/dsa/dsa_lib.c
    {"DSAparams_dup", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.97, crypto/dsa/dsa_asn1.c
    {"ECDSA_SIG_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, include/openssl/ec.h
    {"EC_KEY_dup", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.97, crypto/ec/ec_key.c
    {"EC_KEY_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.98, crypto/ec/ec_key.c
    {"EC_KEY_new_by_curve_name", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.98, crypto/ec/ec_key.c
    {"EC_KEY_new_by_curve_name_ex", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.97, crypto/ec/ec_key.c
    {"EC_KEY_new_ex", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.97, crypto/ec/ec_key.c
    {"EC_KEY_new_method", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.97, crypto/ec/ec_kmeth.c
    {"EVP_PKEY_CTX_dup", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.93, crypto/evp/pmeth_lib.c
    {"EVP_PKEY_CTX_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/evp/pmeth_lib.c
    {"EVP_PKEY_CTX_new_from_name", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/evp/pmeth_lib.c
    {"EVP_PKEY_CTX_new_from_pkey", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/evp/pmeth_lib.c
    {"EVP_PKEY_CTX_new_id", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/evp/pmeth_lib.c
    {"EVP_PKEY_new_CMAC_key", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/evp/p_lib.c
    {"EVP_PKEY_new_mac_key", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/evp/pmeth_gn.c
    {"EVP_PKEY_new_raw_private_key", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/evp/p_lib.c
    {"EVP_PKEY_new_raw_private_key_ex", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/evp/p_lib.c
    {"EVP_PKEY_new_raw_public_key", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/evp/p_lib.c
    {"EVP_PKEY_new_raw_public_key_ex", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/evp/p_lib.c
    {"IMPLEMENT_ASN1_DUP_FUNCTION", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.72, crypto/rsa/rsa_asn1.c
    {"LP_find_file", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, crypto/LPdir_unix.c
    {"OCSP_BASICRESP_delete_ext", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.8, crypto/ocsp/ocsp_ext.c
    {"OCSP_ONEREQ_delete_ext", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.8, crypto/ocsp/ocsp_ext.c
    {"OCSP_REQUEST_delete_ext", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.8, crypto/ocsp/ocsp_ext.c
    {"OCSP_SINGLERESP_delete_ext", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.8, crypto/ocsp/ocsp_ext.c
    {"OCSP_accept_responses_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/ocsp/ocsp_ext.c
    {"OCSP_sendreq_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/ocsp/ocsp_http.c
    {"OCSP_url_svcloc_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/ocsp/ocsp_ext.c
    {"OPENSSL_INIT_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.97, crypto/conf/conf_lib.c
    {"OSSL_CMP_CTX_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.99, crypto/cmp/cmp_ctx.c
    {"OSSL_CMP_MSG_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.97, crypto/cmp/cmp_msg.c
    {"OSSL_CMP_SRV_CTX_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/cmp/cmp_server.c
    {"OSSL_CRMF_ENCRYPTEDVALUE_decrypt", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.82, crypto/crmf/crmf_lib.c
    {"OSSL_DECODER_CTX_new_for_pkey", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.93, crypto/encode_decode/decoder_pkey.c
    {"OSSL_DEMO_H3_CONN_new_for_addr", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.88, demos/http3/ossl-nghttp3.c
    {"OSSL_DEMO_H3_CONN_new_for_conn", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, demos/http3/ossl-nghttp3.c
    {"OSSL_ENCODER_CTX_new_for_pkey", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.93, crypto/encode_decode/encoder_pkey.c
    {"OSSL_HTTP_exchange", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.8, crypto/http/http_client.c
    {"OSSL_HTTP_transfer", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.75, crypto/http/http_client.c
    {"OSSL_PARAM_dup", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/params_dup.c
    {"OSSL_STORE_INFO_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/store/store_lib.c
    {"OSSL_STORE_INFO_new_CERT", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/store/store_lib.c
    {"OSSL_STORE_INFO_new_CRL", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/store/store_lib.c
    {"OSSL_STORE_INFO_new_NAME", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/store/store_lib.c
    {"OSSL_STORE_INFO_new_PARAMS", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/store/store_lib.c
    {"OSSL_STORE_INFO_new_PKEY", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/store/store_lib.c
    {"OSSL_STORE_INFO_new_PUBKEY", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/store/store_lib.c
    {"OSSL_STORE_INFO_new_SKEY", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/store/store_lib.c
    {"OSSL_provider_init", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, test/p_test.c
    {"PEM_ASN1_read", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.88, crypto/pem/pem_lib.c
    {"PEM_read_DHparams", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.88, crypto/pem/pem_all.c
    {"PEM_read_PUBKEY_ex", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.82, crypto/pem/pem_pkey.c
    {"PEM_read_PrivateKey_ex", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.82, crypto/pem/pem_pkey.c
    {"PKCS7_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.97, crypto/pkcs7/pk7_asn1.c
    {"PKCS7_new_ex", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/pkcs7/pk7_asn1.c
    {"RSAPrivateKey_dup", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, crypto/rsa/rsa_asn1.c
    {"RSA_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.97, crypto/rsa/rsa_lib.c
    {"RSA_new_method", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/rsa/rsa_lib.c
    {"SMIME_read_ASN1", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.82, crypto/asn1/asn_mime.c
    {"SMIME_read_ASN1_ex", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.82, crypto/asn1/asn_mime.c
    {"SMIME_read_CMS_ex", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.88, crypto/cms/cms_io.c
    {"SMIME_read_PKCS7_ex", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, crypto/pkcs7/pk7_mime.c
    {"SRP_create_verifier", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/srp/srp_vfy.c
    {"SRP_create_verifier_BN", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, crypto/srp/srp_vfy.c
    {"SRP_create_verifier_BN_ex", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/srp/srp_vfy.c
    {"SRP_create_verifier_ex", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/srp/srp_vfy.c
    {"SSL_CTX_new_ex", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, ssl/ssl_lib.c
    {"SSL_SESSION_dup", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.97, ssl/ssl_sess.c
    {"SSL_dup", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.93, ssl/ssl_lib.c
    {"SSL_new_domain", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.93, ssl/ssl_lib.c
    {"SSL_new_from_listener", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.93, ssl/ssl_lib.c
    {"SSL_new_listener", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.93, ssl/ssl_lib.c
    {"SSL_new_listener_from", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.93, ssl/ssl_lib.c
    {"TS_RESP_create_response", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.88, include/openssl/ts.h
    {"UI_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/ui/ui_lib.c
    {"UI_new_method", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/ui/ui_lib.c
    {"X509_ATTRIBUTE_create_by_NID", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/x509/x509_att.c
    {"X509_ATTRIBUTE_create_by_OBJ", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/x509/x509_att.c
    {"X509_ATTRIBUTE_create_by_txt", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/x509/x509_att.c
    {"X509_CRL_METHOD_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/x509/x_crl.c
    {"X509_CRL_new_ex", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.92, crypto/x509/x_crl.c
    {"X509_EXTENSION_create_by_NID", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, crypto/x509/x509_v3.c
    {"X509_EXTENSION_create_by_OBJ", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.88, crypto/x509/x509_v3.c
    {"X509_NAME_ENTRY_create_by_NID", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, crypto/x509/x509name.c
    {"X509_NAME_ENTRY_create_by_OBJ", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.88, crypto/x509/x509name.c
    {"X509_NAME_ENTRY_create_by_txt", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, crypto/x509/x509name.c
    {"X509_PKEY_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.92, crypto/asn1/x_pkey.c
    {"X509_PUBKEY_dup", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/x509/x_pubkey.c
    {"X509_PUBKEY_new_ex", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/x509/x_pubkey.c
    {"X509_REQ_new_ex", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/x509/x_req.c
    {"X509_STORE_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/x509/x509_lu.c
    {"X509_STORE_new_1", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, test/cmp_ctx_test.c
    {"X509_from_strings", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, test/testutil/load.c
    {"X509_new_ex", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/x509/x_x509.c
    {"X509at_add1_attr_by_txt", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, crypto/x509/x509_att.c
    {"alloc_kdf_algorithm_name", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, apps/kdf.c
    {"alloc_mac_algorithm_name", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, apps/mac.c
    {"alloc_new_neighborhood_list", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.93, crypto/hashtable/hashtable.c
    {"alloc_port_user_ssl", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, ssl/quic/quic_impl.c
    {"app_new_stream_cb", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.7, demos/quic/poll-server/quic-server-ssl-poll-http.c
    {"asn1_item_embed_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.88, crypto/asn1/tasn_new.c
    {"asn1_template_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.87, crypto/asn1/tasn_new.c
    {"b64_read_asn1", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.82, crypto/asn1/asn_mime.c
    {"bio_to_mem", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.88, apps/lib/apps.c
    {"brotli_alloc", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/comp/c_brotli.c
    {"cert_response", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.8, crypto/cmp/cmp_client.c
    {"cfq_get_free", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.78, ssl/quic/quic_cfq.c
    {"cms_get_text_bio", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, crypto/cms/cms_smime.c
    {"core_bio_new_from_new_bio", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, crypto/bio/ossl_core_bio.c
    {"create_a_psk", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, test/helpers/ssltestlib.c
    {"create_cert_store", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.92, apps/ts.c
    {"create_client_ctx", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.92, test/quicapitest.c
    {"create_context", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, demos/sslecho/echecho.c
    {"create_ctx", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, demos/guide/quic-server-block.c
    {"create_key", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, demos/signature/EVP_ED_Signature_demo.c
    {"create_merged_key", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, demos/pkey/EVP_PKEY_DSA_paramvalidate.c
    {"create_ml_dsa_raw_key", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.92, fuzz/ml-dsa.c
    {"create_mlkem_raw_key", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.92, fuzz/ml-kem.c
    {"create_ocsp_resp", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, test/sslapitest.c
    {"create_peer", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.72, demos/keyexch/ecdh.c
    {"create_quic_ssl_objects", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.88, test/quicapitest.c
    {"create_quic_ssl_objects_seed_peer", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.88, test/quicapitest.c
    {"create_response", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, apps/ts.c
    {"create_server_ctx", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.92, test/quicapitest.c
    {"create_socket", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.88, test/quic-openssl-docker/hq-interop/quic-hq-interop-server.c
    {"create_socket_bio", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, demos/guide/quic-client-block.c
    {"create_ssl_ctx", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, doc/designs/ddd/ddd-01-conn-blocking.c
    {"create_ssl_ctx_pair", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, test/helpers/ssltestlib.c
    {"create_ssl_objects", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, test/helpers/ssltestlib.c
    {"create_ssl_objects2", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, test/helpers/ssltestlib.c
    {"create_verify_ctx", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.88, apps/ts.c
    {"ctlog_new_from_conf", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.88, crypto/ct/ct_log.c
    {"d2i_DHxparams", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, crypto/dh/dh_asn1.c
    {"d2i_DSA_PUBKEY", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, crypto/x509/x_pubkey.c
    {"d2i_ECDSA_SIG", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, crypto/ec/ec_asn1.c
    {"d2i_ECParameters", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, crypto/ec/ec_asn1.c
    {"d2i_ECPrivateKey", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, crypto/ec/ec_asn1.c
    {"d2i_EC_PUBKEY", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, crypto/x509/x_pubkey.c
    {"d2i_KeyParams", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, crypto/asn1/d2i_param.c
    {"d2i_KeyParams_bio", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.75, crypto/asn1/d2i_param.c
    {"d2i_PKCS8PrivateKey_bio", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, crypto/pem/pem_pk8.c
    {"d2i_PKCS8PrivateKey_fp", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, crypto/pem/pem_pk8.c
    {"d2i_PUBKEY_ex_bio", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.87, crypto/x509/x_all.c
    {"d2i_PUBKEY_ex_fp", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, crypto/x509/x_all.c
    {"d2i_PUBKEY_int", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, crypto/x509/x_pubkey.c
    {"d2i_PrivateKey_decoder", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, crypto/asn1/d2i_pr.c
    {"d2i_PrivateKey_ex_bio", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.87, crypto/x509/x_all.c
    {"d2i_PrivateKey_ex_fp", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, crypto/x509/x_all.c
    {"d2i_PublicKey", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.82, crypto/asn1/d2i_pu.c
    {"d2i_RSA_PUBKEY", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, crypto/x509/x_pubkey.c
    {"d2i_SSL_SESSION_ex", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.82, ssl/ssl_asn1.c
    {"depack_do_implicit_stream_create", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, ssl/quic/quic_rx_depack.c
    {"dh_create_pkey", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, test/acvp_test.c
    {"dh_dup", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.92, providers/implementations/keymgmt/dh_kmgmt.c
    {"dh_new_intern", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.97, crypto/dh/dh_lib.c
    {"do_derive", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, fuzz/ml-kem.c
    {"do_handshake_internal", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, test/helpers/handshake.c
    {"drbg_ctr_new_wrapper", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, providers/implementations/rands/drbg_ctr.c
    {"drbg_hash_new_wrapper", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, providers/implementations/rands/drbg_hash.c
    {"drbg_hmac_new_wrapper", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, providers/implementations/rands/drbg_hmac.c
    {"dsa_create_pkey", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, test/acvp_test.c
    {"dsa_dup", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.92, providers/implementations/keymgmt/dsa_kmgmt.c
    {"dsa_new_intern", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.97, crypto/dsa/dsa_lib.c
    {"dtls_new_record_layer", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, ssl/record/methods/dtls_meth.c
    {"dup_bio_err", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.92, apps/lib/apps.c
    {"dup_bio_in", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.92, apps/lib/apps.c
    {"dup_bio_out", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.92, apps/lib/apps.c
    {"ec_dup", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.92, providers/implementations/keymgmt/ec_kmgmt.c
    {"ecdsa_create_pkey", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, test/acvp_test.c
    {"ech_decode_one_entry", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, ssl/ech/ech_store.c
    {"eddsa_create_pkey", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, test/acvp_test.c
    {"evp_keymgmt_util_make_pkey", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.88, crypto/evp/keymgmt_lib.c
    {"evp_md_ctx_new_ex", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.92, crypto/evp/digest.c
    {"evp_pkey_copy_downgraded", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.88, crypto/evp/p_lib.c
    {"evp_pkey_new_raw_nist_public_key", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/hpke/hpke.c
    {"gen_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.97, crypto/cmp/cmp_msg.c
    {"get_file_data", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, demos/http3/ossl-nghttp3-demo-server.c
    {"get_str_from_file", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, apps/lib/apps.c
    {"glue2bio", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, test/testutil/load.c
    {"glue_strings", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, test/testutil/driver.c
    {"hf_new_ssl", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, test/radix/quic_ops.c
    {"hf_new_ssl_listener_from", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, test/radix/quic_ops.c
    {"hpke_decrypt_encch", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, ssl/ech/ech_internal.c
    {"http_new_bio", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.92, crypto/http/http_client.c
    {"http_req_ctx_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.93, crypto/http/http_client.c
    {"int_ctx_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/evp/pmeth_lib.c
    {"keygen_mlkem_real_key", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, fuzz/ml-kem.c
    {"ktls_new_record_layer", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, ssl/record/methods/ktls_meth.c
    {"load_certs", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, apps/lib/apps.c
    {"load_content_info", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.88, apps/cms.c
    {"load_crls", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, apps/lib/apps.c
    {"load_key_certs_crls", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.88, apps/lib/apps.c
    {"make_key", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, test/endecode_test.c
    {"make_key_fromdata", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, test/evp_extra_test.c
    {"make_template", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, test/endecode_test.c
    {"ml_dsa_create_keypair", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, test/ml_dsa_test.c
    {"ml_dsa_dup_key", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, providers/implementations/keymgmt/ml_dsa_kmgmt.c
    {"mlx_kem_dup", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.88, providers/implementations/keymgmt/mlx_kmgmt.c
    {"mlx_kem_key_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, providers/implementations/keymgmt/mlx_kmgmt.c
    {"mock_srv_ctx_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, apps/lib/cmp_mock_srv.c
    {"msg_encode", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, crypto/slh_dsa/slh_dsa.c
    {"my_malloc", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, test/mem_alloc_test.c
    {"my_realloc", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, test/mem_alloc_test.c
    {"net_read_alloc", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, doc/designs/ddd/ddd-06-mem-uv.c
    {"new_cmac_key_int", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/evp/p_lib.c
    {"new_conn", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, doc/designs/ddd/ddd-01-conn-blocking.c
    {"new_file_ctx", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, providers/implementations/storemgmt/file_store.c
    {"new_pkcs12_builder", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, test/helpers/pkcs12.h
    {"new_raw_key_int", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/evp/p_lib.c
    {"ossl_asn1_item_ex_new_intern", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.93, crypto/asn1/tasn_new.c
    {"ossl_b2i_DSA_after_header", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.88, crypto/pem/pvkfmt.c
    {"ossl_b2i_RSA_after_header", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.88, crypto/pem/pvkfmt.c
    {"ossl_bio_new_from_core_bio", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, providers/common/bio_prov.c
    {"ossl_cmp_certConf_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/cmp/cmp_msg.c
    {"ossl_cmp_certrep_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/cmp/cmp_msg.c
    {"ossl_cmp_certreq_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/cmp/cmp_msg.c
    {"ossl_cmp_error_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/cmp/cmp_msg.c
    {"ossl_cmp_genm_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.97, crypto/cmp/cmp_msg.c
    {"ossl_cmp_genp_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.97, crypto/cmp/cmp_msg.c
    {"ossl_cmp_mock_srv_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, apps/lib/cmp_mock_srv.c
    {"ossl_cmp_msg_create", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/cmp/cmp_msg.c
    {"ossl_cmp_pkiconf_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/cmp/cmp_msg.c
    {"ossl_cmp_pollRep_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/cmp/cmp_msg.c
    {"ossl_cmp_pollReq_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/cmp/cmp_msg.c
    {"ossl_cmp_rp_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/cmp/cmp_msg.c
    {"ossl_cmp_rr_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/cmp/cmp_msg.c
    {"ossl_cms_CompressedData_create", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/cms/cms_cd.c
    {"ossl_cms_Data_create", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/cms/cms_lib.c
    {"ossl_cms_DigestedData_create", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/cms/cms_dd.c
    {"ossl_core_bio_new_file", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.93, crypto/bio/ossl_core_bio.c
    {"ossl_core_bio_new_from_bio", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.92, crypto/bio/ossl_core_bio.c
    {"ossl_core_bio_new_mem_buf", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.93, crypto/bio/ossl_core_bio.c
    {"ossl_crypto_condvar_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/thread/arch/thread_win.c
    {"ossl_d2i_DH_PUBKEY", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, crypto/x509/x_pubkey.c
    {"ossl_d2i_DHx_PUBKEY", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, crypto/x509/x_pubkey.c
    {"ossl_d2i_DSA_PUBKEY", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, crypto/x509/x_pubkey.c
    {"ossl_d2i_ED25519_PUBKEY", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, crypto/x509/x_pubkey.c
    {"ossl_d2i_ED448_PUBKEY", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, crypto/x509/x_pubkey.c
    {"ossl_d2i_PrivateKey_legacy", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.8, crypto/asn1/d2i_pr.c
    {"ossl_d2i_X25519_PUBKEY", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, crypto/x509/x_pubkey.c
    {"ossl_d2i_X448_PUBKEY", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, crypto/x509/x_pubkey.c
    {"ossl_decoder_ctx_for_pkey_dup", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.92, crypto/encode_decode/decoder_pkey.c
    {"ossl_dh_dup", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/dh/dh_backend.c
    {"ossl_dh_new_ex", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.97, crypto/dh/dh_lib.c
    {"ossl_dsa_dup", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/dsa/dsa_backend.c
    {"ossl_dsa_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.97, crypto/dsa/dsa_lib.c
    {"ossl_ec_key_dup", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/ec/ec_backend.c
    {"ossl_ec_key_new_method_int", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.97, crypto/ec/ec_kmeth.c
    {"ossl_echstore_entry_dup", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, ssl/ech/ech_internal.c
    {"ossl_ht_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/hashtable/hashtable.c
    {"ossl_malloc_align", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/aligned_alloc.c
    {"ossl_ml_dsa_key_dup", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/ml_dsa/ml_dsa_key.c
    {"ossl_ml_dsa_key_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.97, crypto/ml_dsa/ml_dsa_key.c
    {"ossl_namemap_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/core_namemap.c
    {"ossl_param_dup", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, crypto/params_dup.c
    {"ossl_property_string_data_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.92, crypto/property/property_string.c
    {"ossl_prov_ml_dsa_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.92, providers/implementations/keymgmt/ml_dsa_kmgmt.c
    {"ossl_qlog_new_from_env", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, ssl/quic/qlog.c
    {"ossl_quic_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.92, ssl/quic/quic_impl.c
    {"ossl_quic_new_domain", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, ssl/quic/quic_impl.c
    {"ossl_quic_new_from_listener", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, ssl/quic/quic_impl.c
    {"ossl_quic_new_listener", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.92, ssl/quic/quic_impl.c
    {"ossl_quic_new_listener_from", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, ssl/quic/quic_impl.c
    {"ossl_quic_new_token_store", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, ssl/quic/quic_impl.c
    {"ossl_quic_tserver_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, ssl/quic/quic_tserver.c
    {"ossl_quic_txpim_pkt_alloc", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, ssl/quic/quic_txpim.c
    {"ossl_rand_ctx_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/rand/rand_lib.c
    {"ossl_rand_pool_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.97, crypto/rand/rand_pool.c
    {"ossl_rcu_lock_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/threads_win.c
    {"ossl_rsa_acvp_test_gen_params_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.88, crypto/rsa/rsa_acvp_test_params.c
    {"ossl_rsa_dup", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.92, crypto/rsa/rsa_backend.c
    {"ossl_rsa_new_with_ctx", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/rsa/rsa_lib.c
    {"ossl_rsa_pss_params_create", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, crypto/rsa/rsa_ameth.c
    {"ossl_slh_dsa_hash_ctx_dup", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/slh_dsa/slh_dsa_hash_ctx.c
    {"ossl_slh_dsa_hash_ctx_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/slh_dsa/slh_dsa_hash_ctx.c
    {"ossl_slh_dsa_key_dup", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/slh_dsa/slh_dsa_key.c
    {"ossl_slh_dsa_key_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/slh_dsa/slh_dsa_key.c
    {"ossl_ssl_connection_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, ssl/ssl_lib.c
    {"ossl_ssl_connection_new_int", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.92, ssl/ssl_lib.c
    {"ossl_stored_namemap_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/core_namemap.c
    {"ossl_threads_ctx_new", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/thread/internal.c
    {"ossl_x509_algor_md_to_mgf1", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.88, crypto/asn1/x_algor.c
    {"ossl_x509_algor_new_from_md", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, crypto/asn1/x_algor.c
    {"ossl_x509at_add1_attr_by_NID", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, crypto/x509/x509_att.c
    {"ossl_x509at_add1_attr_by_OBJ", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, crypto/x509/x509_att.c
    {"ossl_x509at_add1_attr_by_txt", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, crypto/x509/x509_att.c
    {"pem_malloc", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/pem/pem_lib.c
    {"pem_read_bio_key", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.82, crypto/pem/pem_pkey.c
    {"pem_read_bio_key_decoder", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, crypto/pem/pem_pkey.c
    {"pem_read_bio_key_legacy", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.82, crypto/pem/pem_pkey.c
    {"pkcs12_create_ex2_setup", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, test/pkcs12_api_test.c
    {"port_make_channel", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, ssl/quic/quic_port.c
    {"port_new_handshake_layer", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, ssl/quic/quic_port.c
    {"process_additional_mac_key_arguments", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.88, apps/lib/apps.c
    {"process_cert_request", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.8, apps/lib/cmp_mock_srv.c
    {"process_include", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.88, crypto/conf/conf_def.c
    {"ql_create", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.88, test/quicapitest.c
    {"qrx_alloc_rxe", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, ssl/quic/quic_record_rx.c
    {"qrx_ensure_free_rxe", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, ssl/quic/quic_record_rx.c
    {"qtest_create_quic_objects", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, test/helpers/quictestlib.c
    {"qtx_alloc_txe", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, ssl/quic/quic_record_tx.c
    {"qtx_ensure_free_txe", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.6, ssl/quic/quic_record_tx.c
    {"quic_new_record_layer", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, ssl/quic/quic_tls.c
    {"r_init", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.8, test/rand_test.c
    {"rsa_create_pkey", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, test/acvp_test.c
    {"rsa_dup", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, providers/implementations/keymgmt/rsa_kmgmt.c
    {"rsa_new_intern", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.93, crypto/rsa/rsa_lib.c
    {"sec_alloc_realloc", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, crypto/buffer/buffer.c
    {"setupearly_data_test", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.8, test/sslapitest.c
    {"slh_dsa_create_keypair", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, test/slh_dsa_test.c
    {"slh_dsa_d2i_PKCS8", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, providers/implementations/encode_decode/decode_der2key.c
    {"slh_dsa_d2i_PUBKEY", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, providers/implementations/encode_decode/decode_der2key.c
    {"slh_dsa_dup_key", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, providers/implementations/keymgmt/slh_dsa_kmgmt.c
    {"slh_dsa_new_key", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, providers/implementations/keymgmt/slh_dsa_kmgmt.c
    {"srp_create_user", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, apps/srp.c
    {"ssl_cert_dup", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.93, ssl/ssl_cert.c
    {"ssl_create_cipher_list", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.88, ssl/ssl_ciph.c
    {"ssl_session_dup", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, ssl/ssl_sess.c
    {"ssl_session_dup_intern", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, ssl/ssl_sess.c
    {"ssl_set_new_record_layer", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, ssl/record/rec_layer_s3.c
    {"str_copy", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.75, crypto/conf/conf_def.c
    {"tls_int_new_record_layer", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.92, ssl/record/methods/tls_common.c
    {"tls_new_record_layer", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, ssl/record/methods/tls_common.c
    {"txpim_get_free", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.8, ssl/quic/quic_txpim.c
    {"unescape", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, test/evp_test.c
    {"win32_utf8argv", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.85, apps/lib/win32_init.c
    {"x509_object_dup", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, crypto/x509/x509_lu.c
    {"x509_pubkey_ex_new_ex", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.9, crypto/x509/x_pubkey.c
    {"zstd_alloc", SaberCheckerAPI::CK_ALLOC},  // allocator, conf=0.95, crypto/comp/c_zstd.c
    /* NSPA_AUTO_OPENSSL_CK_ALLOC_END */







































    {"VOS_MemFree", SaberCheckerAPI::CK_FREE},
    {"cfree", SaberCheckerAPI::CK_FREE},
    {"free", SaberCheckerAPI::CK_FREE},
    {"free_all_mem", SaberCheckerAPI::CK_FREE},
    {"freeaddrinfo", SaberCheckerAPI::CK_FREE},
    {"gcry_mpi_release", SaberCheckerAPI::CK_FREE},
    {"gcry_sexp_release", SaberCheckerAPI::CK_FREE},
    {"globfree", SaberCheckerAPI::CK_FREE},
    {"nhfree", SaberCheckerAPI::CK_FREE},
    {"obstack_free", SaberCheckerAPI::CK_FREE},
    {"safe_cfree", SaberCheckerAPI::CK_FREE},
    {"safe_free", SaberCheckerAPI::CK_FREE},
    {"safefree", SaberCheckerAPI::CK_FREE},
    {"safexfree", SaberCheckerAPI::CK_FREE},
    {"sm_free", SaberCheckerAPI::CK_FREE},
    {"vim_free", SaberCheckerAPI::CK_FREE},
    {"xfree", SaberCheckerAPI::CK_FREE},
    {"SSL_CTX_free", SaberCheckerAPI::CK_FREE},
    {"SSL_free", SaberCheckerAPI::CK_FREE},
    {"XFree", SaberCheckerAPI::CK_FREE},
    /* NSPA_AUTO_OPENSSL_CK_FREE_BEGIN */
    /* NSPA: openssl project-specific CK_FREE entries */
    {"APP_HTTP_TLS_INFO_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.92, apps/lib/apps.c
    {"ASN1_item_ex_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.9, crypto/asn1/tasn_fre.c
    {"ASN1_item_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.9, crypto/asn1/tasn_fre.c
    {"ASYNC_WAIT_CTX_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.88, crypto/async/async_wait.c
    {"BIO_ACCEPT_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.88, crypto/bio/bss_acpt.c
    {"BIO_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.92, crypto/bio/bio_lib.c
    {"BIO_free_all", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.9, crypto/bio/bio_lib.c
    {"BUF_MEM_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.97, crypto/buffer/buffer.c
    {"CRYPTO_clear_free", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.97, crypto/mem.c
    {"CRYPTO_free", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.99, crypto/mem.c
    {"CRYPTO_free_ex_data", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.95, crypto/ex_data.c
    {"CRYPTO_secure_clear_free", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.97, crypto/mem_sec.c
    {"CRYPTO_secure_free", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.97, crypto/mem_sec.c
    {"CTLOG_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.92, crypto/ct/ct_log.c
    {"DH_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.97, crypto/dh/dh_lib.c
    {"DSA_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.97, crypto/dsa/dsa_lib.c
    {"ECDSA_SIG_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.9, include/openssl/ec.h
    {"EC_KEY_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.97, crypto/ec/ec_key.c
    {"EVP_PKEY_CTX_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.95, crypto/evp/pmeth_lib.c
    {"EVP_PKEY_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.97, crypto/evp/p_lib.c
    {"EVP_SKEY_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.95, crypto/evp/s_lib.c
    {"HANDSHAKE_RESULT_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.93, test/helpers/handshake.c
    {"LP_find_file_end", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.92, crypto/LPdir_unix.c
    {"MYOBJ_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.95, test/exdatatest.c
    {"OPENSSL_INIT_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.92, crypto/conf/conf_lib.c
    {"OPENSSL_sk_delete", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.85, include/openssl/stack.h
    {"OPENSSL_sk_delete_ptr", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.85, include/openssl/stack.h
    {"OSSL_CMP_CTX_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.99, crypto/cmp/cmp_ctx.c
    {"OSSL_CMP_MSG_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.97, crypto/cmp/cmp_msg.c
    {"OSSL_CMP_SRV_CTX_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.93, crypto/cmp/cmp_server.c
    {"OSSL_DECODER_CTX_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.93, crypto/encode_decode/decoder_meth.c
    {"OSSL_DEMO_H3_CONN_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.95, demos/http3/ossl-nghttp3.c
    {"OSSL_ERR_STATE_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.92, crypto/err/err.c
    {"OSSL_HPKE_CTX_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.95, crypto/hpke/hpke.c
    {"OSSL_HTTP_REQ_CTX_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.93, crypto/http/http_client.c
    {"OSSL_STORE_INFO_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.95, crypto/store/store_lib.c
    {"PKCS7_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.95, crypto/pkcs7/pk7_asn1.c
    {"RADIX_OBJ_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.92, test/radix/quic_bindings.c
    {"RADIX_THREAD_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.88, test/radix/quic_bindings.c
    {"RECORD_LAYER_clear", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.82, ssl/record/rec_layer_s3.c
    {"RSA_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.93, crypto/rsa/rsa_lib.c
    {"SCT_CTX_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.85, crypto/ct/ct_local.h
    {"SSL_SESSION_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.93, ssl/ssl_sess.c
    {"TS_REQ_delete_ext", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.85, include/openssl/ts.h
    {"TS_RESP_CTX_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.93, crypto/ts/ts_rsp_sign.c
    {"TS_TST_INFO_delete_ext", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.85, include/openssl/ts.h
    {"UI_destroy_method", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.93, crypto/ui/ui_lib.c
    {"UI_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.92, crypto/ui/ui_lib.c
    {"X509V3_conf_free", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.95, crypto/x509/v3_utl.c
    {"X509_ACERT_delete_attr", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.85, crypto/x509/x509_acert.c
    {"X509_CRL_delete_ext", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.85, crypto/x509/x509_ext.c
    {"X509_INFO_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.88, crypto/asn1/x_info.c
    {"X509_NAME_delete_entry", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.87, crypto/x509/x509name.c
    {"X509_PKEY_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.88, crypto/asn1/x_pkey.c
    {"X509_REQ_delete_attr", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.82, crypto/x509/x509_req.c
    {"X509_REVOKED_delete_ext", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.85, crypto/x509/x509_ext.c
    {"X509_STORE_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.95, crypto/x509/x509_lu.c
    {"X509_delete_ext", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.85, crypto/x509/x509_ext.c
    {"X509at_delete_attr", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.85, crypto/x509/x509_att.c
    {"X509v3_delete_ext", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.85, crypto/x509/x509_v3.c
    {"X509v3_delete_extension", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.88, crypto/x509/x509_v3.c
    {"acpt_free", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.83, crypto/bio/bss_acpt.c
    {"bio_f_noisy_dgram_filter_free", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.87, test/helpers/quictestlib.h
    {"bio_f_pkt_split_dgram_filter_free", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.87, test/helpers/quictestlib.h
    {"bio_free_ex_data", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.88, crypto/bio/bio_lib.c
    {"bn_free", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.9, crypto/asn1/x_bignum.c
    {"cfq_free_cb_", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.78, test/quic_fifd_test.c
    {"ch_cleanup", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.93, ssl/quic/quic_channel.c
    {"cleanup_ml_dsa_keys", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.9, fuzz/ml-dsa.c
    {"cleanup_mlkem_keys", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.9, fuzz/ml-kem.c
    {"cleanup_one", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.82, test/radix/quic_bindings.c
    {"close_all_ids", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.8, demos/http3/ossl-nghttp3-demo-server.c
    {"conn_free", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.83, crypto/bio/bss_conn.c
    {"ctx_data_free_data", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.95, test/helpers/handshake.c
    {"custom_ext_free_old_cb_wrap", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.88, ssl/statem/extensions_cust.c
    {"decoder_cache_entry_free", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.9, crypto/encode_decode/decoder_pkey.c
    {"delete_ext", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.88, crypto/x509/v3_conf.c
    {"destroy_pe", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.88, demos/quic/poll-server/quic-server-ssl-poll-http.c
    {"destroy_peer", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.88, demos/keyexch/ecdh.c
    {"destroy_poll_manager", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.88, demos/quic/poll-server/quic-server-ssl-poll-http.c
    {"destroy_ui_method", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.88, apps/lib/apps_ui.c
    {"do_free_upto", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.92, crypto/cms/cms_smime.c
    {"drbg_ctr_free", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.95, test/p_ossltest.c
    {"dtls1_clear_sent_buffer", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.88, ssl/d1_lib.c
    {"dtls1_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.85, ssl/d1_lib.c
    {"dtls_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.9, ssl/record/methods/dtls_meth.c
    {"ech_free_stashed_key_shares", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.85, ssl/ech/ech_internal.c
    {"err_delete_thread_state", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.88, crypto/err/err.c
    {"evp_keymgmt_freedata", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.88, crypto/evp/keymgmt_meth.c
    {"evp_pkey_free_it", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.9, crypto/evp/p_lib.c
    {"evp_skeymgmt_freedata", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.88, crypto/evp/skeymgmt_meth.c
    {"free_asn1_data", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.95, providers/implementations/encode_decode/encode_key2any.c
    {"free_buf_mem", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.92, ssl/quic/quic_channel.c
    {"free_cb", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.78, test/quic_cfq_test.c
    {"free_dir", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.88, crypto/x509/by_dir.c
    {"free_file_ctx", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.82, providers/implementations/storemgmt/file_store.c
    {"free_frame_data", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.92, ssl/quic/quic_channel.c
    {"free_key_list", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.8, test/evp_test.c
    {"free_old_rcu_data", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.9, test/threadstest.c
    {"free_path_response", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.92, ssl/quic/quic_rx_depack.c
    {"free_pstring", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.95, crypto/http/http_lib.c
    {"free_transport_params_cb", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.72, ssl/quic/quic_tls.c
    {"h3_stream_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.9, demos/http3/ossl-nghttp3.c
    {"helper_cleanup", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.93, test/quic_txp_test.c
    {"helper_destroy", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.95, test/quic_ackm_test.c
    {"int_dh_free", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.92, crypto/dh/dh_ameth.c
    {"int_dsa_free", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.92, crypto/dsa/dsa_ameth.c
    {"int_ec_free", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.92, crypto/ec/ec_ameth.c
    {"int_rsa_free", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.85, crypto/rsa/rsa_ameth.c
    {"lms_free_key", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.92, providers/implementations/keymgmt/lms_kmgmt.c
    {"long_free", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.8, crypto/asn1/x_long.c
    {"mem_buf_free", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.85, crypto/bio/bss_mem.c
    {"mem_free", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.85, crypto/bio/bss_mem.c
    {"ml_dsa_free_key", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.82, providers/implementations/keymgmt/ml_dsa_kmgmt.c
    {"mlx_kem_key_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.85, providers/implementations/keymgmt/mlx_kmgmt.c
    {"mock_srv_ctx_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.93, apps/lib/cmp_mock_srv.c
    {"module_lists_free", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.82, crypto/conf/conf_mod.c
    {"my_free", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.95, test/mem_alloc_test.c
    {"name_funcs_free", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.9, crypto/objects/o_names.c
    {"names_lh_free_doall", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.85, crypto/objects/o_names.c
    {"ndef_prefix_free", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.9, crypto/asn1/bio_ndef.c
    {"ndef_suffix_free", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.9, crypto/asn1/bio_ndef.c
    {"net_connect_fail_close_done", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.75, doc/designs/ddd/ddd-06-mem-uv.c
    {"net_read_done", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.82, doc/designs/ddd/ddd-06-mem-uv.c
    {"net_write_done", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.8, doc/designs/ddd/ddd-06-mem-uv.c
    {"new_free_cb", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.9, test/ech_test.c
    {"old_free_cb", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.9, test/sslapitest.c
    {"op_cache_free", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.9, crypto/evp/keymgmt_lib.c
    {"ossl_X509_PUBKEY_INTERNAL_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.88, crypto/x509/x_pubkey.c
    {"ossl_asn1_enc_free", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.78, crypto/asn1/asn1_local.h
    {"ossl_asn1_item_embed_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.7, crypto/asn1/asn1_local.h
    {"ossl_asn1_primitive_free", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.75, crypto/asn1/asn1_local.h
    {"ossl_asn1_template_free", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.72, crypto/asn1/asn1_local.h
    {"ossl_cmp_mock_srv_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.95, apps/lib/cmp_mock_srv.c
    {"ossl_config_modules_free", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.82, crypto/conf/conf_mod.c
    {"ossl_core_bio_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.9, crypto/bio/ossl_core_bio.c
    {"ossl_crypto_condvar_free", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.75, crypto/thread/arch/thread_none.c
    {"ossl_crypto_mutex_free", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.75, crypto/thread/arch/thread_none.c
    {"ossl_echstore_entry_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.85, ssl/ech/ech_store.c
    {"ossl_ht_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.92, crypto/hashtable/hashtable.c
    {"ossl_lms_key_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.95, crypto/lms/lms_key.c
    {"ossl_lms_sig_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.95, crypto/lms/lms_sig.c
    {"ossl_ml_dsa_key_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.95, crypto/ml_dsa/ml_dsa_key.c
    {"ossl_namemap_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.9, crypto/core_namemap.c
    {"ossl_property_string_data_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.87, crypto/property/property_string.c
    {"ossl_prov_free_key", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.95, providers/implementations/encode_decode/endecoder_common.c
    {"ossl_qlog_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.9, ssl/quic/qlog.c
    {"ossl_qtx_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.9, ssl/quic/quic_record_tx.c
    {"ossl_quic_cfq_release", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.92, ssl/quic/quic_cfq.c
    {"ossl_quic_channel_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.9, ssl/quic/quic_channel.c
    {"ossl_quic_demux_release_urxe", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.92, ssl/quic/quic_demux.c
    {"ossl_quic_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.93, ssl/quic/quic_impl.c
    {"ossl_quic_free_token_store", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.85, ssl/quic/quic_impl.c
    {"ossl_quic_tls_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.9, ssl/quic/quic_tls.c
    {"ossl_quic_tserver_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.92, ssl/quic/quic_tserver.c
    {"ossl_rand_pool_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.92, crypto/rand/rand_pool.c
    {"ossl_rcu_lock_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.92, crypto/threads_win.c
    {"ossl_rsa_acvp_test_gen_params_free", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.88, crypto/rsa/rsa_acvp_test_params.c
    {"ossl_slh_dsa_hash_ctx_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.95, crypto/slh_dsa/slh_dsa_hash_ctx.c
    {"ossl_slh_dsa_key_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.95, crypto/slh_dsa/slh_dsa_key.c
    {"ossl_ssl_connection_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.92, ssl/ssl_lib.c
    {"ossl_stored_namemap_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.88, crypto/core_namemap.c
    {"ossl_threads_ctx_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.9, crypto/thread/internal.c
    {"p_teardown", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.9, test/p_test.c
    {"peer_free_data", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.9, test/helpers/handshake.c
    {"pem_free", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.95, crypto/pem/pem_lib.c
    {"post_read", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.78, doc/designs/ddd/ddd-06-mem-uv.c
    {"property_table_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.85, crypto/property/property_string.c
    {"qtest_fault_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.9, test/helpers/quictestlib.h
    {"qtx_pending_to_free", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.85, ssl/quic/quic_record_tx.c
    {"quic_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.88, ssl/quic/quic_tls.c
    {"quic_free_domain", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.88, ssl/quic/quic_impl.c
    {"quic_free_listener", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.88, ssl/quic/quic_impl.c
    {"r_teardown", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.85, test/rand_test.c
    {"reuse_h3ssl", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.75, demos/http3/ossl-nghttp3-demo-server.c
    {"sa_free_node", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.9, crypto/sparse_array.c
    {"save_free_certs", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.85, apps/cmp.c
    {"sh_free", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.95, crypto/mem_sec.c
    {"slh_dsa_clean_keys", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.9, fuzz/slh-dsa.c
    {"slh_dsa_free_key", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.82, providers/implementations/keymgmt/slh_dsa_kmgmt.c
    {"ssl3_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.92, ssl/s3_lib.c
    {"ssl3_free_digest_list", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.9, ssl/s3_enc.c
    {"ssl_cert_clear_certs", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.88, ssl/ssl_cert.c
    {"ssl_cert_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.92, ssl/ssl_cert.c
    {"ssl_excert_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.9, apps/lib/s_cb.c
    {"ssl_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.82, ssl/bio_ssl.c
    {"ssl_free_wbio_buffer", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.82, ssl/ssl_lib.c
    {"ssl_release_record", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.9, ssl/record/rec_layer_s3.c
    {"teardown", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.9, doc/designs/ddd/ddd-01-conn-blocking.c
    {"teardown_continued", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.8, doc/designs/ddd/ddd-06-mem-uv.c
    {"teardown_ctx", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.9, doc/designs/ddd/ddd-01-conn-blocking.c
    {"thread_release_shared_pkey", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.8, test/threadstest.c
    {"tls1_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.9, ssl/t1_lib.c
    {"tls_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.88, ssl/record/methods/tls_common.c
    {"tls_int_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.9, ssl/record/methods/tls_common.c
    {"tlsa_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.85, ssl/ssl_lib.c
    {"tracedata_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.9, apps/openssl.c
    {"tst_bio_core_free", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.9, test/bio_core_test.c
    {"ui_free_method_data", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.88, crypto/ui/ui_util.c
    {"uint32_free", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.88, crypto/asn1/x_int64.c
    {"uint64_free", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.88, crypto/asn1/x_int64.c
    {"x509_name_ex_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.8, crypto/x509/x_name.c
    {"x509_pubkey_ex_free", SaberCheckerAPI::CK_FREE},  // destroyer, conf=0.92, crypto/x509/x_pubkey.c
    {"xor_prov_free_key", SaberCheckerAPI::CK_FREE},  // releaser, conf=0.88, test/tls-provider.c
    /* NSPA_AUTO_OPENSSL_CK_FREE_END */







































    {"fopen", SaberCheckerAPI::CK_FOPEN},
    {"\01_fopen", SaberCheckerAPI::CK_FOPEN},
    {"\01fopen64", SaberCheckerAPI::CK_FOPEN},
    {"\01readdir64", SaberCheckerAPI::CK_FOPEN},
    {"\01tmpfile64", SaberCheckerAPI::CK_FOPEN},
    {"fopen64", SaberCheckerAPI::CK_FOPEN},
    {"XOpenDisplay", SaberCheckerAPI::CK_FOPEN},
    {"XtOpenDisplay", SaberCheckerAPI::CK_FOPEN},
    {"fopencookie", SaberCheckerAPI::CK_FOPEN},
    {"popen", SaberCheckerAPI::CK_FOPEN},
    {"readdir", SaberCheckerAPI::CK_FOPEN},
    {"readdir64", SaberCheckerAPI::CK_FOPEN},
    {"gzdopen", SaberCheckerAPI::CK_FOPEN},
    {"iconv_open", SaberCheckerAPI::CK_FOPEN},
    {"tmpfile", SaberCheckerAPI::CK_FOPEN},
    {"tmpfile64", SaberCheckerAPI::CK_FOPEN},
    {"BIO_new_socket", SaberCheckerAPI::CK_FOPEN},
    {"gcry_md_open", SaberCheckerAPI::CK_FOPEN},
    {"gcry_cipher_open", SaberCheckerAPI::CK_FOPEN},


    {"fclose", SaberCheckerAPI::CK_FCLOSE},
    {"XCloseDisplay", SaberCheckerAPI::CK_FCLOSE},
    {"XtCloseDisplay", SaberCheckerAPI::CK_FCLOSE},
    {"__res_nclose", SaberCheckerAPI::CK_FCLOSE},
    {"pclose", SaberCheckerAPI::CK_FCLOSE},
    {"closedir", SaberCheckerAPI::CK_FCLOSE},
    {"dlclose", SaberCheckerAPI::CK_FCLOSE},
    {"gzclose", SaberCheckerAPI::CK_FCLOSE},
    {"iconv_close", SaberCheckerAPI::CK_FCLOSE},
    {"gcry_md_close", SaberCheckerAPI::CK_FCLOSE},
    {"gcry_cipher_close", SaberCheckerAPI::CK_FCLOSE},

    //This must be the last entry.
    {0, SaberCheckerAPI::CK_DUMMY}

};


/*!
 * initialize the map
 */
void SaberCheckerAPI::init()
{
    set<CHECKER_TYPE> t_seen;
    CHECKER_TYPE prev_t= CK_DUMMY;
    t_seen.insert(CK_DUMMY);
    for(const ei_pair *p= ei_pairs; p->n; ++p)
    {
        if(p->t != prev_t)
        {
            //This will detect if you move an entry to another block
            //  but forget to change the type.
            if(t_seen.count(p->t))
            {
                fputs(p->n, stderr);
                putc('\n', stderr);
                assert(!"ei_pairs not grouped by type");
            }
            t_seen.insert(p->t);
            prev_t= p->t;
        }
        if(tdAPIMap.count(p->n))
        {
            fputs(p->n, stderr);
            putc('\n', stderr);
            assert(!"duplicate name in ei_pairs");
        }
        tdAPIMap[p->n]= p->t;
    }
}

void SaberCheckerAPI::addCustomAPI(const string& funcName, CHECKER_TYPE type)
{
    if(funcName.empty() || type == CK_DUMMY)
        return;
    tdAPIMap[funcName] = type;
}

bool SaberCheckerAPI::loadCustomAPIsFromFile(const string& filename)
{
    ifstream input(filename.c_str());
    if(!input)
    {
        fprintf(stderr, "Cannot open custom API config file: %s\n", filename.c_str());
        return false;
    }

    string line;
    unsigned lineNo = 0;
    while(getline(input, line))
    {
        ++lineNo;

        string::size_type hash = line.find('#');
        if(hash != string::npos)
            line.erase(hash);
        string::size_type slash = line.find("//");
        if(slash != string::npos)
            line.erase(slash);

        for(char& ch : line)
        {
            if(ch == ':' || ch == '=' || ch == ',' || ch == ';' ||
                    ch == '[' || ch == ']' || ch == '{' || ch == '}' ||
                    ch == '(' || ch == ')' || ch == '"' || ch == '\'' ||
                    isspace(static_cast<unsigned char>(ch)))
            {
                ch = ' ';
            }
        }

        vector<string> tokens;
        string token;
        stringstream stream(line);
        while(stream >> token)
        {
            token = trimAPIConfigToken(token);
            if(!token.empty())
                tokens.push_back(token);
        }
        if(tokens.empty())
            continue;

        CHECKER_TYPE type = CK_DUMMY;
        size_t typeIndex = tokens.size();
        for(size_t i = 0; i < tokens.size(); ++i)
        {
            if(parseCheckerTypeToken(tokens[i], type))
            {
                typeIndex = i;
                break;
            }
        }

        if(type == CK_DUMMY)
        {
            fprintf(stderr, "Ignore custom API config line %u without checker type.\n", lineNo);
            continue;
        }

        for(size_t i = 0; i < tokens.size(); ++i)
        {
            if(i == typeIndex)
                continue;
            const string normalized = normalizeAPIConfigToken(tokens[i]);
            if(normalized == "name" || normalized == "names" ||
                    normalized == "function" || normalized == "functions" ||
                    normalized == "category" || normalized == "checker" ||
                    normalized == "checker_type" || normalized == "type")
            {
                continue;
            }
            addCustomAPI(tokens[i], type);
        }
    }

    return true;
}


