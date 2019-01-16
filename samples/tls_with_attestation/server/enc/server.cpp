// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <openenclave/enclave.h>
#include <mbedtls/pk.h>
#include <mbedtls/rsa.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/certs.h>
#include <mbedtls/x509.h>
#include <mbedtls/ssl.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/error.h>
#include <mbedtls/debug.h>
#include <stdlib.h>
#include <string.h>
#include <openenclave/bits/socket.h>
#include <mbedtls/platform.h>
#include <mbedtls/ssl_cache.h>  // Enable simple SSL cache implementation (MBEDTLS_SSL_CACHE_C)
#include "../../common/utility.h"

oe_result_t enclave_identity_verifier_callback(oe_identity_t *identity, void* arg);

extern "C" {
	int setup_tls_server(char* server_port);
};


// mbedtls debug levels 
// 0 No debug, 1 Error, 2 State change, 3 Informational, 4 Verbose
#define DEBUG_LEVEL 1
#define SERVER_IP "127.0.0.1"

#define HTTP_RESPONSE \
    "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n\r\n" \
    "<h2>mbed TLS Test Server</h2>\r\n" \
    "<p>Successful connection using: %s</p>\r\n"\
	"A message from TLS server inside enclave\r\n"

static void my_debug(void *ctx, int level,
	const char *file, int line,
	const char *str)
{
	((void)level);

	mbedtls_fprintf((FILE *)ctx, "%s:%04d: %s", file, line, str);
	fflush((FILE *)ctx);
}

// If set, the verify callback is called for each certificate in the chain. 
// The verification callback is supposed to return 0 on success. Otherwise, the
// verification failed. 
static int cert_verify_callback(void *data, mbedtls_x509_crt *crt, int depth, uint32_t *flags) 
{
	oe_result_t result = OE_FAILURE;
	int ret = 1;
	unsigned char* cert_buf = NULL;
	size_t cert_size = 0;

	(void)data;
	(void)flags;

	mbedtls_printf(" cert_verify_callback with depth = %d\n", depth);

	cert_buf = crt->raw.p;
	cert_size = crt->raw.len;

	mbedtls_printf("crt->version = %d\n", crt->version);
	mbedtls_printf("cert_size = %zu\n", cert_size);

    if (cert_size <= 0)
		goto exit;

    result = oe_verify_tls_cert(cert_buf, cert_size, enclave_identity_verifier_callback, NULL);
	if (result != OE_OK)
	{
		mbedtls_printf("oe_verify_tls_cert failed with result = %s\n", oe_result_str(result));	
		goto exit;
	}
	ret = 0;

exit:
	return ret;
}

int configure_server_ssl(mbedtls_ssl_context *ssl,
						mbedtls_ssl_config *conf,
						mbedtls_ssl_cache_context *cache,
						mbedtls_ctr_drbg_context *ctr_drbg,
						mbedtls_x509_crt *server_cert,
						mbedtls_pk_context *pkey)
{
	int ret = 1;
	oe_result_t result = OE_FAILURE;

	mbedtls_printf("Generating the certificate and private key\n");

	result = generate_certificate_and_pkey(server_cert, pkey);
    if (result != OE_OK)
    {
        printf("failed with %s\n", oe_result_str(result));
		ret = 1;
        goto exit;
    }

	mbedtls_printf("Setting up the SSL configuration....\n");

	if ((ret = mbedtls_ssl_config_defaults(conf,
									MBEDTLS_SSL_IS_SERVER,
									MBEDTLS_SSL_TRANSPORT_STREAM,
									MBEDTLS_SSL_PRESET_DEFAULT)) != 0)
	{
		mbedtls_printf("failed\n  ! mbedtls_ssl_config_defaults returned failed %d\n", ret);
		goto exit;
	}

	mbedtls_ssl_conf_rng(conf, mbedtls_ctr_drbg_random, ctr_drbg);
	mbedtls_ssl_conf_dbg(conf, my_debug, stdout);
	mbedtls_ssl_conf_session_cache(conf, cache, mbedtls_ssl_cache_get, mbedtls_ssl_cache_set);

	//mbedtls_ssl_conf_authmode(conf, MBEDTLS_SSL_VERIFY_OPTIONAL);
	mbedtls_ssl_conf_authmode(conf, MBEDTLS_SSL_VERIFY_NONE);

	mbedtls_ssl_conf_verify(conf, cert_verify_callback, NULL);

	// enable TLS server to send a list of acceptable CAs in CertificateRequest messages.
    // mbedtls_ssl_conf_cert_req_ca_list( &conf, MBEDTLS_SSL_CERT_REQ_CA_LIST_ENABLED);
	mbedtls_ssl_conf_ca_chain(conf, server_cert->next, NULL);

	if ((ret = mbedtls_ssl_conf_own_cert(conf, server_cert, pkey)) != 0)
	{
		mbedtls_printf("failed\n  ! mbedtls_ssl_conf_own_cert returned %d\n", ret);
		goto exit;
	}

	if ((ret = mbedtls_ssl_setup(ssl, conf)) != 0)
	{
		mbedtls_printf("failed\n  ! mbedtls_ssl_setup returned %d\n\n", ret);
		goto exit;
	}

	ret = 0;

exit:
	fflush(stdout);
	return ret;
}

