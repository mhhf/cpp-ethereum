/*
	This file is part of cpp-ethereum.

	cpp-ethereum is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	cpp-ethereum is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file main.cpp
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 * Ethereum client.
 */

#include <boost/asio.hpp>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>

#include <thread>
#include <chrono>
#include <fstream>
#include <algorithm>
#include "Defaults.h"
#include "Client.h"
#include "PeerNetwork.h"
#include "BlockChain.h"
#include "State.h"
#include "FileSystem.h"
#include "Instruction.h"
#include "RLP.h"

#include <mongo/client/dbclient.h>

using namespace std;
using namespace eth;
using namespace mongo;

#define ADD_QUOTES_HELPER(s) #s
#define ADD_QUOTES(s) ADD_QUOTES_HELPER(s)

bool isTrue(std::string const& _m)
{
	return _m == "on" || _m == "yes" || _m == "true" || _m == "1";
}

bool isFalse(std::string const& _m)
{
	return _m == "off" || _m == "no" || _m == "false" || _m == "0";
}

void help()
{
	cout
        << "Usage eth [OPTIONS] <remote-host>" << endl
        << "Options:" << endl
        << "    -a,--address <addr>  Set the coinbase (mining payout) address to addr (default: auto)." << endl
        << "    -c,--client-name <name>  Add a name to your client's version string (default: blank)." << endl
        << "    -d,--db-path <path>  Load database from path (default:  ~/.ethereum " << endl
        << "                         <APPDATA>/Etherum or Library/Application Support/Ethereum)." << endl
        << "    -h,--help  Show this help message and exit." << endl
        << "    -i,--interactive  Enter interactive mode (default: non-interactive)." << endl
        << "    -l,--listen <port>  Listen on the given port for incoming connected (default: 30303)." << endl
		<< "    -m,--mining <on/off/number>  Enable mining, optionally for a specified number of blocks (Default: off)" << endl
        << "    -n,--upnp <on/off>  Use upnp for NAT (default: on)." << endl
        << "    -o,--mode <full/peer>  Start a full node or a peer node (Default: full)." << endl
        << "    -p,--port <port>  Connect to remote port (default: 30303)." << endl
        << "    -r,--remote <host>  Connect to remote host (default: none)." << endl
        << "    -s,--secret <secretkeyhex>  Set the secret key for use with send command (default: auto)." << endl
        << "    -u,--public-ip <ip>  Force public ip to given (default; auto)." << endl
        << "    -v,--verbosity <0 - 9>  Set the log verbosity from 0 to 9 (Default: 8)." << endl
        << "    -x,--peers <number>  Attempt to connect to given number of peers (Default: 5)." << endl
        << "    -V,--version  Show the version and exit." << endl;
        exit(0);
}

void version()
{
	cout << "eth version " << ADD_QUOTES(ETH_VERSION) << endl;
	cout << "Build: " << ADD_QUOTES(ETH_BUILD_PLATFORM) << "/" << ADD_QUOTES(ETH_BUILD_TYPE) << endl;
	exit(0);
}


BSONObj getBSONContract(const Client& c, const Address& address)
{
	BSONObjBuilder contract;

	contract.append("_id", toString(address));
	u256 contract_balance = c.state().balance(address);
	contract.append("balance", toString(contract_balance));

	BSONObjBuilder contract_memory;
	auto mem = c.state().contractMemory(address);
	for (auto i : mem)
	{
		contract_memory.append(toString(i.first), toString(i.second));
	}

	contract.append("memory", contract_memory.obj());
	return contract.obj();
}


void exportBlock(mongo::DBClientConnection& db, Client& c)
{

}


void updateMongoDB(mongo::DBClientConnection& db, Client& c, h256& lastBlock)
{
	auto const& bc = c.blockChain();

	for (auto h = bc.currentHash(); h != lastBlock; h = bc.details(h).parent)
	{
		auto d = bc.details(h);
		auto blockData = bc.block(h);
		auto block = RLP(blockData);
		BlockInfo info(blockData);

		cout << "block: " << d.number << endl;

		for (auto const& i : block[1])
		{
			Transaction tx(i.data());

			// a new contract
			if (!tx.receiveAddress) {

				Address address = right160(tx.sha3());
				BSONObj contract = getBSONContract(c, address);
				db.insert("webeth.contracts", contract);
			}
			else if (c.state().isContractAddress(tx.receiveAddress))
			{
				BSONObj contract = getBSONContract(c, tx.receiveAddress);
				db.update("webeth.contracts", BSON("_id" << contract["_id"]), BSON("$set" << BSON("balance" << contract["balance"])));
			}
			else
			{

			}
		}

		BSONObjBuilder b;
		b.append("_id", toString(h));
		b.append("number", (long long)d.number); // this is actualy unigned long long
		//b.appendNumber("number", (long long)d.number);
		b.appendTimestamp("timestamp", (unsigned long long)info.timestamp);
		BSONObj p = b.obj();
		db.insert("webeth.blocks", p);
	}


	lastBlock = bc.currentHash();
}


