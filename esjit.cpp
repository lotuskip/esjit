// Please see README file.
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <string>
#include <vector>
#include <ctime>
#include <cerrno>

#include <boost/graph/adjacency_list.hpp>
#include <boost/lexical_cast.hpp>

#include <jack/jack.h>
#include <jack/statistics.h>

using namespace std;
using namespace boost;

const char* VERSION = "3";

// The commands part of the help view:
const char NUM_KEYS = 10;
const char* helps[NUM_KEYS][2] = {
{ "Q", "quit" },
{ "r", "refresh connections" },
{ "c N M", "connect port N to port M" },
{ "d N M", "disconnect ports N and M" },
{ "D", "destroy all connections" },
{ "C <f>", "store connection setup to file <f>" },
{ "R <f>", "restore connection setup from file <f>" },
{ "s", "show server info / statistics" },
{ "i", "print detailed port info" },
{ "x", "reset the max delay counter" }
};

const char* info_str[5] = { "Port ID & name ", "aliases ", "ltncy-rng ", "flgs ", "type" };

// Colour changing strings:
const char DEFCOL[] = "\033[0m";
const char COL[] = "\033[0m\033[40m\033[3";
const char RED[] = "1m";
const char GREEN[] = "2m";
const char BLUE[] = "4m";
const char MAG[] = "5m";
const char CYAN[] = "6m";

jack_client_t *client;
char* aliases[2];

// We represent the ports and their connections by a graph, where the
// vertex data is a pointer to the JACK internal port structure.
typedef adjacency_list<setS, vecS, undirectedS, jack_port_t*> PortGraph;
typedef PortGraph::vertex_descriptor vtex;
typedef PortGraph::vertex_iterator vtex_it;
typedef PortGraph::adjacency_iterator adj_it;

PortGraph portgraph;


// Get the vertex of the given jack port in the graph:
vtex get_vtex(const jack_port_t* ref)
{
	for(pair<vtex_it, vtex_it> v = vertices(portgraph); v.first != v.second; ++v.first)
	{
		if(portgraph[*v.first] == ref)
			return *v.first;
	}
	// ending up here is an error!
	throw 1;
}

// Get the Nth port:
jack_port_t* port_by_index(unsigned short N)
{
	if(N >= num_vertices(portgraph))
		return NULL;
	vtex_it vti = vertices(portgraph).first;
	for(; N > 0; --N)
		++vti;
	return portgraph[*vti];
}

// Get 'N' for the given port:
short index_by_port(const jack_port_t* ref)
{
	short n = 0;
	for(pair<vtex_it, vtex_it> v = vertices(portgraph); v.first != v.second; ++v.first)
	{
		if(portgraph[*v.first] == ref)
			return n;
		++n;
	}
	return -1; // not found
}


// Read the ports and the connections from the JACK server and construct the graph.
void refresh_list()
{
	portgraph.clear();
	vtex_it vti;

	jack_port_t *port, *port2;
	const char **ports = jack_get_ports(client, NULL, NULL, 0);
	// We loop the ports twice; first forming the vertices of the graph (the ports),
	// next the edges (the connections). This ensures the ordering of the ports
	// remains consistent throughout.
	int i;
	for(i = 0; ports[i]; ++i)
	{
		if((port = jack_port_by_name(client, ports[i])))
			portgraph[add_vertex(portgraph)] = port;
	}
	const char **connections;
	int j;
	for(i = 0; ports[i]; ++i)
	{
		if((port = jack_port_by_name(client, ports[i]))
			&& (connections = jack_port_get_all_connections(client, port)))
		{
			for(j = 0; connections[j]; ++j)
			{
				if((port2 = jack_port_by_name(client, connections[j])))
				{
					try { add_edge(get_vtex(port), get_vtex(port2), portgraph); }
					catch(int e)
					{
						cerr << "Warning! Error creating port graph!" << endl;
						return; /* Give up midway. Ending up here is, I think,
						theoretically possible, if a new port is created and
						connected to an existing port _after_ we've looped the
						ports the first time... */
					}
				}
			}
			jack_free(connections);
		}
	}
	jack_free(ports);
}


