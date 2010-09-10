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

const char* VERSION = "1";

const char NUM_KEYS = 9;
const char* helps[NUM_KEYS][2] = {
{ "Q", "quit" },
{ "r", "refresh connections" },
{ "c N M", "connect port N to port M" },
{ "d N M", "disconnect ports N and M" },
{ "D", "destroy all connections" },
{ "C <f>", "store connection setup to file <f>" },
{ "R <f>", "restore connection setup from file <f>" },
{ "x", "show server info / statistics" },
{ "i", "print detailed port info" },
};

// colours:
const char* DEFCOL = "\033[0m";
const char* COL = "\033[0m\033[40m\033[3";
const char* RED = "1m";
const char* GREEN = "2m";
const char* BLUE = "4m";
const char* MAG = "5m";
const char* CYAN = "6m";


jack_client_t *client;
char* aliases[2];

// We represent the ports and their connections by a graph, where the
// vertex data is a pointer to the JACK internal port structure.
typedef adjacency_list<setS, vecS, undirectedS, jack_port_t*> PortGraph;
typedef PortGraph::vertex_descriptor vtex;
typedef PortGraph::vertex_iterator vtex_it;
typedef PortGraph::adjacency_iterator adj_it;

PortGraph portgraph;


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

jack_port_t* port_by_index(short N)
{
	if(N >= num_vertices(portgraph))
		return NULL;
	vtex_it vti = vertices(portgraph).first;
	for(; N > 0; --N)
		++vti;
	return portgraph[*vti];
}

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

void refresh_list()
{
	portgraph.clear();
	vtex vd1, vd2;
	vtex_it vti;

	jack_port_t *port, *port2;
	const char **ports = jack_get_ports(client, NULL, NULL, 0);
	// We loop the ports twice; first forming the vertices of the graph (the ports), next
	// the edges (the connections). This ensures the ordering of the ports remains
	// consistent throughout.
	for(int i = 0; ports[i]; ++i)
	{
		if((port = jack_port_by_name(client, ports[i])))
			portgraph[add_vertex(portgraph)] = port;
	}
	const char **connections;
	int j;
	for(int i = 0; ports[i]; ++i)
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
						return; // give up midway
					}
				}
			}
		}
	}
	free(ports);
}

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

