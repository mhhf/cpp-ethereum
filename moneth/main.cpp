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

template<typename T>
string toHexStr(const T& s)
{
	ostringstream ss;
	ss << showbase << hex << s;
	return ss.str();
}

h256 fromHexToH256(const string& s)
{
	h256 result;
	bytes b = fromHex(s);
	memcpy(result.data(), b.data(), std::min<size_t>(b.size(), (size_t)result.size));
	return result;
}

h256 fromHexToH256LittleEndian(const string& s)
{
	h256 result;
	bytes b = fromHex(s);
	assert(result.size >= b.size());
	memcpy(result.data() + result.size - b.size(), b.data(), b.size());
	return result;
}

BSONObj getBSONContract(const Client& c, const Address& address)
{
	BSONObjBuilder contract;

	contract.append("_id", toString(address));
	u256 contract_balance = c.state().balance(address);
	contract.append("balance", toString(contract_balance));

	BSONObjBuilder contract_code;
	BSONObjBuilder contract_memory;
	auto mem = c.state().contractMemory(address);

	unsigned numerics = 0;
	bool unexpectedNumeric = false;
	u256 next = 0;

	for (auto i : mem)
	{
		//contract_memory.append(toString(i.first), toString(i.second));

		if (next < i.first)
		{
			unsigned j;
			for (j = 0; j <= numerics && next + j < i.first; ++j) {
				//s_out << (j < numerics || unexpectedNumeric ? " 0" : " STOP");
				//contract_code.append(toString(toHex(i.first)), (j < numerics || unexpectedNumeric ? " 0" : " STOP"));
				if (!unexpectedNumeric) {
					cout << "[c] " << toHexStr(next + j) << " - " << (j < numerics ? "0x0" : "STOP") << endl;
					contract_code.append(toHexStr(next + j), (j < numerics ? "0x0" : "STOP"));
				}
			}
			unexpectedNumeric = false;
			numerics -= min(numerics, j);
			if (next + j < i.first) {
				//s_out << " ...\n@" << showbase << hex << i.first << showbase << dec << "\t";
			}
		}
		else if (!next)
		{
			//s_out << "@" << showbase << hex << i.first << showbase << dec << "\t";
		}

		auto iit = c_instructionInfo.find((Instruction)(unsigned)i.second);
		if (numerics || iit == c_instructionInfo.end() || (u256)(unsigned)iit->first != i.second)	// not an instruction or expecting an argument...
		{
			if (numerics) {
				numerics--;
				cout << "[c] " << toHexStr(i.first) << " - " << toHexStr(i.second) << endl;
				contract_code.append(toHexStr(i.first), toHexStr(i.second));
			}
			else {
				unexpectedNumeric = true;
				cout << "[m] " << toHexStr(i.first) << " - " << toHexStr(i.second) << endl;
				contract_memory.append(toHexStr(i.first), toHexStr(i.second));
			}
		}
		else // print commands
		{
			InstructionInfo const& ii = iit->second;
			//s_out << " " << ii.name;
			cout << "[c] " << toHexStr(i.first) << " - " << ii.name << endl;
			contract_code.append(toHexStr(i.first), ii.name);
			numerics = ii.additional; // command has some parameters
		}
		next = i.first + 1;
	}

	contract.append("memory", contract_memory.obj());
	contract.append("code", contract_code.obj());
	return contract.obj();
}


