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

#include "../json_spirit/json_spirit_writer_template.h"

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
#include "BuildInfo.h"
using namespace std;
using namespace eth;

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
	cout << "eth version " << ETH_QUOTED(ETH_VERSION) << endl;
	cout << "Build: " << ETH_QUOTED(ETH_BUILD_PLATFORM) << "/" << ETH_QUOTED(ETH_BUILD_TYPE) << endl;
	exit(0);
}


json_spirit::mObject getJSONContract(const Client& c, const Address& address)
{
	json_spirit::mObject contract;

	contract["address"] = toString(address);

	u256 contract_balance = c.state().balance(address);
	contract["balance"] = toString(contract_balance);

	json_spirit::mObject contract_memory;

	auto mem = c.state().contractMemory(address);
	for (auto i : mem)
	{
		contract_memory[toString(i.first)] = toString(i.second);
	}

	contract["memory"] = contract_memory;
	return contract;
}


json_spirit::mObject getJSONTransaction(const Transaction& t)
{
	json_spirit::mObject o;
	if (t.receiveAddress) {
		o["receiveAddress"] = toString(t.receiveAddress);
	} else {
		o["sha3"] = toString(right160(t.sha3()));
	}
	o["safeSender"] = toString(t.safeSender());
	o["value"] = toString(t.value);
	o["nonce"] = toString(t.nonce);

	//bool isContract = c.state().isContractAddress(t.receiveAddress);
	//o["contract"] = isContract ? "true" : "false";
	return o;
}

void getJSONBlockInfo()
{

}


void getJSONState(Client& c, std::ostream& s_out)
{
	json_spirit::mObject result;


	// block:list
	json_spirit::mArray blocks;
	json_spirit::mArray contractArray;
	int block_count = 0;
	auto const& bc = c.blockChain();
	for (auto h = bc.currentHash(); h != bc.genesisHash(); h = bc.details(h).parent)
	{
		auto d = bc.details(h);
		auto blockData = bc.block(h);
		auto block = RLP(blockData);
		BlockInfo info(blockData);

		// it's a boring block, procede ...
		if (block[1].itemCount() == 0 && (block_count++) > 7) {
			continue;
		}

		json_spirit::mObject b;
		b["hash"] = toString(h);
		b["number"] = toString(d.number);
		b["timestamp"] = toString(info.timestamp);
		
		json_spirit::mArray txArray;
		// block transactions
		for (auto const& i : block[1])
		{
			Transaction tx(i.data());
			txArray.push_back(getJSONTransaction(tx));

			if (!tx.receiveAddress) {

				Address address = right160(tx.sha3());
				contractArray.push_back(json_spirit::mValue(getJSONContract(c, address)));
			}
		}
		b["transactions"] = txArray;
		blocks.push_back(b);
	}

	result["blocks"] = blocks;
	result["contracts"] = contractArray;
	
	json_spirit::mArray txArrayPending;
	for (Transaction const& t : c.pending())
	{
		txArrayPending.push_back(getJSONTransaction(t));
	}
	result["pending"] = txArrayPending;


	json_spirit::write_stream(json_spirit::mValue(result), s_out, true);
}


