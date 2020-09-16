#include "x509.h"

int EVP_PKEY_assign_RSA_function(EVP_PKEY *pkey, RSA *rsa)
{
    return EVP_PKEY_assign_RSA(pkey, rsa);
}

int make_certificate_ptr(X509 **x509_out, EVP_PKEY **pkey_out, int bits,
                         int serial, int days, const char *hostname,
                         int (*pkey_assign_rsa_f)(EVP_PKEY *, RSA *),
                         int (*x509_sign_f)(X509 *, EVP_PKEY *,
                                            const EVP_MD *))
{
    CRYPTO_mem_ctrl(CRYPTO_MEM_CHECK_ON);

    EVP_PKEY *pkey = EVP_PKEY_new();

    BIGNUM *bn = BN_new();
    BN_set_word(bn, RSA_F4);

    RSA *rsa = RSA_new();
    RSA_generate_key_ex(rsa, bits, bn, NULL);

    BN_free(bn);
    bn = NULL;

    if (!pkey_assign_rsa_f(pkey, rsa)) {
        RSA_free(rsa);
        EVP_PKEY_free(pkey);
        fprintf(stderr, "Unable to assign rsa key\n");
        return -1;
    }

    X509 *x509 = X509_new();
    X509_set_version(x509, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(x509), serial);
    X509_gmtime_adj(X509_get_notBefore(x509), 0);
    X509_gmtime_adj(X509_get_notAfter(x509), (long)60 * 60 * 24 * days);
    X509_set_pubkey(x509, pkey);

    X509_NAME *name = X509_get_subject_name(x509);
    X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC,
                               (const unsigned char *)"US", -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                               (const unsigned char *)"Crusty Game", -1, -1,
                               0);

    X509_set_issuer_name(x509, name);

    if (!x509_sign_f(x509, pkey, EVP_sha256())) {
        fprintf(stderr, "Unable to sign certificate with sha256\n");
        EVP_PKEY_free(pkey);
        X509_free(x509);
        return -1;
    }

    // Delegate our pointers over to outgoing arguments.
    *x509_out = x509;
    *pkey_out = pkey;

    return 0;
}

int make_certificate(X509 **x509, EVP_PKEY **pkey, int bits, int serial,
                     int days, const char *hostname)
{
    return make_certificate_ptr(x509, pkey, bits, serial, days, hostname,
                                &EVP_PKEY_assign_RSA_function, &X509_sign);
}

int make_certificate_easy(X509 **x509, EVP_PKEY **pkey, const char *hostname)
{
    const int bits = 4096;
    const int serial = 0;
    const int days = 365;
    return make_certificate(x509, pkey, bits, serial, days, hostname);
}

void write_certificate(const X509 *x509, const char *dest)
{
    FILE *f = fopen(dest, "w");
    PEM_write_X509(f, (X509 *)x509);
    fclose(f);
}

void write_certificate_key(const EVP_PKEY *pkey, const char *dest)
{
    FILE *f = fopen(dest, "w");
    PEM_write_PrivateKey(f, (EVP_PKEY *)pkey, NULL, NULL, 0, NULL, NULL);
    fclose(f);
}
