// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "crypter.h"
#include "crypto/aes.h"
#include "crypto/sha512.h"

#include "script/script.h"
#include "script/standard.h"
#include "util.h"
#include <string>
#include <vector>
#include <boost/foreach.hpp>
#include <openssl/md5.h>

#include <openssl/aes.h>
#include <openssl/evp.h>
#include <iostream>
#include <fstream>
#include <string>
#include <openssl/pem.h> // For RSA Key Export
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>


bool fKeySetBiblePay;
unsigned char chKeyBiblePay[256];
unsigned char chIVBiblePay[256];

// RSA
const unsigned int RSA_KEYLEN = 2048;
const unsigned int KEY_SERVER_PRI = 0;
const unsigned int KEY_SERVER_PUB = 1;
const unsigned int KEY_CLIENT_PUB = 2;
const unsigned int KEY_AES        = 3;
const unsigned int KEY_AES_IV     = 4;
const unsigned int KEY_CLIENT_PRI = 5;
// END RSA


int CCrypter::BytesToKeySHA512AES(const std::vector<unsigned char>& chSalt, const SecureString& strKeyData, int count, unsigned char *key,unsigned char *iv) const
{
    // This mimics the behavior of openssl's EVP_BytesToKey with an aes256cbc
    // cipher and sha512 message digest. Because sha512's output size (64b) is
    // greater than the aes256 block size (16b) + aes256 key size (32b),
    // there's no need to process more than once (D_0).

    if(!count || !key || !iv)
        return 0;

    unsigned char buf[CSHA512::OUTPUT_SIZE];
    CSHA512 di;

    di.Write((const unsigned char*)strKeyData.c_str(), strKeyData.size());
    if(chSalt.size())
        di.Write(&chSalt[0], chSalt.size());
    di.Finalize(buf);

    for(int i = 0; i != count - 1; i++)
        di.Reset().Write(buf, sizeof(buf)).Finalize(buf);

    memcpy(key, buf, WALLET_CRYPTO_KEY_SIZE);
    memcpy(iv, buf + WALLET_CRYPTO_KEY_SIZE, WALLET_CRYPTO_IV_SIZE);
    memory_cleanse(buf, sizeof(buf));
    return WALLET_CRYPTO_KEY_SIZE;
}

bool CCrypter::SetKeyFromPassphrase(const SecureString& strKeyData, const std::vector<unsigned char>& chSalt, const unsigned int nRounds, const unsigned int nDerivationMethod)
{
    if (nRounds < 1 || chSalt.size() != WALLET_CRYPTO_SALT_SIZE)
        return false;

    int i = 0;
    if (nDerivationMethod == 0)
        i = BytesToKeySHA512AES(chSalt, strKeyData, nRounds, vchKey.data(), vchIV.data());

    if (i != (int)WALLET_CRYPTO_KEY_SIZE)
    {
        memory_cleanse(vchKey.data(), vchKey.size());
        memory_cleanse(vchIV.data(), vchIV.size());
        return false;
    }

    fKeySet = true;
    return true;
}

bool CCrypter::SetKey(const CKeyingMaterial& chNewKey, const std::vector<unsigned char>& chNewIV)
{
    if (chNewKey.size() != WALLET_CRYPTO_KEY_SIZE || chNewIV.size() != WALLET_CRYPTO_IV_SIZE)
        return false;

    memcpy(vchKey.data(), chNewKey.data(), chNewKey.size());
    memcpy(vchIV.data(), chNewIV.data(), chNewIV.size());

    fKeySet = true;
    return true;
}

bool CCrypter::Encrypt(const CKeyingMaterial& vchPlaintext, std::vector<unsigned char> &vchCiphertext) const
{
    if (!fKeySet)
        return false;

    // max ciphertext len for a n bytes of plaintext is
    // n + AES_BLOCKSIZE bytes
    vchCiphertext.resize(vchPlaintext.size() + AES_BLOCKSIZE);

    AES256CBCEncrypt enc(vchKey.data(), vchIV.data(), true);
    size_t nLen = enc.Encrypt(&vchPlaintext[0], vchPlaintext.size(), &vchCiphertext[0]);
    if(nLen < vchPlaintext.size())
        return false;
    vchCiphertext.resize(nLen);

    return true;
}

bool CCrypter::Decrypt(const std::vector<unsigned char>& vchCiphertext, CKeyingMaterial& vchPlaintext) const
{
    if (!fKeySet)
        return false;

    // plaintext will always be equal to or lesser than length of ciphertext
    int nLen = vchCiphertext.size();

    vchPlaintext.resize(nLen);

    AES256CBCDecrypt dec(vchKey.data(), vchIV.data(), true);
    nLen = dec.Decrypt(&vchCiphertext[0], vchCiphertext.size(), &vchPlaintext[0]);
    if(nLen == 0)
        return false;
    vchPlaintext.resize(nLen);
    return true;
}