void runCommand(Client& c, KeyPair& us, std::istream& s_in, std::ostream& s_out)
{
	//cout << "> " << flush;
	std::string cmd;
	s_in >> cmd;
	if (cmd == "netstart")
	{
		eth::uint port;
		s_in >> port;
		c.startNetwork((short)port);
	}
	else if (cmd == "connect")
	{
		string addr;
		eth::uint port;
		s_in >> addr >> port;
		c.connect(addr, (short)port);
	}
	else if (cmd == "netstop")
	{
		c.stopNetwork();
	}
	else if (cmd == "mine:start")
	{
		c.startMining();
	}
	else if (cmd == "mine:stop")
	{
		c.stopMining();
	}
	else if (cmd == "address")
	{
		s_out << endl;
		s_out << "Current address: " + asHex(us.address().asArray()) << endl;
	}
	else if (cmd == "compile")
	{
    
		string filename;
		s_in >> filename;
    
    std::ifstream ifs("contracts/"+filename);
    std::string content( (std::istreambuf_iterator<char>(ifs) ),
        (std::istreambuf_iterator<char>()    ) ); 
    
    u256s code = compileLisp(content);

		s_out << "LISP: "<< content << "\n\nES CODE: " << disassemble(code) << endl;
	}
	else if (cmd == "contract:from:buffer")
	{
    
		char buffer[256];
		s_in.getline(buffer, 256);
		string data(buffer);
    
		// string filename;
		// s_in >> filename;
    
    // std::ifstream ifs("contracts/"+filename);
    // std::string content( (std::istreambuf_iterator<char>(ifs) ),
    //     (std::istreambuf_iterator<char>()    ) ); 
    
    u256s code = compileLisp(data);

    
		u256 amount = 1000000000000000000;
		// s_in >> amount;

		c.transact(us.secret(), Address(), amount, code);
    
	}
	else if (cmd == "contract:from:file")
	{
    
		string filename;
		s_in >> filename;
    
    std::ifstream ifs("contracts/"+filename);
    std::string content( (std::istreambuf_iterator<char>(ifs) ),
        (std::istreambuf_iterator<char>()    ) ); 
    
    u256s code = compileLisp(content);

    
		u256 amount = 1000000000000000000;
		// s_in >> amount;

		c.transact(us.secret(), Address(), amount, code);
    
	}
	else if (cmd == "secret")
	{
		s_out << endl;
		s_out << "Current secret: " + asHex(us.secret().asArray()) << endl;
	}
	else if (cmd == "balance")
	{
		u256 balance = c.state().balance(us.address());
		string value = formatBalance(balance);
		s_out << endl;
		s_out << "Current balance: ";
		s_out << value << endl;
	}
	else if (cmd == "pending")
	{
		for (Transaction const& t : c.pending())
		{
			if (t.receiveAddress)
			{
				bool isContract = c.state().isContractAddress(t.receiveAddress);
				s_out << t.safeSender().abridged() << (isContract ? '*' : '-') << "> "
					<< formatBalance(t.value) << " [" << (unsigned)t.nonce << "]" << endl;
			}
			else
			{
				s_out << t.safeSender().abridged() << "+> "
					<< right160(t.sha3()) << ": "
					<< formatBalance(t.value) << " [" << (unsigned)t.nonce << "]" << endl;
			}
		}
	}
	else if (cmd == "balanceof")
	{
		string owner;
		s_in >> owner;
		u256 balance = c.state().balance(h160(fromUserHex(owner)));
		s_out << endl;
		s_out << "Current balance: ";
		s_out << formatBalance(balance) << endl;
	}
  else if (cmd == "contract:valueof" ) 
  { // Returns the value of a key from a contract
  }
	else if (cmd == "memory")
	{
		string address;
		s_in >> address;
		auto mem = c.state().contractMemory(h160(fromUserHex(address)));

		unsigned numerics = 0;
		bool unexpectedNumeric = false;
		u256 next = 0;

		for (auto i : mem)
		{
			if (next < i.first)
			{
				unsigned j;
				for (j = 0; j <= numerics && next + j < i.first; ++j) {
					s_out << (j < numerics || unexpectedNumeric ? " 0" : " STOP");
				}
				unexpectedNumeric = false;
				numerics -= min(numerics, j);
				if (next + j < i.first) {
					s_out << " ...\n@" << showbase << hex << i.first << showbase << dec << "\t";
				}
			}
			else if (!next)
			{
				s_out << "@" << showbase << hex << i.first << showbase << dec << "\t";
			}
			
			auto iit = c_instructionInfo.find((Instruction)(unsigned)i.second);
			if (numerics || iit == c_instructionInfo.end() || (u256)(unsigned)iit->first != i.second)	// not an instruction or expecting an argument...
			{
				if (numerics) {
					numerics--;
				} else {
					unexpectedNumeric = true;
				}
				s_out << " " << i.second;
			}
			else // print commands
			{
				InstructionInfo const& ii = iit->second;
				s_out << " " << ii.name;
				numerics = ii.additional; // command has some parameters
			}
			next = i.first + 1;
		}
		s_out << endl;
	}
	else if (cmd == "memory:raw")
	{
		string address;
		s_in >> address;

		auto mem = c.state().contractMemory(h160(fromUserHex(address)));
		for (auto i : mem)
		{
			s_out << i.first << "\t" << i.second << endl;
		}
	}
	else if (cmd == "transact")
	{
		string sechex;
		string rechex;
		u256 amount;
		s_in >> sechex >> rechex >> amount;
		Secret secret = h256(fromUserHex(sechex));
		Address dest = h160(fromUserHex(rechex));
		c.transact(secret, dest, amount);
	}
	else if (cmd == "send")
	{
		string rechex;
		u256 amount;
		s_in >> rechex >> amount;
		Address dest = h160(fromUserHex(rechex));
		c.transact(us.secret(), dest, amount);
	}
	else if (cmd == "peers")
	{
		for (size_t i = 0; i < c.peers().size(); i++) {
			s_out << c.peers()[i].host << ":" << c.peers()[i].port << endl;
		}
	}
	else if (cmd == "fee")
	{
		s_out << formatBalance(c.state().fee()) << endl;
	}
	else if (cmd == "difficulty")
	{
		auto const& bc = c.blockChain();
		auto diff = BlockInfo(bc.block()).difficulty;
		s_out << toLog2(diff) << endl;
	}
	else if (cmd == "contract:list")
	{
		auto const& bc = c.blockChain();
		for (auto h = bc.currentHash(); h != bc.genesisHash(); h = bc.details(h).parent)
		{
			auto d = bc.details(h);
			// print the block info
			auto blockData = bc.block(h);
			auto block = RLP(blockData);
			BlockInfo info(blockData);

			// block transactions
			for (auto const& i : block[1])
			{
				Transaction t(i.data());
				
				if (!t.receiveAddress)
				{
					s_out << "  " << t.safeSender() << "  "
						<< right160(t.sha3()) << "  "
						<< formatBalance(t.value) << "  " << (unsigned)t.nonce << endl;
				}
			}
		}
	}
	else if (cmd == "address:new")
	{
		us = KeyPair::create();
		s_out << asHex(us.address().asArray()) << endl;
	}
	else if (cmd == "contract:create")
	{
		u256 amount = 1000000000000000000;
		// s_in >> amount;

		char buffer[256];
		s_in.getline(buffer, 256);
		string data(buffer);

		u256s contract = eth::assemble(data, false);
		cout << "sent: " << amount << " : " << data << endl;

		c.transact(us.secret(), Address(), amount, contract);
	}
	else if (cmd == "write:cmi")
	{
		u256 amount = 10000000000000000;
    // TODO: hard code contract adress
		string contractAddr;
		s_in >> contractAddr;
		Address dest = h160(fromUserHex(contractAddr));

		//char buffer[256];
		//s_in.getline(buffer, 256);
		//string data(buffer);

		string c_name;
		string c_git1;
		string c_git2;
		string c_address;
		s_in >> c_name  >> c_address >> c_git1 >> c_git2;
    
    bytes c_name_buffer(32); 
    bytes c_git1_buffer(32); 
    bytes c_git2_buffer(32); 
    bytes c_address_buffer(32); 
    
    auto c_address_hash = fromUserHex(c_address);
    
    std::copy(c_address_hash.begin(), c_address_hash.end(), c_address_buffer.begin());
    std::copy(c_name.begin(), c_name.end(), c_name_buffer.begin());
    std::copy(c_git1.begin(), c_git1.end(), c_git1_buffer.begin());
    std::copy(c_git2.begin(), c_git2.end(), c_git2_buffer.begin());
    
		u256s txdata;
    
		txdata.push_back(h256(c_name_buffer));
		txdata.push_back(h256(c_address_buffer));
		txdata.push_back(h256(c_git1_buffer));
		txdata.push_back(h256(c_git2_buffer));
    
		s_out << "sent: " << amount << " to " << contractAddr << " : " << txdata << endl;

		c.transact(us.secret(), dest, amount, txdata);
	}
	else if (cmd == "contract:send")
	{
		u256 amount = 10000000000000000;
		string contractAddr;
		s_in >> contractAddr;
		Address dest = h160(fromUserHex(contractAddr));

		//char buffer[256];
		//s_in.getline(buffer, 256);
		//string data(buffer);

		string a;
		string b;
		string _c;
		s_in >> a >> b >> _c;
    
    bytes a_buffer(32); 
    bytes b_buffer(32); 
    bytes c_buffer(32); 
    
    auto as = fromUserHex(a);
    
    std::copy(as.begin(), as.end(), a_buffer.begin());
    std::copy(b.begin(), b.end(), b_buffer.begin());
    std::copy(_c.begin(), _c.end(), c_buffer.begin());
    
    // u256 ua = h256(fromUserHex(a));
		u256 ua = h256(a_buffer);
		u256 ub = h256(b_buffer);
		u256 uc = h256(c_buffer);
    
    
		u256s txdata;
		txdata.push_back(100);
		txdata.push_back(ub);
		txdata.push_back(uc);
		s_out << "sent: " << amount << " to " << contractAddr << " : " << txdata << endl;

		c.transact(us.secret(), dest, amount, txdata);
	}
	else if (cmd == "block:list")
	{
		auto const& bc = c.blockChain();
		for (auto h = bc.currentHash(); h != bc.genesisHash(); h = bc.details(h).parent)
		{
			auto d = bc.details(h);
			auto blockData = bc.block(h);
			auto block = RLP(blockData);
			if (block[1].itemCount() > 0) {
        s_out << d.number << ":\t" << h;
				s_out << " (" << block[1].itemCount() << ")";
			s_out << endl;
			}
		}
	}
	else if (cmd == "block:info")
	{
		int blocknr;
		s_in >> blocknr;

		auto const& bc = c.blockChain();
		for (auto h = bc.currentHash(); h != bc.genesisHash(); h = bc.details(h).parent)
		{
			auto d = bc.details(h);
			if (d.number == blocknr) {
				s_out << d.number << ":\t" << h << endl;

				// print the block info
				auto blockData = bc.block(h);
				auto block = RLP(blockData);
				BlockInfo info(blockData);

				s_out << "Timestamp: " << info.timestamp << endl;
				s_out << "Transactions: " << block[1].itemCount() << endl;

				// block transactions
				for (auto const& i : block[1])
				{
					Transaction t(i.data());
					if (t.receiveAddress)
					{
						bool isContract = c.state().isContractAddress(t.receiveAddress);
						s_out << t.safeSender().abridged() << (isContract ? '*' : '-') << "> "
							<< formatBalance(t.value) << " [" << (unsigned)t.nonce << "]" << endl;
					}
					else
					{
						s_out << t.safeSender().abridged() << "+> "
							<< right160(t.sha3()) << ": "
							<< formatBalance(t.value) << " [" << (unsigned)t.nonce << "]" << endl;
					}
				}

				break;
			}
		}
	}
	else if (cmd == "json:getstate")
	{
		getJSONState(c, s_out);
	}
	else if (cmd == "exit")
	{
		exit(0);
	}
	else if (cmd == "contract:view")
	{
		string address;
		s_in >> address;
		auto mem = c.state().contractMemory(h160(fromUserHex(address)));

//		unsigned numerics = 0;
		bool memory = false;
		u256 next = 0;

    s_out << "[\n";
		for (auto i : mem)
		{
      // todo - how to check if contract code has ended?
      if ( i.first > 1000 ) {
        if (next < i.first) // is new in the row 
        {
          if(memory) s_out << "\"},\n";
          else memory = true;
          s_out << "{\"" << noshowbase << hex << i.first << "\":\"" << noshowbase << i.second;
        } else { // is successor
          s_out << i.second ;
        } 
      }
			next = i.first + 1;
		}
    s_out << "}\n]\n";
		s_out << endl;
	}
	else
	{
		s_out << "unknown command: " << cmd << endl;
	}
}