// Construct a list of port indices to which the given vertex is connected.
// Returns either "" or something like "0,2,4,5".
string constr_list_of_conns(const vtex vd)
{
	string ret = "";
	for(pair<adj_it, adj_it> av = adjacent_vertices(vd, portgraph);
		av.first != av.second; ++av.first)
		ret += lexical_cast<string>(index_by_port(portgraph[*av.first])) + ',';
	if(!ret.empty())
		ret.erase(ret.size()-1); // remove last ','
	return ret;
}

// Print the connections. That's what you see first when you run esjit.
void print_connections()
{
	refresh_list();

	vector<string> columns[3];
	unsigned short longests[2] = { 0, 0 };
	
	// Collect what to print:
	char ch;
	int flags;
	unsigned short n = 0;
	string tmpstr;
	for(pair<vtex_it,vtex_it> v = vertices(portgraph); v.first != v.second; ++v.first)
	{
		tmpstr = constr_list_of_conns(*v.first);
		flags = jack_port_flags(portgraph[*v.first]);
		if(flags & JackPortIsInput) // playback port
		{
			if(!tmpstr.empty())
				tmpstr += "--> ";
			columns[0].push_back(tmpstr);
			columns[1].push_back(string(COL) + GREEN);
			columns[2].push_back("");
		}
		else
		{
			columns[0].push_back("");
			columns[1].push_back(string(COL) + RED);
			if(!tmpstr.empty())
				tmpstr.insert(0, " -->");
			columns[2].push_back(tmpstr);
		}
		columns[1].back() += '[' + lexical_cast<string>(n) + "] "
			+ jack_port_name(portgraph[*v.first]) + DEFCOL;
		for(ch = 0; ch < 2; ++ch)
		{
			if(columns[ch].back().size() > longests[ch])
				longests[ch] = columns[ch].back().size();
		}
		++n;
	}
	
	// Print it nicely:
 	cout << "\033[2J\033[H"; // clear screen
	for(n = 0; n < columns[0].size(); ++n)
	{
		cout << right << setw(longests[0]) << columns[0][n];
		cout << left << setw(longests[1]) << columns[1][n];
		cout << columns[2][n] << endl;
	}
}


// Print the detailed info on the ports.
void print_details()
{
	refresh_list();

	// Collect what to print:
	vector<bool> redstart;
	vector<string> columns[5];
	unsigned short longests[4] = { 15, 8, 10, 5 }; // lengths of the info_str strings
	int flags;
	char ch;
	short cnt;
	unsigned short n = 0;
	jack_latency_range_t range;
	for(pair<vtex_it,vtex_it> v = vertices(portgraph); v.first != v.second; ++v.first)
	{
		flags = jack_port_flags(portgraph[*v.first]);
		if(flags & JackPortIsInput) // playback port
			redstart.push_back(false);
		else
			redstart.push_back(true);
		columns[0].push_back('[' + lexical_cast<string>(n) + "] "
			+ jack_port_name(portgraph[*v.first]) + ' ');

		if((cnt = jack_port_get_aliases(portgraph[*v.first], aliases)) > 0)
		{
			columns[1].push_back("");
			for(; cnt > 0; --cnt)
			{
				columns[1].back() += aliases[cnt-1];
				if(cnt > 1)
					columns[1].back() += ", ";
			}
		}
		else columns[1].push_back("--");

		jack_port_get_latency_range(portgraph[*v.first],
			(flags & JackPortIsInput) ? JackCaptureLatency : JackPlaybackLatency,
			&range);
		columns[2].push_back(lexical_cast<string>(range.min)
			+ '-' + lexical_cast<string>(range.max));

		columns[3].push_back("");
		if(flags & JackPortCanMonitor)
			columns[3].back() += 'm';
		if(flags & JackPortIsPhysical)
			columns[3].back() += 'P';
		if(flags & JackPortIsTerminal)
			columns[3].back() += 't';

		columns[4].push_back(jack_port_type(portgraph[*v.first]));

		for(ch = 0; ch < 4; ++ch)
		{
			if(columns[ch].back().size() > longests[ch])
				longests[ch] = columns[ch].back().size() + 2; // yes, +2 to give some spacing
		}
		++n;
	}

	// Print it nicely:
	for(n = 0; n < 4; ++n)
		cout << setw(longests[n]) << info_str[n];
	cout << info_str[4] << endl;
	for(n = 0; n < columns[0].size(); ++n)
	{
		cout << COL;
		if(redstart[n]) cout << RED;
		else cout << GREEN;
		cout << setw(longests[0]) << columns[0][n] << DEFCOL;
		for(ch = 1; ch < 4; ++ch)
			cout << setw(longests[ch]) << columns[ch][n];
		cout << columns[4][n] << endl;
	}
}