static bool EncryptSecret(const CKeyingMaterial& vMasterKey, const CKeyingMaterial &vchPlaintext, const uint256& nIV, std::vector<unsigned char> &vchCiphertext)
{
    CCrypter cKeyCrypter;
    std::vector<unsigned char> chIV(WALLET_CRYPTO_IV_SIZE);
    memcpy(&chIV[0], &nIV, WALLET_CRYPTO_IV_SIZE);
    if(!cKeyCrypter.SetKey(vMasterKey, chIV))
        return false;
    return cKeyCrypter.Encrypt(*((const CKeyingMaterial*)&vchPlaintext), vchCiphertext);
}

std::string RPAD(std::string sUnpadded, int nLength)
{
	sUnpadded += "0000000000000000000000000000000000000000000000000000000000000000";
	sUnpadded = sUnpadded.substr(0, nLength);
	return sUnpadded;
}

std::string DecryptAES256(std::string s64, std::string sKey)
{
	std::string sIV = "eb5a781ea9da2ef3";
	std::string sEnc = DecodeBase64(s64);
	sKey = RPAD(sKey, 32);

	SecureString sSCKey(sKey.c_str());
    SecureString sValue;
    if(!DecryptAES256(sSCKey, sEnc, sIV, sValue))
    {
		return std::string();
    }
    return sValue.c_str();
}

std::string EncryptAES256(std::string sPlaintext, std::string sKey)
{
	std::string sIV = "eb5a781ea9da2ef3";
	sKey = RPAD(sKey, 32);
	SecureString sSCKey(sKey.c_str());

	std::string sCipherValue;
	SecureString SCPlainText(sPlaintext.c_str());
	bool fSuccess = EncryptAES256(sSCKey, SCPlainText, sIV, sCipherValue);
	if (!fSuccess)
	{
		return std::string();
	}
	std::string sEnc = EncodeBase64(sCipherValue);
	return sEnc;
}

// General secure AES 256 CBC encryption routine
bool EncryptAES256(const SecureString& sKey, const SecureString& sPlaintext, const std::string& sIV, std::string& sCiphertext)
{
    // Verify key sizes
    if(sKey.size() != 32 || sIV.size() != AES_BLOCKSIZE) {
        LogPrintf("crypter EncryptAES256 - Invalid key or block size: Key: %d sIV:%d\n", sKey.size(), sIV.size());
        return false;
    }

    // max ciphertext len for a n bytes of plaintext is
    // n + AES_BLOCKSIZE bytes
    sCiphertext.resize(sPlaintext.size() + AES_BLOCKSIZE);

    AES256CBCEncrypt enc((const unsigned char*) &sKey[0], (const unsigned char*) &sIV[0], true);
    size_t nLen = enc.Encrypt((const unsigned char*) &sPlaintext[0], sPlaintext.size(), (unsigned char*) &sCiphertext[0]);
    if(nLen < sPlaintext.size())
        return false;
    sCiphertext.resize(nLen);
    return true;
}

bool LoadBibleKey(std::string biblekey, std::string salt)
{
	const char* chBibleKey = biblekey.c_str();
	const char* chSalt = salt.c_str();
	OPENSSL_cleanse(chKeyBiblePay, sizeof(chKeyBiblePay));
    OPENSSL_cleanse(chIVBiblePay, sizeof(chIVBiblePay));
    EVP_BytesToKey(EVP_aes_256_cbc(), EVP_sha512(),(unsigned char *)chSalt,
		(unsigned char *)chBibleKey, 
		strlen(chBibleKey), 1,
		chKeyBiblePay, chIVBiblePay);
    fKeySetBiblePay = true;
    return true;
}

std::vector<unsigned char> StringToVector(std::string sData)
{
    std::vector<unsigned char> v(sData.begin(), sData.end());
	return v;
}

std::string VectorToString(std::vector<unsigned char> v)
{
     std::string s(v.begin(), v.end());
     return s;
}


bool BibleEncrypt(std::vector<unsigned char> vchPlaintext, std::vector<unsigned char> &vchCiphertext)
{
	if (!fKeySetBiblePay) LoadBibleKey("biblepay","eb5a781ea9da2ef36");
    int nLen = vchPlaintext.size();
    int nCLen = nLen + AES_BLOCK_SIZE, nFLen = 0;
    vchCiphertext = std::vector<unsigned char> (nCLen);
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    bool fOk = true;
    EVP_CIPHER_CTX_init(ctx);
	if (fOk) fOk = EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, chKeyBiblePay, chIVBiblePay);
    if (fOk) fOk = EVP_EncryptUpdate(ctx, &vchCiphertext[0], &nCLen, &vchPlaintext[0], nLen);
    if (fOk) fOk = EVP_EncryptFinal_ex(ctx, (&vchCiphertext[0])+nCLen, &nFLen);
    EVP_CIPHER_CTX_free(ctx);
    if (!fOk) return false;
    vchCiphertext.resize(nCLen + nFLen);
    return true;
}