int setup_tls_server(char* server_port)
{
	int ret = 0;
	int len = 0;
	oe_result_t result = OE_FAILURE;
	mbedtls_entropy_context entropy;
	mbedtls_ctr_drbg_context ctr_drbg;
	mbedtls_ssl_context ssl;
	mbedtls_ssl_config conf;
	mbedtls_x509_crt server_cert;
	mbedtls_pk_context pkey;
	mbedtls_ssl_cache_context cache;
	mbedtls_net_context listen_fd, client_fd;
	unsigned char buf[1024];
	const char *pers = "tls_server";

	// init mbedtls objects
	mbedtls_net_init(&listen_fd);
	mbedtls_net_init(&client_fd);
	mbedtls_ssl_init(&ssl);
	mbedtls_ssl_config_init(&conf);
	mbedtls_ssl_cache_init(&cache);
	mbedtls_x509_crt_init(&server_cert);
	mbedtls_pk_init(&pkey);
	mbedtls_entropy_init(&entropy);
	mbedtls_ctr_drbg_init(&ctr_drbg);

	mbedtls_debug_set_threshold(DEBUG_LEVEL);

	mbedtls_printf("Setup the listening TCP socket on SERVER_IP= [%s] server_port = [%s]\n", SERVER_IP, server_port);
	if ((ret = mbedtls_net_bind(&listen_fd, SERVER_IP, server_port , MBEDTLS_NET_PROTO_TCP)) != 0)
	{
		mbedtls_printf(" failed\n  ! mbedtls_net_bind returned %d\n", ret);
		goto exit;
	}

	mbedtls_printf("mbedtls_net_bind returned successfully. (listen_fd = %d)\n", listen_fd.fd);

	mbedtls_printf("Seeding the random number generator (RNG)\n");
	if ((ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
		(const unsigned char *)pers,
		strlen(pers))) != 0)
	{
		mbedtls_printf(" failed\n  ! mbedtls_ctr_drbg_seed returned %d\n", ret);
		goto exit;
	}

	// Configure server SSL settings
	ret = configure_server_ssl(&ssl, &conf, &cache, &ctr_drbg, &server_cert, &pkey);
	if (ret != 0)
	{
		mbedtls_printf(" failed\n  ! mbedtls_net_connect returned %d\n", ret);
		goto exit;
	}

