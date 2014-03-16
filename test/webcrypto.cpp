/*

 */

#include <random>
#include <secp256k1.h>
#include <Common.h>
#include <RLP.h>
#include <Log.h>
#include <Transaction.h>
using namespace std;
using namespace eth;

int webcryptoTest()
{
	cnote << "Testing some crypto stuff";

	// NOTE: someow veryfy seens not to work
	// secp256k1_ecdsa_sign_compact fails, whereas secp256k1_ecdsa_recover_compact recovers the correct key

	secp256k1_start();
	{
		string pubkey_s    = "04d309d44055367fe462287a09f952dc7e4599c54078e182e2bc7d4eac24c258076efe1e1263f34710e6acc81aa51f394dc5715d0bc4519090dba1c30fd09b809e";
		string prkey_s     = "b6278852cf5ea552c66daf22ada11b9c41646d0da173c36eaeb518aea4f4dfa7";
		string msg_s = "foobar";
		string signature_s = "a4909e20d435aa40eee9a5e0d599432f05ad6f0220b6c21048c3bf4c5e159775457a94a44d74e716712e25c181d38d3265e140aa7fbbc35766bfcea339efbc33";

		bytes privkey = fromHex(prkey_s);
		bytes pubkey = fromHex(pubkey_s);
		bytes signature = fromHex(signature_s);
		//bytes msg = fromHex(msg_s);
		
		int k = secp256k1_ecdsa_verify((byte const*)msg_s.data(), msg_s.size(), (byte const*)signature.data(), signature.size(), (byte const*)pubkey.data(), pubkey.size());
		cout << "tada: " << k << endl;

		bytes pubkey2(65);
		int pubkeylen = 65;
		int ret = secp256k1_ecdsa_seckey_verify(privkey.data());
		cout << "SEC: " << dec << ret << " " << toHex(privkey) << endl;
		ret = secp256k1_ecdsa_pubkey_create(pubkey2.data(), &pubkeylen, privkey.data(), 1);
		pubkey2.resize(pubkeylen);
		int good = secp256k1_ecdsa_pubkey_verify(pubkey2.data(), (int)pubkey2.size());
		cout << "PUB: " << dec << ret << " " << pubkeylen << " " << toHex(pubkey2) << (good ? " GOOD" : " BAD") << endl;


		bytes sig(64);
		u256 nonce = 0;
		int v = 0;
		ret = secp256k1_ecdsa_sign_compact((byte const*)msg_s.data(), (int)msg_s.size(), sig.data(), privkey.data(), (byte const*)&nonce, &v);
		cout << "MYSIG: " << dec << ret << " " << sig.size() << " " << toHex(sig) << " " << v << endl;

		k = secp256k1_ecdsa_verify((byte const*)msg_s.data(), msg_s.size(), (byte const*)sig.data(), sig.size(), (byte const*)pubkey2.data(), pubkey2.size());
		cout << "tada: " << k << endl;

		bytes pubkey3(65);
		int pubkeylen3 = 65;
		ret = secp256k1_ecdsa_recover_compact((byte const*)msg_s.data(), (int)msg_s.size(), (byte const*)signature.data(), pubkey3.data(), &pubkeylen3, 0, 28);
		cout << "tada: " << ret << " " << toHex(pubkey3) << endl;
	}

	
	return 0;
}