bool BibleDecrypt(const std::vector<unsigned char>& vchCiphertext,std::vector<unsigned char>& vchPlaintext)
{
	LoadBibleKey("biblepay","eb5a781ea9da2ef36");
	int nLen = vchCiphertext.size();
    int nPLen = nLen, nFLen = 0;
    //EVP_CIPHER_CTX ctx;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    bool fOk = true;
    EVP_CIPHER_CTX_init(ctx);
    if (fOk) fOk = EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, chKeyBiblePay, chIVBiblePay);
    if (fOk) fOk = EVP_DecryptUpdate(ctx, &vchPlaintext[0], &nPLen, &vchCiphertext[0], nLen);
    if (fOk) fOk = EVP_DecryptFinal_ex(ctx, (&vchPlaintext[0])+nPLen, &nFLen);
    EVP_CIPHER_CTX_free(ctx);
    if (!fOk) return false;
    vchPlaintext.resize(nPLen + nFLen);
    return true;
}

static bool DecryptSecret(const CKeyingMaterial& vMasterKey, const std::vector<unsigned char>& vchCiphertext, const uint256& nIV, CKeyingMaterial& vchPlaintext)
{
    CCrypter cKeyCrypter;
    std::vector<unsigned char> chIV(WALLET_CRYPTO_IV_SIZE);
    memcpy(&chIV[0], &nIV, WALLET_CRYPTO_IV_SIZE);
    if(!cKeyCrypter.SetKey(vMasterKey, chIV))
        return false;
    return cKeyCrypter.Decrypt(vchCiphertext, *((CKeyingMaterial*)&vchPlaintext));
}

// General secure AES 256 CBC decryption routine
bool DecryptAES256(const SecureString& sKey, const std::string& sCiphertext, const std::string& sIV, SecureString& sPlaintext)
{
    // Verify key sizes
    if(sKey.size() != 32 || sIV.size() != AES_BLOCKSIZE) {
        LogPrintf("crypter DecryptAES256 - Invalid key or block size\n");
        return false;
    }

    // plaintext will always be equal to or lesser than length of ciphertext
    int nLen = sCiphertext.size();

    sPlaintext.resize(nLen);

    AES256CBCDecrypt dec((const unsigned char*) &sKey[0], (const unsigned char*) &sIV[0], true);
    nLen = dec.Decrypt((const unsigned char*) &sCiphertext[0], sCiphertext.size(), (unsigned char*) &sPlaintext[0]);
    if(nLen == 0)
        return false;
    sPlaintext.resize(nLen);
    return true;
}


static bool DecryptKey(const CKeyingMaterial& vMasterKey, const std::vector<unsigned char>& vchCryptedSecret, const CPubKey& vchPubKey, CKey& key)
{
    CKeyingMaterial vchSecret;
    if(!DecryptSecret(vMasterKey, vchCryptedSecret, vchPubKey.GetHash(), vchSecret))
        return false;

    if (vchSecret.size() != 32)
        return false;

    key.Set(vchSecret.begin(), vchSecret.end(), vchPubKey.IsCompressed());
    return key.VerifyPubKey(vchPubKey);
}

/* R ANDREWS - BIBLEPAY - 9/13/2018 - ADD SUPPORT FOR RSA */


int RSA_WRITE_KEY_TO_FILE(FILE *file, int key, EVP_PKEY *rKey)
{
	switch(key) 
	{
		case KEY_SERVER_PRI:
			 if(!PEM_write_PrivateKey(file, rKey, NULL, NULL, 0, 0, NULL))         return -1;
			 break;
		case KEY_CLIENT_PRI:
			 if(!PEM_write_PrivateKey(file, rKey, NULL, NULL, 0, 0, NULL))         return -1;
			 break;
		case KEY_SERVER_PUB:
			 if(!PEM_write_PUBKEY(file, rKey))                                     return -1;
			 break;
		case KEY_CLIENT_PUB:
			 if(!PEM_write_PUBKEY(file, rKey))                                     return -1;
			 break;
		default:
			return -1;
	}
	return 1;
}


int RSA_GENERATE_KEYPAIR(std::string sPublicKeyPath, std::string sPrivateKeyPath)
{
	if (sPublicKeyPath.empty() || sPrivateKeyPath.empty()) return -1;
    EVP_PKEY *remotePublicKey = NULL;
	EVP_PKEY_CTX *context = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
	if(EVP_PKEY_keygen_init(context) <= 0) return -1;
	if(EVP_PKEY_CTX_set_rsa_keygen_bits(context, RSA_KEYLEN) <= 0) return -1;
	if(EVP_PKEY_keygen(context, &remotePublicKey) <= 0) return -1;
	EVP_PKEY_CTX_free(context);
    FILE *outFile = fopen(sPublicKeyPath.c_str(), "w");
	RSA_WRITE_KEY_TO_FILE(outFile, KEY_CLIENT_PUB, remotePublicKey);
	fclose(outFile);
	FILE *outFile2 = fopen(sPrivateKeyPath.c_str(), "w");
	RSA_WRITE_KEY_TO_FILE(outFile2, KEY_CLIENT_PRI, remotePublicKey);
	fclose(outFile2);
	return 1;
}