void print_connections()
{
	refresh_list();

	vector<string> columns[3];
	short longests[2] = { 0, 0 };
	
	// Collect what to print:
	char ch;
	int flags;
	short n = 0;
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


void print_details()
{
	refresh_list();

	// Collect what to print:
	vector<bool> redstart;
	vector<string> columns[5];
	columns[0].push_back("Port ID & name ");
	columns[1].push_back("aliases ");
	columns[2].push_back("ltncy/tot ");
	columns[3].push_back("flgs ");
	columns[4].push_back("type");
	short longests[4] = { 15, 8, 10, 5 }; // lengths of the above
	int flags;
	char ch;
	short cnt;
	short n = 0;
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

		columns[2].push_back(lexical_cast<string>(jack_port_get_latency(portgraph[*v.first]))
			+ '/' + lexical_cast<string>(jack_port_get_total_latency(client, portgraph[*v.first])));

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
	for(ch = 0; ch < 4; ++ch)
		cout << setw(longests[ch]) << columns[ch][0];
	cout << columns[4][0] << endl;
	for(n = 1; n < columns[0].size(); ++n)
	{
		cout << COL;
		if(redstart[n-1]) cout << RED;
		else cout << GREEN;
		cout << setw(longests[0]) << columns[0][n] << DEFCOL;
		for(ch = 1; ch < 4; ++ch)
			cout << setw(longests[ch]) << columns[ch][n];
		cout << columns[4][n] << endl;
	}
}


// Returns true if connections were changed
bool conn_disconn(const bool disc, const short N, const short M)
{
	jack_port_t *src_port = 0;
	jack_port_t *dst_port = 0;
	jack_port_t *port1 = 0;
	jack_port_t *port2 = 0;
	int port1_flags, port2_flags;

	if(!(port1 = port_by_index(N)) || !(port2 = port_by_index(M)))
	{
		cout << "Invalid numbers!" << endl;
		return true; // true, so that we refresh!
	}

	port1_flags = jack_port_flags(port1);
	port2_flags = jack_port_flags(port2);

	if(port1_flags & JackPortIsInput)
	{
		if(port2_flags & JackPortIsOutput)
		{
			src_port = port2;
			dst_port = port1;
		}
	}
	else if(port2_flags & JackPortIsInput)
	{
		src_port = port1;
		dst_port = port2;
	}

	if (!src_port || !dst_port)
	{
		cout << "One port has to be input, the other output!" << endl;
		return false;
	}

	if(disc)
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
	return true;
}


int main(int argc, char* argv[])
{
	jack_status_t status;
    jack_options_t options = JackNoStartServer;
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
		cin >> entry;
		if(entry == "Q")
			break;
		if(entry == "h")
		{
			cout << "esjit version " << VERSION << endl;
			cout << "For full info, read the README file, "
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
			cout << endl << COL << BLUE << 'P' << DEFCOL << ": corresponds to a physical I/O connector";
			cout << endl << COL << BLUE << 'm' << DEFCOL << ": can be monitored";
			cout << endl << COL << BLUE << 't' << DEFCOL << ": is a terminal port" << endl;
		}
		else if(entry == "r")
			print_connections();
		else if(entry == "i")
			print_details();
		else if(entry == "c" || entry == "d")
		{
			cin >> N;
			cin >> M;
			if(conn_disconn(entry == "d", N, M))
				print_connections();
		}
		else if(entry == "D")
		{
			const char **connections;
			const char **ports = jack_get_ports(client, NULL, NULL, 0);
			for(N = 0; ports[N]; ++N)
			{
				if((connections = jack_port_get_all_connections(client,
					jack_port_by_name(client, ports[N]))))
				{
					for(M = 0; connections[M]; ++M)
						jack_disconnect(client, ports[N], connections[M]);
					free(connections);
				}
			}
			free(ports);
			print_connections();
		}
		else if(entry == "C")
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
  				const char **jack_oports = jack_get_ports(client, 0, 0, JackPortIsOutput);
				for(N = 0; jack_oports[N]; ++N)
				{
      				const char **connections = jack_port_get_all_connections(client,
				      jack_port_by_name(client, jack_oports[N]));
					if(connections)
					{
						ofile << jack_oports[N] << endl;
						for(M = 0; connections[M]; ++M)
							ofile << '\t' << connections[M] << endl;
						free(connections);
					}
      			}
				ofile.close();
				free(jack_oports);
				cout << "Saved connection setup to \'" << entry << "\'." << endl;
			}
		}
		else if(entry == "R")
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
					if(s[0] != '\t')
						outname = s;
					else // connect ports "s" and "outname"
					{
						s.erase(0,1); // remove the tab
						N = jack_connect(client, outname.c_str(), s.c_str());
						if(N && N != EEXIST) // "connection already exists" is not an error here
							cout << "Could not connect \'" << s << "\' to \'" << outname << "\'." << endl;
					}
				}
				print_connections();
			}
		}
		else if(entry == "x")
		{
			cout << COL << BLUE << "**JACK server info**" << DEFCOL << endl;
			cout << "Running realtime: ";
			if(jack_is_realtime(client)) cout << COL << BLUE << "yes";
			else cout << COL << MAG << "no";
			cout << DEFCOL << endl << "Samplerate: " << COL << CYAN << jack_get_sample_rate(client) << " Hz";
			cout << DEFCOL << endl << "Buffer size: " << COL << CYAN << jack_get_buffer_size(client) << " b";
			cout << DEFCOL << endl << "CPU load: " << COL << CYAN << jack_cpu_load(client);
			cout << DEFCOL << endl << "Max delay: " << COL << CYAN << jack_get_max_delayed_usecs(client) << " microseconds";
			cout << DEFCOL << endl;
		}
		else
			cout << "Unknown key \'" << entry << "\'! Enter \'h\' for help." << endl;
	}

	jack_client_close(client);
	return 0;
}