// Connect or disconnect the ports with the given numbers ('disc'==true --> disconnect).
// Returns true if connections were changed and should be reprinted.
bool conn_disconn(const bool disc, const short N, const short M)
{
	jack_port_t *src_port = 0, *dst_port = 0;
	int port1_flags, port2_flags;

	if(!(src_port = port_by_index(N)) || !(dst_port = port_by_index(M)))
	{
		cout << "Invalid numbers!" << endl;
		return false;
	}
	// else ports are valid, at least according to our (possible outdated) view.

	port1_flags = jack_port_flags(src_port);
	port2_flags = jack_port_flags(dst_port);
	// Check if both are in or put:
	if((port1_flags & port2_flags) & (JackPortIsInput | JackPortIsOutput))
	{
		cout << "One port has to be input, the other output!" << endl;
		return false;
	}
	// Determine which is input, which output. The user can give these in either
	// order, but the jack API requires them source-first.
	if(port1_flags & JackPortIsInput) // implies port2 is output
	{
		// need to switch:
		jack_port_t *tmp_port = src_port;
		src_port = dst_port;
		dst_port = tmp_port;
	}

	if(disc) // requested to disconnect, not connect
	{
		if(jack_disconnect(client, jack_port_name(src_port), jack_port_name(dst_port)))
		{
			cout << "Disconnect failed!" << endl;
			return false;
		}
	}
	else if(jack_connect(client, jack_port_name(src_port), jack_port_name(dst_port)))
	{
		cout << "Connect failed!" << endl;
		return false;
	}
	return true; // Success.
}