EVP_PKEY *RSA_LOAD_PUBKEY(const char *file)
{
	RSA *rsa_pkey = NULL;
	BIO *rsa_pkey_file = NULL;
	EVP_PKEY *pkey = EVP_PKEY_new();
	// Create a new BIO file structure to be used with PEM file
	rsa_pkey_file = BIO_new(BIO_s_file());
	if (rsa_pkey_file == NULL)
	{
		fprintf(stderr, "RSA_LOAD_PUBKEY::Error creating a new BIO file.\n");
		goto end;
	}
	
	// Read PEM file using BIO's file structure
	if (BIO_read_filename(rsa_pkey_file, file) <= 0)
	{
		fprintf(stderr, "RSA_LOAD_PUBKEY::Error opening %s\n",file);
		goto end;
	}

	// Read RSA based PEM file into rsa_pkey structure
	if (!PEM_read_bio_RSA_PUBKEY(rsa_pkey_file, &rsa_pkey, NULL, NULL))
	{
		fprintf(stderr, "Error loading RSA Public Key File.\n");
		goto end;
	}

	// Populate pkey with the rsa key. rsa_pkey is owned by pkey, therefore if we free pkey, rsa_pkey will be freed  too
    if (!EVP_PKEY_assign_RSA(pkey, rsa_pkey))
    {
        fprintf(stderr, "Error assigning EVP_PKEY_assign_RSA: failed.\n");
        goto end;
    }

end:
	if (rsa_pkey_file != NULL)
		BIO_free(rsa_pkey_file);
	if (pkey == NULL)
	{
		fprintf(stderr, "RSA_LOAD_PUBKEY::Error unable to load %s\n", file);
	}
	return(pkey);
}

EVP_PKEY *RSA_LOAD_PRIVKEY(const char *file)
{
	RSA *rsa_pkey = NULL;
	BIO *rsa_pkey_file = NULL;
	EVP_PKEY *pkey = EVP_PKEY_new();
	// Create a new BIO file structure to be used with PEM file
	rsa_pkey_file = BIO_new(BIO_s_file());
	if (rsa_pkey_file == NULL)
	{
		fprintf(stderr, "Error creating a new BIO file.\n");
		goto end;
	}
	// Read PEM file using BIO's file structure
	if (BIO_read_filename(rsa_pkey_file, file) <= 0)
	{
		fprintf(stderr, "Error opening %s\n",file);
		goto end;
	}
	// Read RSA based PEM file into rsa_pkey structure
	if (!PEM_read_bio_RSAPrivateKey(rsa_pkey_file, &rsa_pkey, NULL, NULL))
	{
		fprintf(stderr, "Error loading RSA Private Key File.\n");
		goto end;
	}
	// Populate pkey with the rsa key. rsa_pkey is owned by pkey,
	// therefore if we free pkey, rsa_pkey will be freed  too
    if (!EVP_PKEY_assign_RSA(pkey, rsa_pkey))
    {
        fprintf(stderr, "Error assigning EVP_PKEY_assign_RSA: failed.\n");
        goto end;
    }

end:
	if (rsa_pkey_file != NULL)
		BIO_free(rsa_pkey_file);
	if (pkey == NULL)
	{
		fprintf(stderr, "RSA_Load_Private_Key::Error unable to load %s\n", file);
	}
	return(pkey);
}


std::vector<char> ReadAllBytes(char const* filename)
{
    std::ifstream ifs(filename, std::ios::binary|std::ios::ate);
    std::ifstream::pos_type pos = ifs.tellg();
    std::vector<char>  result(pos);
    ifs.seekg(0, std::ios::beg);
    ifs.read(&result[0], pos);
    return result;
}

std::string GetSANDirectory3()
{
	 boost::filesystem::path pathConfigFile(GetArg("-conf", "biblepay.conf"));
     if (!pathConfigFile.is_complete()) pathConfigFile = GetDataDir(false) / pathConfigFile;
	 boost::filesystem::path dir = pathConfigFile.parent_path();
	 std::string sDir = dir.string() + "/SAN/";
	 boost::filesystem::path pathSAN(sDir);
	 if (!boost::filesystem::exists(pathSAN))
	 {
		 boost::filesystem::create_directory(pathSAN);
	 }
	 return sDir;
}