void exportPending(mongo::DBClientConnection& db, Client& c)
{
	db.dropCollection("webeth.pending");

	for (Transaction const& tx : c.pending())
	{
		BSONObjBuilder transaction;
		transaction.append("sender", toString(tx.safeSender()));
		transaction.append("receiver", toString(tx.receiveAddress));
		transaction.append("value", toString(tx.value));
		transaction.append("nonce", toString(tx.nonce));
		transaction.append("iscontract", c.state().isContractAddress(tx.receiveAddress));
		db.insert("webeth.pending", transaction.obj());
	}
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

		BSONArrayBuilder transaction_array;

		for (auto const& i : block[1])
		{
			Transaction tx(i.data());

			// a new contract
			if (!tx.receiveAddress) {

				Address address = right160(tx.sha3());
				BSONObj contract = getBSONContract(c, address);
				db.insert("webeth.contracts", contract);

				// check if there is code for the contract
				db.update("webeth.code", BSON("address" << toString(address)),
					BSON("$set" << BSON("status" << "ready")));
			}
			else 
			{
				if (c.state().isContractAddress(tx.receiveAddress))
				{
					BSONObj contract = getBSONContract(c, tx.receiveAddress);
					db.update("webeth.contracts",
						BSON("_id" << contract["_id"]), 
						BSON("$set" << BSON("balance" << contract["balance"] << "memory" << contract["memory"] << "code" << contract["code"]))
						);
				}


				BSONObjBuilder transaction;
				transaction.genOID();
				transaction.append("sender", toString(tx.safeSender()));
				transaction.append("receiver", toString(tx.receiveAddress));
				transaction.append("value", toString(tx.value));
				transaction.append("nonce", toString(tx.nonce));
				transaction.append("block", toString(h));
				transaction.appendTimestamp("timestamp", (unsigned long long)info.timestamp);
				
				BSONObj txo = transaction.obj();
				transaction_array.append(txo["_id"]);
				db.insert("webeth.transactions", txo);

				u256 receiver_balance = c.state().balance(tx.receiveAddress);
				if (db.count("webeth.address", BSON("_id" << toString(tx.receiveAddress))) > 0) {
					db.update("webeth.address",
						BSON("_id" << toString(tx.receiveAddress)),
						BSON("$set" << BSON("balance" << toString(receiver_balance)))
						);
				}
				else
				{
					BSONObjBuilder address_receiver;
					address_receiver.append("_id", toString(tx.receiveAddress));
					address_receiver.append("balance", toString(receiver_balance));
					db.insert("webeth.address", address_receiver.obj());
				}

			}

			u256 sender_balance = c.state().balance(tx.safeSender());
			if (db.count("webeth.address", BSON("_id" << toString(tx.safeSender()))) > 0) {
				db.update("webeth.address",
					BSON("_id" << toString(tx.safeSender())),
					BSON("$set" << BSON("balance" << toString(sender_balance)))
					);
			}
			else
			{
				BSONObjBuilder address_sender;
				address_sender.append("_id", toString(tx.safeSender()));
				address_sender.append("balance", toString(sender_balance));
				db.insert("webeth.address", address_sender.obj());
			}
		}


		BSONObjBuilder b;
		b.append("_id", toString(h));
		b.append("number", (long long)d.number); // this is actualy unigned long long
		//b.appendNumber("number", (long long)d.number);
		b.appendTimestamp("timestamp", (unsigned long long)info.timestamp);
		b.appendArray("transactions", transaction_array.obj());
		BSONObj p = b.obj();
		db.insert("webeth.blocks", p);
	}


	lastBlock = bc.currentHash();
}


void executeTransaction(Client& c, Transaction const& t)
{
	/*
	Secret secret = h256(fromHex(sechex));
	Address dest = h160(fromHex(rechex));
	c.transact(secret, dest, amount);
	*/
}

void executeRequest(Client& c, const Secret& secret, const string& contractAddr, const string& name, const string& value)
{
	// the amount to pay for the transaction
	const u256 amount = 10000000000000000;

	Address dest = h160(contractAddr, h160::FromHex);

	u256s txdata;
	txdata.push_back(fromHexToH256(name));
	txdata.push_back(fromHexToH256(value));
	cout << txdata << endl;

	c.transact(secret, dest, amount, txdata);
}

void executeRequestsMongoDB(mongo::DBClientConnection& db, Client& c, const Secret& secret)
{
	// check if there are any requested transactions
	static const char * requests = "webeth.newnamerequest";
	if(db.count(requests) > 0)
	{
		auto_ptr<DBClientCursor> cursor = db.query(requests, BSONObj());
		while ( cursor->more() ) {
            BSONObj obj = cursor->next();
			
			if (obj.hasField("address") && obj.hasField("name") && obj.hasField("value"))
			{
				string address = obj["address"].str();
				string name = obj["name"].str();
				string value = obj["value"].str();

				executeRequest(c, secret, address, name, value);
				cout << "request: " << obj.toString() << endl;
			}
        }
		db.dropCollection(requests);
	}
}

