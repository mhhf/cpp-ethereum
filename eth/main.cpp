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
#include "Defaults.h"
#include "Client.h"
#include "PeerNetwork.h"
#include "BlockChain.h"
#include "State.h"
#include "FileSystem.h"
#include "Instruction.h"
#include "RLP.h"
using namespace std;
using namespace eth;

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
	else if (cmd == "minestart")
	{
		c.startMining();
	}
	else if (cmd == "minestop")
	{
		c.stopMining();
	}
	else if (cmd == "address")
	{
		s_out << endl;
		s_out << "Current address: " + asHex(us.address().asArray()) << endl;
		s_out << "===" << endl;
	}
	else if (cmd == "secret")
	{
		s_out << endl;
		s_out << "Current secret: " + asHex(us.secret().asArray()) << endl;
		s_out << "===" << endl;
	}
	else if (cmd == "balance")
	{
		u256 balance = c.state().balance(us.address());
		string value = formatBalance(balance);
		s_out << endl;
		s_out << "Current balance: ";
		s_out << value << endl;
		s_out << "===" << endl;
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
		s_out << "===" << endl;
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
					s_out << " ...\n@" << showbase << hex << i.first << "    ";
				}
			}
			else if (!next)
			{
				s_out << "@" << showbase << hex << i.first << "    ";
			}
			auto iit = c_instructionInfo.find((Instruction)(unsigned)i.second);
			if (numerics || iit == c_instructionInfo.end() || (u256)(unsigned)iit->first != i.second)	// not an instruction or expecting an argument...
			{
				if (numerics)
					numerics--;
				else
					unexpectedNumeric = true;
				s_out << " " << showbase << hex << i.second;
			}
			else
			{
				auto const& ii = iit->second;
				s_out << " *" << ii.name << "*";
				numerics = ii.additional;
			}
			next = i.first + 1;
		}
		s_out << endl;
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
	else if (cmd == "exit")
	{
		exit(0);
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
	else if (cmd == "contract:send")
	{
		u256 amount = 10000000000000000;
		string contractAddr;
		s_in >> contractAddr;
		Address dest = h160(fromUserHex(contractAddr));

		char buffer[256];
		s_in.getline(buffer, 256);
		string data(buffer);

		u256s txdata;
		txdata.push_back(3);
		txdata.push_back(7);
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
			s_out << d.number << ":\t" << h;
			if (block[1].itemCount() > 0) {
				s_out << " (" << block[1].itemCount() << ")";
			}
			s_out << endl;
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
			coinbase = h160(fromUserHex(argv[++i]));
		else if ((arg == "-s" || arg == "--secret") && i + 1 < argc)
			us = KeyPair(h256(fromUserHex(argv[++i])));
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

	Client c("Ethereum(++)/" + clientName + "v" ADD_QUOTES(ETH_VERSION) "/" ADD_QUOTES(ETH_BUILD_TYPE) "/" ADD_QUOTES(ETH_BUILD_PLATFORM), coinbase, dbPath);

	if (interactive)
	{
		cout << "Ethereum (++)" << endl;
		cout << "  Code by Gav Wood, (c) 2013, 2014." << endl;
		cout << "  Based on a design by Vitalik Buterin." << endl << endl;

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
		else // command line
		{
			while (true)
			{
				cout << "> ";
				runCommand(c, us, cin, cout);
			}
		}
	}
	else
	{
		cout << "Address: " << endl << asHex(us.address().asArray()) << endl;
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