unsigned char *RSA_ENCRYPT_CHAR(std::string sPubKeyPath, unsigned char *plaintext, int plaintext_length, int& cipher_len, std::string& sError)
{
	EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
	EVP_CIPHER_CTX_init(ctx);
	EVP_PKEY *pkey;
	unsigned char iv[EVP_MAX_IV_LENGTH];
	unsigned char *encrypted_key;
	int encrypted_key_length;
	uint32_t eklen_n;
	unsigned char *ciphertext;
	// Load RSA Public Key File (usually this is a PEM ASCII file with boundaries or armor)
	pkey = RSA_LOAD_PUBKEY(sPubKeyPath.c_str());
	if (pkey == NULL)
	{
	    ciphertext = (unsigned char*)malloc(1);
		sError = "Error loading public key.";
		return ciphertext;
	}
	encrypted_key = (unsigned char*)malloc(EVP_PKEY_size(pkey));
	encrypted_key_length = EVP_PKEY_size(pkey);
	if (!EVP_SealInit(ctx, EVP_des_ede_cbc(), &encrypted_key, &encrypted_key_length, iv, &pkey, 1))
	{
		fprintf(stdout, "EVP_SealInit: failed.\n");
	}
	eklen_n = htonl(encrypted_key_length);
	int size_header = sizeof(eklen_n) + encrypted_key_length + EVP_CIPHER_iv_length(EVP_des_ede_cbc());
	/* compute max ciphertext len, see man EVP_CIPHER */
	int max_cipher_len = plaintext_length + EVP_CIPHER_CTX_block_size(ctx) - 1;
	ciphertext = (unsigned char*)malloc(size_header + max_cipher_len);
	/* Write out the encrypted key length, then the encrypted key, then the iv (the IV length is fixed by the cipher we have chosen). */
	int pos = 0;
	memcpy(ciphertext + pos, &eklen_n, sizeof(eklen_n));
	pos += sizeof(eklen_n);
	memcpy(ciphertext + pos, encrypted_key, encrypted_key_length);
	pos += encrypted_key_length;
	memcpy(ciphertext + pos, iv, EVP_CIPHER_iv_length(EVP_des_ede_cbc()));
	pos += EVP_CIPHER_iv_length(EVP_des_ede_cbc());
	/* Process the plaintext data and write the encrypted data to the ciphertext. cipher_len is filled with the length of ciphertext generated, len is the size of plaintext in bytes
	 * Also we have our updated position, we can skip the header via ciphertext + pos */
	int total_len = 0;
	int bytes_processed = 0;
	if (!EVP_SealUpdate(ctx, ciphertext + pos, &bytes_processed, plaintext, plaintext_length))
	{
		fprintf(stdout, "EVP_SealUpdate: failed.\n");
	}

	LogPrintf(" precipherlen %f ", bytes_processed);
	total_len += bytes_processed;
	pos += bytes_processed;
	if (!EVP_SealFinal(ctx, ciphertext + pos, &bytes_processed))
	{
		fprintf(stdout, "RSA_Encrypt::EVP_SealFinal: failed.\n");
	}
	total_len += bytes_processed;
	cipher_len = total_len;
	EVP_PKEY_free(pkey);
	free(encrypted_key);
	EVP_CIPHER_CTX_free(ctx);
	return ciphertext;
}

void RSA_Encrypt_File(std::string sPubKeyPath, std::string sSourcePath, std::string sEncryptPath, std::string& sError)
{
	std::vector<char> vMessage = ReadAllBytes(sSourcePath.c_str());
	std::vector<unsigned char> uData(vMessage.begin(), vMessage.end());
	unsigned char *ciphertext;
	int cipher_len = 0;
	unsigned char *long_ciphertext = (unsigned char *)malloc(uData.size() + 10000);
	memcpy(long_ciphertext, &uData[0], uData.size());
	size_t messageLength = uData.size() + 10000;
	ciphertext = RSA_ENCRYPT_CHAR(sPubKeyPath, long_ciphertext, messageLength, cipher_len, sError);
	if (sError.empty())
	{
		std::ofstream fd(sEncryptPath.c_str());
		fd.write((const char*)ciphertext, cipher_len);
		fd.close();
	}
}