void executeContractCreateRequestMongoDB(mongo::DBClientConnection& db, Client& c, const Secret& secret)
{
	std::vector<BSONObj> requests;
	db.findN(requests, "webeth.code", BSON("status" << "new"), 10);
	cout << "[webeth.code] found: " << requests.size() << endl;

	for (size_t i = 0; i < requests.size(); i++)
	{
		const BSONObj& obj = requests[i];
		if (obj.hasField("code"))
		{
			string code = obj["code"].str();

			u256s asm_code = compileLisp(code);
			u256 amount = 10000000000000000;

			Address address = right160(c.transact(secret, Address(), amount, asm_code));
			cout << "contract address: " << address << endl;

			db.update("webeth.code",
				BSON("_id" << obj["_id"]),
				BSON("$set" << BSON("status" << "pending" << "address" << toString(address)))
				);
		}
	}
}

void executeTransactionRequestMongoDB(mongo::DBClientConnection& db, Client& c, const Secret& secret)
{
	// check if there are any requested transactions
	static const char * requests = "webeth.transactionrequest";
	cout << "[webeth.transactionrequest] found: " << db.count(requests) << endl;

	if (db.count(requests) > 0)
	{
		auto_ptr<DBClientCursor> cursor = db.query(requests, BSONObj());
		while (cursor->more()) {
			BSONObj obj = cursor->next();

			if (obj.hasField("receiveAddress") &&
				obj.hasField("fromAddress") &&
				obj.hasField("value") &&
				obj.hasField("data") &&
				obj.hasField("vrs"))
			{
				string receiveAddress = obj["receiveAddress"].str();
				string fromAddress = obj["fromAddress"].str();
				string value = obj["value"].str();
				vector<BSONElement> data = obj["data"].Array();
				BSONObj vrs = obj["vrs"].Obj();

				if (value.size() % 2 != 0)
				{
					cout << "[webeth.transactionrequest] invalide value: uneven number of chars." << endl;
					continue;
				}

				Transaction t;
				t.receiveAddress = Address(fromHex(receiveAddress));
				t.value = u256(fromHexToH256LittleEndian(value));

				for (auto i : data)
				{
					t.data.push_back(u256(fromHexToH256(i.str())));
				}

				t.vrs.v = fromHex(vrs["v"].str())[0];
				t.vrs.r = u256(fromHexToH256(vrs["r"].str()));
				t.vrs.s = u256(fromHexToH256(vrs["s"].str()));

				Address from = Address(fromHex(fromAddress));

				c.transact(t, from);
			}
			else
			{
				cout << "[webeth.transactionrequest] invalide transaction: some fields are mising." << endl;
			}
		}
		db.dropCollection(requests);
	}
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

	string configFile = getDataDir() + "/moent_config.rlp";
	bytes b = contents(configFile);
	cout << "read config: " << configFile << endl;

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
			coinbase = h160(fromHex(argv[++i]));
		else if ((arg == "-s" || arg == "--secret") && i + 1 < argc)
			us = KeyPair(h256(fromHex(argv[++i])));
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

	cout << "Address: " << endl << toHex(us.address().asArray()) << endl;
	c.startNetwork(listenPort, remoteHost, remotePort, mode, peers, publicIP, upnp);
	eth::uint n = c.blockChain().details().number;


	// connect to db
	mongo::DBClientConnection db;
	db.connect("localhost");


	// get the latest block from the db
	BSONObj lastBlock = db.findOne("webeth.blocks", Query().sort("number", -1)); // get the block with the highest number
	h256 lastBlockId = c.blockChain().genesisHash();
	if (lastBlock.hasField("_id")) {
		lastBlockId = h256(fromHex(lastBlock["_id"].str()));
		cout << "last exported block : " << lastBlockId << endl;
	}
	//h256 begin = bc.details(lastId).parent;
	

	while (true)
	{
		executeRequestsMongoDB(db, c, us.secret());
		executeContractCreateRequestMongoDB(db, c, us.secret());
		executeTransactionRequestMongoDB(db, c, us.secret());

		exportPending(db, c);

		if (c.changed())
		{
			// check the database every 1s
			updateMongoDB(db, c, lastBlockId);
			cout << "step" << endl;
		}
		this_thread::sleep_for(chrono::milliseconds(1000));
	}

	return 0;
}
