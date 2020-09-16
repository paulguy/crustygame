#ifndef X509_H
#define X509_H

#include <openssl/bio.h>
#include <openssl/conf.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

int EVP_PKEY_assign_RSA_function(EVP_PKEY *pkey, RSA *rsa);

int make_certificate_ptr(X509 **x509_out, EVP_PKEY **pkey_out, int bits,
                         int serial, int days, const char *hostname,
                         int (*pkey_assign_rsa_f)(EVP_PKEY *, RSA *),
                         int (*x509_sign_f)(X509 *, EVP_PKEY *,
                                            const EVP_MD *));
int make_certificate(X509 **x509_out, EVP_PKEY **pkey_out, int bits,
                     int serial, int days, const char *hostname);
int make_certificate_easy(X509 **x509_out, EVP_PKEY **pkey_out,
                          const char *hostname);

void write_certificate(const X509 *x509, const char *dest);
void write_certificate_key(const EVP_PKEY *pkey, const char *dest);

X509 *read_certificate(const char *path);
EVP_PKEY *read_certificate_key(const char *path);

#endif /* X509_H */
