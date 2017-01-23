#ifndef RTPBUNDLETRANSPORT_H
#define	RTPBUNDLETRANSPORT_H

#include <sys/socket.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <map>
#include <string>
#include <poll.h>
#include <srtp2/srtp.h>
#include "config.h"
#include "DTLSICETransport.h"

class RTPBundleTransport :
	public DTLSICETransport::Sender
{
public:
	RTPBundleTransport();
	~RTPBundleTransport();
	int Init();
	DTLSICETransport* AddICETransport(const std::string &username,const Properties& properties);
	int RemoveICETransport(const std::string &username);
	
	int End();
	
	int GetLocalPort() const { return port; }
	virtual int Send(const ICERemoteCandidate* candidate,const BYTE *buffer,const DWORD size);

private:
	void Start();
	void Stop();
	int  Read();
	int Run();
private:
	static  void* run(void *par);
private:
	typedef std::map<std::string,DTLSICETransport*> RTPICETransports;
	typedef std::map<std::string,ICERemoteCandidate*> RTPICECandidates;
private:
	//Sockets
	int 	socket;
	int 	port;
	pollfd	ufds[1];
	bool	inited;
	bool	running;

	pthread_t thread;
	
	RTPICETransports transports;
	RTPICECandidates candidates;
};

#endif