unsigned char *RSA_DECRYPT_CHAR(std::string sPriKeyPath, unsigned char *ciphertext, int ciphrtext_size, int& plaintxt_len, std::string& sError)
{
	EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
	EVP_CIPHER_CTX_init(ctx);
	EVP_PKEY *pkey;
	unsigned char *encrypted_key;
	unsigned int encrypted_key_length;
	uint32_t eklen_n;
	unsigned char iv[EVP_MAX_IV_LENGTH];
	int bytes_processed = 0;
	// the length of udata is at most ciphertextlen + ciphers block size.
	int ciphertext_len = ciphrtext_size + EVP_CIPHER_block_size(EVP_des_ede_cbc());
	unsigned char *plaintext = (unsigned char *)malloc(ciphertext_len);
	pkey = RSA_LOAD_PRIVKEY(sPriKeyPath.c_str());
	if (pkey==NULL)
	{
		sError = "No private key provided.";
		return plaintext;
	}
	encrypted_key = (unsigned char*)malloc(EVP_PKEY_size(pkey));
	int pos = 0;
	memcpy(&eklen_n, ciphertext + pos, sizeof(eklen_n));
	pos += sizeof(eklen_n);
	encrypted_key_length = ntohl(eklen_n);
	memcpy(encrypted_key, ciphertext + pos, encrypted_key_length);
	pos += encrypted_key_length;
	memcpy(iv, ciphertext + pos, EVP_CIPHER_iv_length(EVP_des_ede_cbc()));
	pos += EVP_CIPHER_iv_length(EVP_des_ede_cbc());
	int total_len = 0;
	// Now we have our encrypted_key and the iv we can decrypt the reamining
	if (!EVP_OpenInit(ctx, EVP_des_ede_cbc(), encrypted_key, encrypted_key_length, iv, pkey))
	{
		fprintf(stderr, "RSADecrypt::EVP_OpenInit: failed.\n");
		sError = "EVP_OpenInit Failed.";
		return plaintext;
	}
	if (!EVP_OpenUpdate(ctx, plaintext, &bytes_processed, ciphertext + pos, ciphrtext_size))
	{
		fprintf(stderr, "RSA::Decrypt::EVP_OpenUpdate: failed.\n");
		sError = "EVP_OpenUpdate failed.";
		return plaintext;
	}
	total_len += bytes_processed;
	if (!EVP_OpenFinal(ctx, plaintext + total_len, &bytes_processed))
	{
		fprintf(stderr, "RSA::Decrypt::EVP_OpenFinal warning: failed.\n");
	}
	total_len += bytes_processed;

	EVP_PKEY_free(pkey);
	free(encrypted_key);

	EVP_CIPHER_CTX_free(ctx);
	plaintxt_len = total_len;
	return plaintext;
}

void RSA_Decrypt_File(std::string sPrivKeyPath, std::string sSourcePath, std::string sDecryptPath, std::string sError)
{
	std::vector<char> vMessage = ReadAllBytes(sSourcePath.c_str());
	std::vector<unsigned char> uData(vMessage.begin(), vMessage.end());
	size_t messageLength = uData.size();
	int plaintextsize = 0;
	unsigned char *decrypted;
	decrypted = RSA_DECRYPT_CHAR(sPrivKeyPath, &uData[0], messageLength, plaintextsize, sError);
	if (sError.empty())
	{
		if (plaintextsize < 10000)
		{
			sError = "RSA_Decrypt_File::Encrypted file is corrupted.";
			return;
		}
		unsigned char *short_plaintext = (unsigned char *)malloc(plaintextsize - 10000);
		memcpy(short_plaintext, decrypted, plaintextsize - 10000);
		std::ofstream fd(sDecryptPath.c_str());
		fd.write((const char*)short_plaintext, plaintextsize - 10000);
		fd.close();
	}
}


std::string RSA_Encrypt_String(std::string sPubKeyPath, std::string sData, std::string& sError)
{
	std::string sEncPath =  GetSANDirectory3() + "temp";
	std::ofstream fd(sEncPath.c_str());
	fd.write(sData.c_str(), sizeof(char)*sData.size());
	fd.close();
	std::string sTargPath = GetSANDirectory3() + "temp_enc";
	RSA_Encrypt_File(sPubKeyPath, sEncPath, sTargPath, sError);
	if (!sError.empty()) return "";
	std::vector<char> vOut = ReadAllBytes(sTargPath.c_str());
	std::string sResults(vOut.begin(), vOut.end());
	return sResults;
}

std::string RSA_Decrypt_String(std::string sPrivKeyPath, std::string sData, std::string& sError)
{
	std::string sEncPath =  GetSANDirectory3() + "temp_dec";
	std::ofstream fd(sEncPath.c_str());
	fd.write(sData.c_str(), sizeof(char)*sData.size());

	fd.close();
	std::string sTargPath = GetSANDirectory3() + "temp_dec_unenc";
	RSA_Decrypt_File(sPrivKeyPath, sEncPath, sTargPath, sError);
	if (!sError.empty()) return "";
	std::vector<char> vOut = ReadAllBytes(sTargPath.c_str());
	std::string sResults(vOut.begin(), vOut.end());
	return sResults;
}


/* END OF RSA SUPPORT - BIBLEPAY */


bool CCryptoKeyStore::SetCrypted()
{
    LOCK(cs_KeyStore);
    if (fUseCrypto)
        return true;
    if (!mapKeys.empty())
        return false;
    fUseCrypto = true;
    return true;
}

bool CCryptoKeyStore::Lock(bool fAllowMixing)
{
    if (!SetCrypted())
        return false;

    if(!fAllowMixing) {
        LOCK(cs_KeyStore);
        vMasterKey.clear();
    }

    fOnlyMixingAllowed = fAllowMixing;
    NotifyStatusChanged(this);
    return true;
}