void executeTransaction(Client& c, Transaction const& t)
{
	/*
	Secret secret = h256(fromUserHex(sechex));
	Address dest = h160(fromUserHex(rechex));
	c.transact(secret, dest, amount);
	*/
}

void synchronizeMongoDB(mongo::DBClientConnection& db, Client& c)
{
	// check if there are any requested transactions
	static const char * requests = "webeth.requests";
	if(db.count(requests) > 0) 
	{
		auto_ptr<DBClientCursor> cursor = db.query(requests, BSONObj());
		while ( cursor->more() ) {
            BSONObj obj = cursor->next();
			cout << obj.toString() << endl;
        }
	}

	// update the state
	//exportStateToMongoDB(db, c);
}


int main(int argc, char** argv)
{
	unsigned short listenPort = 30303;
	string remoteHost;
	unsigned short remotePort = 30303;

	string dbPath;
	eth::uint mining = ~(eth::uint)0;
	NodeMode mode = NodeMode::Full;
	unsigned peers = 5;
	string publicIP;
	bool upnp = true;
	string clientName;

	// Init defaults
	Defaults::get();

	// Our address.
	KeyPair us = KeyPair::create();
	Address coinbase = us.address();

	string configFile = getDataDir() + "/config.rlp";
	bytes b = contents(configFile);

	if (b.size())
	{
		RLP config(b);
		us = KeyPair(config[0].toHash<Secret>());
		coinbase = config[1].toHash<Address>();
	}
	else
	{
		RLPStream config(2);
		config << us.secret() << coinbase;
		writeFile(configFile, config.out());
	}

	for (int i = 1; i < argc; ++i)
	{
		string arg = argv[i];
		if ((arg == "-l" || arg == "--listen" || arg == "--listen-port") && i + 1 < argc)
			listenPort = (short)atoi(argv[++i]);
		else if ((arg == "-u" || arg == "--public-ip" || arg == "--public") && i + 1 < argc)
			publicIP = argv[++i];
		else if ((arg == "-r" || arg == "--remote") && i + 1 < argc)
			remoteHost = argv[++i];
		else if ((arg == "-p" || arg == "--port") && i + 1 < argc)
			remotePort = (short)atoi(argv[++i]);
		else if ((arg == "-n" || arg == "--upnp") && i + 1 < argc)
		{
			string m = argv[++i];
			if (isTrue(m))
				upnp = true;
			else if (isFalse(m))
				upnp = false;
			else
			{
				cerr << "Invalid UPnP option: " << m << endl;
				return -1;
			}
		}
		else if ((arg == "-c" || arg == "--client-name") && i + 1 < argc)
			clientName = argv[++i];
		else if ((arg == "-a" || arg == "--address" || arg == "--coinbase-address") && i + 1 < argc)
			coinbase = h160(fromUserHex(argv[++i]));
		else if ((arg == "-s" || arg == "--secret") && i + 1 < argc)
			us = KeyPair(h256(fromUserHex(argv[++i])));
		else if ((arg == "-d" || arg == "--path" || arg == "--db-path") && i + 1 < argc)
			dbPath = argv[++i];
		else if ((arg == "-v" || arg == "--verbosity") && i + 1 < argc)
			g_logVerbosity = atoi(argv[++i]);
		else if ((arg == "-x" || arg == "--peers") && i + 1 < argc)
			peers = atoi(argv[++i]);
		else if ((arg == "-o" || arg == "--mode") && i + 1 < argc)
		{
			string m = argv[++i];
			if (m == "full")
				mode = NodeMode::Full;
			else if (m == "peer")
				mode = NodeMode::PeerServer;
			else
			{
				cerr << "Unknown mode: " << m << endl;
				return -1;
			}
		}
		else if (arg == "-h" || arg == "--help")
			help();
		else if (arg == "-V" || arg == "--version")
			version();
		else
			remoteHost = argv[i];
	}

	if (!clientName.empty()) {
		clientName += "/";
	}

	Client c("Ethereum(++)/" + clientName + "v" ADD_QUOTES(ETH_VERSION) "/" ADD_QUOTES(ETH_BUILD_TYPE) "/" ADD_QUOTES(ETH_BUILD_PLATFORM), coinbase, dbPath);

	cout << "Address: " << endl << asHex(us.address().asArray()) << endl;
	c.startNetwork(listenPort, remoteHost, remotePort, mode, peers, publicIP, upnp);
	eth::uint n = c.blockChain().details().number;


	// connect to db
	mongo::DBClientConnection db;
	db.connect("localhost");


	// get the latest block from the db
	BSONObj lastBlock = db.findOne("webeth.blocks", Query().sort("number", -1)); // get the block with the highest number
	h256 lastBlockId = c.blockChain().genesisHash();
	if (lastBlock.hasField("_id")) {
		lastBlockId = h256(fromUserHex(lastBlock["_id"].str()));
		cout << "last exported block : " << lastBlockId << endl;
	}
	//h256 begin = bc.details(lastId).parent;
	

	while (true)
	{
		// check the database every 1s
		updateMongoDB(db, c, lastBlockId);
		this_thread::sleep_for(chrono::milliseconds(1000));
		cout << "step" << endl;
	}

	return 0;
}
