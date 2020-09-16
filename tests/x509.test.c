#include "unity/unity.h"

#include "../x509.h"

static int mock_assign_rsa(EVP_PKEY *pkey, RSA *rsa)
{
    return 0;
}

static int mock_x509_sign(X509 *x509, EVP_PKEY *pkey, const EVP_MD *md)
{
    return 0;
}

void setUp(void)
{
}

void tearDown(void)
{
}

void test_make_certificate(void)
{
    X509 *x509 = NULL;
    EVP_PKEY *pkey = NULL;
    TEST_ASSERT_TRUE(make_certificate_easy(&x509, &pkey, "localhost") == 0);
    EVP_PKEY_free(pkey);
    X509_free(x509);
}

void test_make_certificate_failures(void)
{
    X509 *x509 = NULL;
    EVP_PKEY *pkey = NULL;
    TEST_ASSERT_TRUE(make_certificate_ptr(&x509, &pkey, 4096, 0, 365,
                                          "localhost", &mock_assign_rsa,
                                          &mock_x509_sign) == -1);
    TEST_ASSERT_TRUE(make_certificate_ptr(&x509, &pkey, 4096, 0, 365,
                                          "localhost",
                                          &EVP_PKEY_assign_RSA_function,
                                          &mock_x509_sign) == -1);
}

int main(int argc, char *argv[])
{
    UNITY_BEGIN();
    RUN_TEST(test_make_certificate);
    RUN_TEST(test_make_certificate_failures);
    return 0;
}