bool CCryptoKeyStore::Unlock(const CKeyingMaterial& vMasterKeyIn, bool fForMixingOnly)
{
    {
        LOCK(cs_KeyStore);
        if (!SetCrypted())
            return false;

        bool keyPass = false;
        bool keyFail = false;
        CryptedKeyMap::const_iterator mi = mapCryptedKeys.begin();
        for (; mi != mapCryptedKeys.end(); ++mi)
        {
            const CPubKey &vchPubKey = (*mi).second.first;
            const std::vector<unsigned char> &vchCryptedSecret = (*mi).second.second;
            CKey key;
            if (!DecryptKey(vMasterKeyIn, vchCryptedSecret, vchPubKey, key))
            {
                keyFail = true;
                break;
            }
            keyPass = true;
            if (fDecryptionThoroughlyChecked)
                break;
        }
        if (keyPass && keyFail)
        {
            LogPrintf("The wallet is probably corrupted: Some keys decrypt but not all.\n");
            assert(false);
        }
        if (keyFail || (!keyPass && cryptedHDChain.IsNull()))
            return false;

        vMasterKey = vMasterKeyIn;

        if(!cryptedHDChain.IsNull()) {
            bool chainPass = false;
            // try to decrypt seed and make sure it matches
            CHDChain hdChainTmp;
            if (DecryptHDChain(hdChainTmp)) {
                // make sure seed matches this chain
                chainPass = cryptedHDChain.GetID() == hdChainTmp.GetSeedHash();
            }
            if (!chainPass) {
                vMasterKey.clear();
                return false;
            }
        }
        fDecryptionThoroughlyChecked = true;
    }
    fOnlyMixingAllowed = fForMixingOnly;
    NotifyStatusChanged(this);
    return true;
}

bool CCryptoKeyStore::AddKeyPubKey(const CKey& key, const CPubKey &pubkey)
{
    {
        LOCK(cs_KeyStore);
        if (!IsCrypted())
            return CBasicKeyStore::AddKeyPubKey(key, pubkey);

        if (IsLocked(true))
            return false;

        std::vector<unsigned char> vchCryptedSecret;
        CKeyingMaterial vchSecret(key.begin(), key.end());
        if (!EncryptSecret(vMasterKey, vchSecret, pubkey.GetHash(), vchCryptedSecret))
            return false;

        if (!AddCryptedKey(pubkey, vchCryptedSecret))
            return false;
    }
    return true;
}


bool CCryptoKeyStore::AddCryptedKey(const CPubKey &vchPubKey, const std::vector<unsigned char> &vchCryptedSecret)
{
    {
        LOCK(cs_KeyStore);
        if (!SetCrypted())
            return false;

        mapCryptedKeys[vchPubKey.GetID()] = make_pair(vchPubKey, vchCryptedSecret);
    }
    return true;
}

bool CCryptoKeyStore::GetKey(const CKeyID &address, CKey& keyOut) const
{
    {
        LOCK(cs_KeyStore);
        if (!IsCrypted())
            return CBasicKeyStore::GetKey(address, keyOut);

        CryptedKeyMap::const_iterator mi = mapCryptedKeys.find(address);
        if (mi != mapCryptedKeys.end())
        {
            const CPubKey &vchPubKey = (*mi).second.first;
            const std::vector<unsigned char> &vchCryptedSecret = (*mi).second.second;
            return DecryptKey(vMasterKey, vchCryptedSecret, vchPubKey, keyOut);
        }
    }
    return false;
}

bool CCryptoKeyStore::GetPubKey(const CKeyID &address, CPubKey& vchPubKeyOut) const
{
    {
        LOCK(cs_KeyStore);
        if (!IsCrypted())
            return CBasicKeyStore::GetPubKey(address, vchPubKeyOut);

        CryptedKeyMap::const_iterator mi = mapCryptedKeys.find(address);
        if (mi != mapCryptedKeys.end())
        {
            vchPubKeyOut = (*mi).second.first;
            return true;
        }
        // Check for watch-only pubkeys
        return CBasicKeyStore::GetPubKey(address, vchPubKeyOut);
    }
    return false;
}

bool CCryptoKeyStore::EncryptKeys(CKeyingMaterial& vMasterKeyIn)
{
    {
        LOCK(cs_KeyStore);
        if (!mapCryptedKeys.empty() || IsCrypted())
            return false;

        fUseCrypto = true;
        BOOST_FOREACH(KeyMap::value_type& mKey, mapKeys)
        {
            const CKey &key = mKey.second;
            CPubKey vchPubKey = key.GetPubKey();
            CKeyingMaterial vchSecret(key.begin(), key.end());
            std::vector<unsigned char> vchCryptedSecret;
            if (!EncryptSecret(vMasterKeyIn, vchSecret, vchPubKey.GetHash(), vchCryptedSecret))
                return false;
            if (!AddCryptedKey(vchPubKey, vchCryptedSecret))
                return false;
        }
        mapKeys.clear();
    }
    return true;
}