int main(int argc, char* argv[])
{
	// Connect to JACK server [cmdline argument, if any, giving the name];
	// don't start one if none is found running:
	jack_status_t status;
    jack_options_t options = JackNoStartServer;
	if(argc > 1)
	{
		if(string(argv[1]) == "-h" || string(argv[1]) == "-v")
		{
			cout << "esjit version " << VERSION << endl
				<< "Usage: " << argv[0] << " [server name, optional]" << endl;
			return 0;
		}
		// Else argv[1] is server name?
		client = jack_client_open("esjit", options, &status, argv[1]);
	}
	else
		client = jack_client_open("esjit", options, &status, NULL);
	if(!client)
	{
		if(status & JackServerFailed)
			cout << "JACK server not running." << endl;
		else
			cout << "jack_client_open() failed, status = " << status << endl;
		return 1;
	}

	aliases[0] = new char[jack_port_name_size()];
	aliases[1] = new char[jack_port_name_size()];

	print_connections();
	cout << "(enter \'h\' for help)" << endl;

	string entry = "";
	short N, M;
	for(;;)
	{
		cout << "> " << flush;
		if(!(cin >> entry))
			break;
		if(entry == "Q") // 'Q'uit
			break;
		if(entry == "h") // 'h'elp
		{
			cout << "esjit version " << VERSION << endl;
			cout << "For full info, read the README file or the man page, "
				"available at least at http://github.com/lotuskip/esjit" << endl;
			cout << "Port colours: ";
			cout << COL << GREEN << "playback" << DEFCOL << ", " << COL << RED
				<< "capture" << DEFCOL << endl;
			cout << "Commands:" << endl;
			for(N = 0; N < NUM_KEYS; ++N)
			{
				cout << COL << CYAN << helps[N][0] << '\t'
					<< DEFCOL << helps[N][1] << endl;
			}
			cout << "In the detailed view, the following abbreviations are used for port flags:";
			cout << endl << COL << BLUE << 'P'
				<< DEFCOL << ": corresponds to a physical I/O connector";
			cout << endl << COL << BLUE << 'm' << DEFCOL << ": can be monitored";
			cout << endl << COL << BLUE << 't' << DEFCOL << ": is a terminal port" << endl;
		}
		else if(entry == "r") // 'r'efresh
			print_connections();
		else if(entry == "i") // 'i'nfo
			print_details();
		else if(entry == "c" || entry == "d") // 'c'onnect or 'd'isconnect
		{
			cin >> N;
			cin >> M;
			if(conn_disconn(entry == "d", N, M))
				print_connections();
		}
		else if(entry == "D") // 'D'isconnect all
		{
			const char **connections,
				**ports = jack_get_ports(client, NULL, NULL, 0);
			for(N = 0; ports[N]; ++N)
			{
				if((connections = jack_port_get_all_connections(client,
					jack_port_by_name(client, ports[N]))))
				{
					for(M = 0; connections[M]; ++M)
						jack_disconnect(client, ports[N], connections[M]);
					jack_free(connections);
				}
			}
			jack_free(ports);
			print_connections();
		}
		else if(entry == "C") // save 'C'onnection setup to file
		{
			cin >> entry; // filename
			ofstream ofile(entry.c_str());
			if(!ofile)
				cout << "Could not open file \'" << entry << "\' for writing!" << endl;
			else
			{
				time_t rawtime;
				struct tm *timeinfo;
				time(&rawtime);
				timeinfo = localtime(&rawtime);
				ofile << "#esjit generated connection setup file, " << asctime(timeinfo);
				// Just list what ports the output ports are connected to (the connections
				// are not directed).
  				const char **jack_oports = jack_get_ports(client, 0, 0, JackPortIsOutput);
				for(N = 0; jack_oports[N]; ++N)
				{
      				const char **connections = jack_port_get_all_connections(client,
				      jack_port_by_name(client, jack_oports[N]));
					if(connections)
					{
						ofile << jack_oports[N] << endl;
						// Print ports connected to this port preceded by a tab:
						for(M = 0; connections[M]; ++M)
							ofile << '\t' << connections[M] << endl;
						jack_free(connections);
					}
      			}
				ofile.close();
				jack_free(jack_oports);
				cout << "Saved connection setup to \'" << entry << "\'." << endl;
			}
		}
		else if(entry == "R") // 'R'etrieve connection setup from file
		{
			cin >> entry; // filename
			ifstream ifile(entry.c_str());
			if(!ifile)
				cout << "Could not read file \'" << entry << "\'!" << endl;
			else
			{
				string s, outname = "";
				while(ifile)
				{
					getline(ifile, s);
					if(s.empty() || s[0] == '#') // comments
						continue;
					if(s[0] != '\t') // tab indicates connected port
						outname = s;
					else // connect ports "s" and "outname"
					{
						s.erase(0,1); // remove the tab
						N = jack_connect(client, outname.c_str(), s.c_str());
						if(N && N != EEXIST) // "connection already exists" is not an error here
							cout << "Could not connect \'" << s
								<< "\' to \'" << outname << "\'." << endl;
					}
				}
				print_connections();
			}
		}
		else if(entry == "s") // 's'erver 's'tatistics
		{
			cout << COL << BLUE << "**JACK server info**" << DEFCOL << endl;
			cout << "Running realtime: ";
			if(jack_is_realtime(client)) cout << COL << BLUE << "yes";
			else cout << COL << MAG << "no";
			cout << DEFCOL << endl << "Samplerate: "
				<< COL << CYAN << jack_get_sample_rate(client) << " Hz";
			cout << DEFCOL << endl << "Buffer size: "
				<< COL << CYAN << jack_get_buffer_size(client) << " b";
			cout << DEFCOL << endl << "CPU load: "
				<< COL << CYAN << jack_cpu_load(client);
			cout << DEFCOL << endl << "Max delay: "
				<< COL << CYAN << jack_get_max_delayed_usecs(client)
				<< " microseconds" << DEFCOL << endl;
		}
		else if(entry == "x") // reset ma'x' delay
		{
			jack_reset_max_delayed_usecs(client);
			cout << "Max delay counter reset." << endl;
		}
		else
			cout << "Unknown key \'" << entry << "\'! Enter \'h\' for help." << endl;
	} // for eva

	jack_client_close(client);
	return 0;
}