int main(int argc, char** argv)
{
	unsigned short listenPort = 30303;
	string remoteHost;
	unsigned short remotePort = 30303;
	bool interactive = false;
	bool network_interactive = false;
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
			coinbase = h160(fromHex(argv[++i]));
		else if ((arg == "-s" || arg == "--secret") && i + 1 < argc)
			us = KeyPair(h256(fromHex(argv[++i])));
		else if (arg == "-i" || arg == "--interactive")
			interactive = true;
		else if ((arg == "-d" || arg == "--path" || arg == "--db-path") && i + 1 < argc)
			dbPath = argv[++i];
		else if ((arg == "-m" || arg == "--mining") && i + 1 < argc)
		{
			string m = argv[++i];
			if (isTrue(m))
				mining = ~(eth::uint)0;
			else if (isFalse(m))
				mining = 0;
			else if (int i = stoi(m))
				mining = i;
			else
			{
				cerr << "Unknown mining option: " << m << endl;
				return -1;
			}
		}
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
		else if (arg == "--network_interactive")
			network_interactive = true;
		else
			remoteHost = argv[i];
	}

	if (!clientName.empty()) {
		clientName += "/";
	}

	Client c("Ethereum(++)/" + clientName + "v" ETH_QUOTED(ETH_VERSION) "/" ETH_QUOTED(ETH_BUILD_TYPE) "/" ETH_QUOTED(ETH_BUILD_PLATFORM), coinbase, dbPath);

	if (network_interactive)
	{
		boost::asio::io_service ios;
		boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::tcp::v4(), 30000);
		boost::asio::ip::tcp::acceptor acceptor(ios, endpoint);

		while (true)
		{
			cout << "waiting for connection" << endl;
			boost::asio::ip::tcp::iostream stream;
			acceptor.accept(*stream.rdbuf());
			cout << "waiting for command" << endl;

			while (stream.good())
			{
				runCommand(c, us, stream, stream);
			}
		}
	}
	else if (interactive) // command line
	{
		cout << "Ethereum (++)" << endl;
		cout << "  Code by Gav Wood, (c) 2013, 2014." << endl;
		cout << "  Based on a design by Vitalik Buterin." << endl << endl;

		while (true)
		{
			cout << "> ";
			runCommand(c, us, cin, cout);
		}
	}
	else
	{
		cout << "Address: " << endl << toHex(us.address().asArray()) << endl;
		c.startNetwork(listenPort, remoteHost, remotePort, mode, peers, publicIP, upnp);
		eth::uint n = c.blockChain().details().number;
		if (mining)
			c.startMining();
		while (true)
		{
			if (c.blockChain().details().number - n == mining)
				c.stopMining();
			this_thread::sleep_for(chrono::milliseconds(100));
		}
	}


	return 0;
}
