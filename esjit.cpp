// See README file.
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <unistd.h>
#include <string>
#include <vector>
#include <ctime>
#include <cerrno>

#include <jack/jack.h>
#include <jack/statistics.h>

using namespace std;

const char NUM_KEYS = 9;
const char* helps[NUM_KEYS][2] = {
{ "Q", "quit" },
{ "r", "refresh connections" },
{ "c N M", "connect port N to port M" },
{ "d N M", "disconnect ports N and M" },
{ "D", "destroy all connections" },
{ "C <f>", "store connection setup to <f>" },
{ "R <f>", "restore connection setup from <f>" },
{ "x", "show server info / statistics" },
{ "i", "print detailed port info" },
};

const char* str_r_col = "\033[0m\033[40m\033[31m"; // record
const char* str_p_col = "\033[0m\033[40m\033[32m"; // playback

jack_client_t *client;

vector<string> portnames;
char* aliases[2];

void refresh_list()
{
	const char **ports = jack_get_ports(client, NULL, NULL, 0);

	portnames.clear();

	for(int i = 0; ports[i]; ++i)
	{
		jack_port_t *port = jack_port_by_name(client, ports[i]);
				
		if(port)
			portnames.push_back(ports[i]);
	}
	free(ports);
}

void print_list_of_conns(const char** connections)
{
	short j = 0;
	short toprint;
	for(;;)
	{
		toprint = -1;
		for(short i = 0; i < portnames.size(); ++i)
		{
			if(portnames[i] == connections[j])
			{
				toprint = i;
				break;
			}
		}
		cout << toprint;
		if(connections[++j])
			cout << ',';
		else
			break;
	}
}

void print_connections()
{
	refresh_list();
	int flags, j;
	const char **connections;
 	cout << "\033[2J\033[H";
	for(int i = 0; i < portnames.size(); ++i)
	{
		jack_port_t *port = jack_port_by_name(client, portnames[i].c_str());
		connections = jack_port_get_all_connections(client, port);
				
		if(port)
		{
			flags = jack_port_flags(port);
			if(flags & JackPortIsInput) // playback port
			{
				if(connections)
				{
					print_list_of_conns(connections);
					cout << "--> ";
				}
				cout << str_p_col;
			}
			else if(flags & JackPortIsOutput) // capture port
				cout << str_r_col;
			cout << '[' << i << "] " << portnames[i] << "\033[0m";
			if(flags & JackPortIsOutput && connections)
			{
				cout << " -->";
				print_list_of_conns(connections);
			}

			if(connections)
				free(connections);
			cout << endl;
		}
	}
}


void print_details()
{
	refresh_list();
	int flags;
	short cnt;
	cout << "Port ID & name | aliases | ltncy/total | flags | type" << endl;
	for(int i = 0; i < portnames.size(); ++i)
	{
		jack_port_t *port = jack_port_by_name(client, portnames[i].c_str());
				
		if(port)
		{
			flags = jack_port_flags(port);
			if(flags & JackPortIsInput)
				cout <<  str_p_col;
			else if(flags & JackPortIsOutput)
				cout <<  str_r_col;

			cout << '[' << i << "] " << portnames[i] << " | ";
			if((cnt = jack_port_get_aliases(port, aliases)) < 0)
			{
				for(; cnt > 0; --cnt)
				{
					cout << aliases[cnt-1];
					if(cnt > 1)
						cout << ", ";
				}
			}
			else cout << '-';
			cout << " | " << jack_port_get_latency(port) << '/'
				<< jack_port_get_total_latency(client, port) << " | ";
			if(flags & JackPortCanMonitor)
				cout << 'm';
			if(flags & JackPortIsPhysical)
				cout << 'P';
			if(flags & JackPortIsTerminal)
				cout << 't';
			cout << " | " << jack_port_type(port) << endl;
			cout << "\033[0m";
		}
	}
}



// Returns true if connections were changed
bool conn_disconn(const bool disc, const short N, const short M)
{
	if(N >= portnames.size() || M >= portnames.size())
	{
		cout << "Invalid port identifiers!" << endl;
		return false;
	}

	jack_port_t *src_port = 0;
	jack_port_t *dst_port = 0;
	jack_port_t *port1 = 0;
	jack_port_t *port2 = 0;
	int port1_flags, port2_flags;

	if(!(port1 = jack_port_by_name(client, portnames[N].c_str())))
	{
		cout << "Port no longer available!" << endl;
		return true; // true, so that we refresh!
	}
	if(!(port2 = jack_port_by_name(client, portnames[M].c_str())))
	{
		cout << "Port no longer available!" << endl;
		return true;
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
			cout << "For full info, read the README file." << endl;
			cout << "Port colours: ";
			cout << str_p_col << "playback" << "\033[0m" << ", " << str_r_col
				<< "capture\033[0m" << endl;
			cout << "Keybindings:" << endl;
			for(N = 0; N < NUM_KEYS; ++N)
			{
				cout << "\033[0m\033[40m\033[36m"
					<< helps[N][0] << '\t'
					<< "\033[0m" << helps[N][1] << endl;
			}
			cout << "In the detailed view, the following abbreviations are used for port flags:";
			cout << endl << "\033[0m\033[40m\033[34m" << 'P' << "\033[0m: corresponds to a physical I/O connector";
			cout << endl << "\033[0m\033[40m\033[34m" << 'm' << "\033[0m: can be monitored";
			cout << endl << "\033[0m\033[40m\033[34m" << 't' << "\033[0m: is a terminal port" << endl;
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
			cout << "**JACK server info**" << endl;
			cout << "Running realtime: ";
			if(jack_is_realtime(client)) cout << "yes";
			else cout << "no";
			cout << endl << "Samplerate: " << jack_get_sample_rate(client);
			cout << endl << "Buffer size: " << jack_get_buffer_size(client);
			cout << endl << "CPU load: " << jack_cpu_load(client);
			cout << endl << "Max delay: " << jack_get_max_delayed_usecs(client) << " microseconds";
			cout << endl;
		}
		else
			cout << "Unknown key \'" << entry << "\'! Enter \'h\' for help." << endl;
	}

	jack_client_close(client);
	return 0;
}