waiting_for_connection_request:
	fflush(stdout);
	if (ret != 0)
	{
		char error_buf[100];
		mbedtls_strerror(ret, error_buf, 100);
		mbedtls_printf("Last error was: %d - %s\n", ret, error_buf);
	}

	// reset ssl setup and client_fd to prepare for the new TLS connection
	mbedtls_net_free(&client_fd);
	mbedtls_ssl_session_reset(&ssl);

	mbedtls_printf("Waiting for a remote connection request...\n");
	if ((ret = mbedtls_net_accept(&listen_fd, &client_fd,
		NULL, 0, NULL)) != 0)
	{
		char errbuf[512];
		mbedtls_strerror(ret, errbuf, sizeof(errbuf));
		mbedtls_printf(" failed\n  ! mbedtls_net_accept returned %d\n\n", ret);
		mbedtls_printf("%s\n", errbuf);
		goto exit;
	}

	// TODO: dump active TLS parameters used here
	mbedtls_printf("mbedtls_net_accept returned successfully.(listen_fd = %d) (client_fd = %d) \n", listen_fd.fd, client_fd.fd);

	// set up bio callbacks
	mbedtls_ssl_set_bio(&ssl, &client_fd, mbedtls_net_send, mbedtls_net_recv, NULL);

	mbedtls_printf("Performing the SSL/TLS handshake...\n");
	while ((ret = mbedtls_ssl_handshake(&ssl)) != 0)
	{
		if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
		{
			mbedtls_printf(" failed\n  ! mbedtls_ssl_handshake returned -0x%x\n", -ret);
			goto exit;
		}
	}

	mbedtls_printf("mbedtls_ssl_handshake done successfully\n");

	// Read client's request
	mbedtls_printf("< Read from client:\n");
	do
	{
		len = sizeof(buf) - 1;
		memset(buf, 0, sizeof(buf));
		ret = mbedtls_ssl_read(&ssl, buf, len);

		if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)
			continue;

		if (ret <= 0)
		{
			switch (ret)
			{
			case MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY:
				mbedtls_printf("connection was closed gracefully\n");
				break;

			case MBEDTLS_ERR_NET_CONN_RESET:
				mbedtls_printf("connection was reset by peer\n");
				break;

			default:
				mbedtls_printf("mbedtls_ssl_read returned -0x%x\n", -ret);
				break;
			}
			break;
		}

		len = ret;
		mbedtls_printf(" %d bytes read\n\n%s", len, (char *)buf);
	#ifdef ADD_TEST_CHECKING
		if (len != CLIENT_REQUEST_PAYLOAD_SIZE) // hard coded to match client
		{
			mbedtls_printf("ERROR: expected reading %d bytes but only got %d bytes\n", CLIENT_REQUEST_PAYLOAD_SIZE, len);
			ret = MBEDTLS_EXIT_FAILURE;
			goto exit;
		}
	#endif
		if (ret > 0)
			break;
	} while (1);

	// Write a response back to the client
	mbedtls_printf("> Write to client:\n");
	len = snprintf((char *)buf, sizeof(buf) - 1, HTTP_RESPONSE, mbedtls_ssl_get_ciphersuite(&ssl));
	while ((ret = mbedtls_ssl_write(&ssl, buf, len)) <= 0)
	{
		if (ret == MBEDTLS_ERR_NET_CONN_RESET)
		{
			mbedtls_printf(" failed\n  ! peer closed the connection\n\n");
			goto waiting_for_connection_request;
		}
		if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
		{
			mbedtls_printf(" failed\n  ! mbedtls_ssl_write returned %d\n\n", ret);
			goto exit;
		}
	}

	len = ret;
	mbedtls_printf(" %d bytes written\n", len);

	mbedtls_printf("Closing the connection...\n");
	while ((ret = mbedtls_ssl_close_notify(&ssl)) < 0)
	{
		if (ret != MBEDTLS_ERR_SSL_WANT_READ &&
			ret != MBEDTLS_ERR_SSL_WANT_WRITE)
		{
			mbedtls_printf("failed! mbedtls_ssl_close_notify returned %d\n\n", ret);
			goto waiting_for_connection_request;
		}
	}

	ret = 0;
	goto waiting_for_connection_request;
exit:

	if (ret != 0)
	{
		char error_buf[100];
		mbedtls_strerror(ret, error_buf, 100);
		mbedtls_printf("Last error was: %d - %s\n\n", ret, error_buf);
	}

	// free sockets
	mbedtls_net_free(&client_fd);
	mbedtls_net_free(&listen_fd);
	// free certificate resource
	mbedtls_x509_crt_free(&server_cert);
	mbedtls_pk_free(&pkey);
	// free ssl resource
	mbedtls_ssl_free(&ssl);
	mbedtls_ssl_config_free(&conf);
#if defined(MBEDTLS_SSL_CACHE_C)
	mbedtls_ssl_cache_free(&cache);
#endif
	mbedtls_ctr_drbg_free(&ctr_drbg);
	mbedtls_entropy_free(&entropy);
	fflush(stdout);
	return(ret);
}