bool CCryptoKeyStore::EncryptHDChain(const CKeyingMaterial& vMasterKeyIn)
{
    // should call EncryptKeys first
    if (!IsCrypted())
        return false;

    if (!cryptedHDChain.IsNull())
        return true;

    if (cryptedHDChain.IsCrypted())
        return true;

    // make sure seed matches this chain
    if (hdChain.GetID() != hdChain.GetSeedHash())
        return false;

    std::vector<unsigned char> vchCryptedSeed;
    if (!EncryptSecret(vMasterKeyIn, hdChain.GetSeed(), hdChain.GetID(), vchCryptedSeed))
        return false;

    hdChain.Debug(__func__);
    cryptedHDChain = hdChain;
    cryptedHDChain.SetCrypted(true);

    SecureVector vchSecureCryptedSeed(vchCryptedSeed.begin(), vchCryptedSeed.end());
    if (!cryptedHDChain.SetSeed(vchSecureCryptedSeed, false))
        return false;

    SecureVector vchMnemonic;
    SecureVector vchMnemonicPassphrase;

    // it's ok to have no mnemonic if wallet was initialized via hdseed
    if (hdChain.GetMnemonic(vchMnemonic, vchMnemonicPassphrase)) {
        std::vector<unsigned char> vchCryptedMnemonic;
        std::vector<unsigned char> vchCryptedMnemonicPassphrase;

        if (!vchMnemonic.empty() && !EncryptSecret(vMasterKeyIn, vchMnemonic, hdChain.GetID(), vchCryptedMnemonic))
            return false;
        if (!vchMnemonicPassphrase.empty() && !EncryptSecret(vMasterKeyIn, vchMnemonicPassphrase, hdChain.GetID(), vchCryptedMnemonicPassphrase))
            return false;

        SecureVector vchSecureCryptedMnemonic(vchCryptedMnemonic.begin(), vchCryptedMnemonic.end());
        SecureVector vchSecureCryptedMnemonicPassphrase(vchCryptedMnemonicPassphrase.begin(), vchCryptedMnemonicPassphrase.end());
        if (!cryptedHDChain.SetMnemonic(vchSecureCryptedMnemonic, vchSecureCryptedMnemonicPassphrase, false))
            return false;
    }

    if (!hdChain.SetNull())
        return false;

    return true;
}

bool CCryptoKeyStore::DecryptHDChain(CHDChain& hdChainRet) const
{
    if (!IsCrypted())
        return true;

    if (cryptedHDChain.IsNull())
        return false;

    if (!cryptedHDChain.IsCrypted())
        return false;

    SecureVector vchSecureSeed;
    SecureVector vchSecureCryptedSeed = cryptedHDChain.GetSeed();
    std::vector<unsigned char> vchCryptedSeed(vchSecureCryptedSeed.begin(), vchSecureCryptedSeed.end());
    if (!DecryptSecret(vMasterKey, vchCryptedSeed, cryptedHDChain.GetID(), vchSecureSeed))
        return false;

    hdChainRet = cryptedHDChain;
    if (!hdChainRet.SetSeed(vchSecureSeed, false))
        return false;

    // hash of decrypted seed must match chain id
    if (hdChainRet.GetSeedHash() != cryptedHDChain.GetID())
        return false;

    SecureVector vchSecureCryptedMnemonic;
    SecureVector vchSecureCryptedMnemonicPassphrase;

    // it's ok to have no mnemonic if wallet was initialized via hdseed
    if (cryptedHDChain.GetMnemonic(vchSecureCryptedMnemonic, vchSecureCryptedMnemonicPassphrase)) {
        SecureVector vchSecureMnemonic;
        SecureVector vchSecureMnemonicPassphrase;

        std::vector<unsigned char> vchCryptedMnemonic(vchSecureCryptedMnemonic.begin(), vchSecureCryptedMnemonic.end());
        std::vector<unsigned char> vchCryptedMnemonicPassphrase(vchSecureCryptedMnemonicPassphrase.begin(), vchSecureCryptedMnemonicPassphrase.end());

        if (!vchCryptedMnemonic.empty() && !DecryptSecret(vMasterKey, vchCryptedMnemonic, cryptedHDChain.GetID(), vchSecureMnemonic))
            return false;
        if (!vchCryptedMnemonicPassphrase.empty() && !DecryptSecret(vMasterKey, vchCryptedMnemonicPassphrase, cryptedHDChain.GetID(), vchSecureMnemonicPassphrase))
            return false;

        if (!hdChainRet.SetMnemonic(vchSecureMnemonic, vchSecureMnemonicPassphrase, false))
            return false;
    }

    hdChainRet.SetCrypted(false);
    hdChainRet.Debug(__func__);

    return true;
}

bool CCryptoKeyStore::SetHDChain(const CHDChain& chain)
{
    if (IsCrypted())
        return false;

    if (chain.IsCrypted())
        return false;

    hdChain = chain;
    return true;
}

bool CCryptoKeyStore::SetCryptedHDChain(const CHDChain& chain)
{
    if (!SetCrypted())
        return false;

    if (!chain.IsCrypted())
        return false;

    cryptedHDChain = chain;
    return true;
}

bool CCryptoKeyStore::GetHDChain(CHDChain& hdChainRet) const
{
    if(IsCrypted()) {
        hdChainRet = cryptedHDChain;
        return !cryptedHDChain.IsNull();
    }

    hdChainRet = hdChain;
    return !hdChain.IsNull();
}
