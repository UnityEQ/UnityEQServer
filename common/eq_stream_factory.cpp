#include "global_define.h"
#include "eqemu_logsys.h"
#include "eq_stream_factory.h"

#ifdef _WINDOWS
	#include <winsock.h>
	#include <process.h>
	#include <io.h>
	#include <stdio.h>
#else
	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <sys/select.h>
	#include <arpa/inet.h>
	#include <netdb.h>
	#include <pthread.h>
#endif

#include <iostream>
#include <fcntl.h>

#include "../common/SocketLib/Base64.h"

#include "op_codes.h"

ThreadReturnType EQStreamFactoryReaderLoop(void *eqfs)
{
EQStreamFactory *fs=(EQStreamFactory *)eqfs;

#ifndef WIN32
	Log.Out(Logs::Detail, Logs::None,  "Starting EQStreamFactoryReaderLoop with thread ID %d", pthread_self());
#endif

	fs->ReaderLoop();

#ifndef WIN32
	Log.Out(Logs::Detail, Logs::None,  "Ending EQStreamFactoryReaderLoop with thread ID %d", pthread_self());
#endif

	THREAD_RETURN(nullptr);
}

ThreadReturnType EQStreamFactoryWriterLoop(void *eqfs)
{
	EQStreamFactory *fs=(EQStreamFactory *)eqfs;

#ifndef WIN32
	Log.Out(Logs::Detail, Logs::None,  "Starting EQStreamFactoryWriterLoop with thread ID %d", pthread_self());
#endif

	fs->WriterLoop();

#ifndef WIN32
	Log.Out(Logs::Detail, Logs::None,  "Ending EQStreamFactoryWriterLoop with thread ID %d", pthread_self());
#endif

	THREAD_RETURN(nullptr);
}

EQStreamFactory::EQStreamFactory(EQStreamType type, int port, uint32 timeout)
	: Timeoutable(5000), stream_timeout(timeout)
{
	StreamType=type;
	Port=port;
	sock=-1;
}

void EQStreamFactory::Close()
{
	Stop();

#ifdef _WINDOWS
	closesocket(sock);
#else
	close(sock);
#endif
	sock=-1;
}

bool EQStreamFactory::Open()
{
struct sockaddr_in address;
#ifndef WIN32
	pthread_t t1,t2;
#endif
	/* Setup internet address information.
	This is used with the bind() call */
	memset((char *) &address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = htons(Port);
	address.sin_addr.s_addr = htonl(INADDR_ANY);

	/* Setting up UDP port for new clients */
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0) {
		return false;
	}

	if (bind(sock, (struct sockaddr *) &address, sizeof(address)) < 0) {
		close(sock);
		sock=-1;
		return false;
	}
	#ifdef _WINDOWS
		unsigned long nonblock = 1;
		ioctlsocket(sock, FIONBIO, &nonblock);
	#else
		fcntl(sock, F_SETFL, O_NONBLOCK);
	#endif
	//moved these because on windows the output was delayed and causing the console window to look bad
	#ifdef _WINDOWS
		_beginthread(EQStreamFactoryReaderLoop,0, this);
		_beginthread(EQStreamFactoryWriterLoop,0, this);
	#else
		pthread_create(&t1,nullptr,EQStreamFactoryReaderLoop,this);
		pthread_create(&t2,nullptr,EQStreamFactoryWriterLoop,this);
	#endif
	return true;
}

std::shared_ptr<EQStream> EQStreamFactory::Pop()
{
	std::shared_ptr<EQStream> s = nullptr;
	MNewStreams.lock();
	if (NewStreams.size()) {
		s = NewStreams.front();
		NewStreams.pop();
		s->PutInUse();
	}
	MNewStreams.unlock();

	return s;
}

void EQStreamFactory::Push(std::shared_ptr<EQStream> s)
{
	MNewStreams.lock();
	NewStreams.push(s);
	MNewStreams.unlock();
}

void EQStreamFactory::ReaderLoop()
{
	fd_set readset;
	std::map<std::pair<uint32, uint16>, std::shared_ptr<EQStream>>::iterator stream_itr;
	int num;
	int length;
	unsigned char buffer[2048];
	sockaddr_in from;
	int socklen = sizeof(sockaddr_in);
	timeval sleep_time;
	ReaderRunning = true;

	while(sock!=-1) {
		MReaderRunning.lock();
		if (!ReaderRunning)
			break;
		MReaderRunning.unlock();

		FD_ZERO(&readset);
		FD_SET(sock,&readset);

		sleep_time.tv_sec=30;
		sleep_time.tv_usec=0;
		if ((num=select(sock+1,&readset,nullptr,nullptr,&sleep_time))<0) {
			// What do we wanna do?
			continue;
		} else if (num==0)
			continue;

		if(sock == -1)
			break;		//somebody closed us while we were sleeping.

		if (FD_ISSET(sock,&readset)) {
#ifdef _WINDOWS
			if ((length=recvfrom(sock,(char*)buffer,sizeof(buffer),0,(struct sockaddr*)&from,(int *)&socklen)) < 2)
#else
			if ((length=recvfrom(sock,buffer,2048,0,(struct sockaddr *)&from,(socklen_t *)&socklen)) < 2)
#endif
			{
				// What do we wanna do?
			} else {
				MStreams.lock();
				stream_itr = Streams.find(std::make_pair(from.sin_addr.s_addr, from.sin_port));
				if (stream_itr == Streams.end()) {
					if (buffer[1]==OP_SessionRequest) {
						std::shared_ptr<EQStream> s = std::make_shared<EQStream>(from);
						s->SetStreamType(StreamType);
						Streams[std::make_pair(from.sin_addr.s_addr, from.sin_port)]=s;
						WriterWork.Signal();
						Push(s);
						s->AddBytesRecv(length);
						s->Process(buffer,length);
						s->SetLastPacketTime(Timer::GetCurrentTime());
					}
					MStreams.unlock();
				} else {
					std::shared_ptr<EQStream> curstream = stream_itr->second;
					//dont bother processing incoming packets for closed connections
					if(curstream->CheckClosed())
						curstream = nullptr;
					else
						curstream->PutInUse();
						//the in use flag prevents the stream from being deleted while we are using it.

					if(curstream) {
						curstream->AddBytesRecv(length);
						curstream->Process(buffer,length);
						curstream->SetLastPacketTime(Timer::GetCurrentTime());
						curstream->ReleaseFromUse();
					}
					MStreams.unlock();
				}
			}
		}
	}
}

void EQStreamFactory::CheckTimeout()
{
	//lock streams the entire time were checking timeouts, it should be fast.
	MStreams.lock();

	unsigned long now=Timer::GetCurrentTime();
	std::map<std::pair<uint32, uint16>, std::shared_ptr<EQStream>>::iterator stream_itr;

	for(stream_itr = Streams.begin(); stream_itr != Streams.end();) {
		std::shared_ptr<EQStream> s = stream_itr->second;

		s->CheckTimeout(now, stream_timeout);

		EQStreamState state = s->GetState();

		//not part of the else so we check it right away on state change
		if (state==CLOSED) {
			if (s->IsInUse()) {
				//give it a little time for everybody to finish with it
			} else {
				//everybody is done, we can delete it now
				std::map<std::pair<uint32, uint16>, std::shared_ptr<EQStream>>::iterator temp = stream_itr;
				++stream_itr;
				temp->second = nullptr;
				Streams.erase(temp);
				continue;
			}
		}

		++stream_itr;
	}
	MStreams.unlock();
}

void EQStreamFactory::WriterLoop()
{
	std::map<std::pair<uint32, uint16>, std::shared_ptr<EQStream>>::iterator stream_itr;
	bool havework=true;
	std::vector<std::shared_ptr<EQStream>> wants_write;
	std::vector<std::shared_ptr<EQStream>>::iterator cur, end;
	bool decay = false;
	uint32 stream_count;
	Timer DecayTimer(20);
	WriterRunning = true;
	DecayTimer.Enable();

	while(sock!=-1) {
		MWriterRunning.lock();
		if (!WriterRunning)
			break;
		MWriterRunning.unlock();

		havework = false;
		wants_write.clear();

		decay=DecayTimer.Check();

		//copy streams into a seperate list so we dont have to keep
		//MStreams locked while we are writting
		MStreams.lock();
		for(stream_itr=Streams.begin();stream_itr!=Streams.end();++stream_itr) {
			// If it's time to decay the bytes sent, then let's do it before we try to write
			if (decay)
				stream_itr->second->Decay();

			//bullshit checking, to see if this is really happening, GDB seems to think so...
			if(stream_itr->second == nullptr) {
				fprintf(stderr, "ERROR: nullptr Stream encountered in EQStreamFactory::WriterLoop for: %i:%i", stream_itr->first.first, stream_itr->first.second);
				continue;
			}

			if (stream_itr->second->HasOutgoingData()) {
				havework=true;
				stream_itr->second->PutInUse();
				wants_write.push_back(stream_itr->second);
			}
		}
		MStreams.unlock();

		//do the actual writes
		cur = wants_write.begin();
		end = wants_write.end();
		for(; cur != end; ++cur) {
			(*cur)->Write(sock);
			(*cur)->ReleaseFromUse();
		}

		Sleep(10);

		MStreams.lock();
		stream_count = Streams.size();
		MStreams.unlock();
		if (!stream_count) {
			WriterWork.Wait();
		}
	}
}

bool EQWebStreamFactory::Open()
{
	return true;
}

void EQWebStreamFactory::CheckTimeout()
{
	//lock streams the entire time were checking timeouts, it should be fast.
	MStreams.lock();

	unsigned long now=Timer::GetCurrentTime();
	std::map<std::string, std::shared_ptr<EQWebStream>>::iterator stream_itr;

	for(stream_itr = Streams.begin(); stream_itr != Streams.end();) {
		std::shared_ptr<EQWebStream> s = stream_itr->second;

		s->CheckTimeout(now, stream_timeout);

		EQStreamState state = s->GetState();

		//not part of the else so we check it right away on state change
		if (state==CLOSED) {
			if (s->IsInUse()) {
				//give it a little time for everybody to finish with it
			} else {
				//everybody is done, we can delete it now

				//first, clear the remaining packets we were sending
				s->RemoveData();
				std::map<std::string, std::shared_ptr<EQWebStream>>::iterator temp = stream_itr;
				++stream_itr;
				temp->second = nullptr;
				Streams.erase(temp);
				continue;
			}
		}

		++stream_itr;
	}
	MStreams.unlock();
}


void EQWebStreamFactory::CheckNewConnection(const char* connection_id)
{
	//lock streams the entire time were checking timeouts, it should be fast.
	MStreams.lock();

	unsigned long now = Timer::GetCurrentTime();
	std::map<std::string, std::shared_ptr<EQWebStream>>::iterator stream_itr;

	for (stream_itr = Streams.begin(); stream_itr != Streams.end();) {
		std::shared_ptr<EQWebStream> s = stream_itr->second;
		if (strcmp(connection_id, stream_itr->first.c_str()) == 0)
		{
			//abort everything

			//first, clear the remaining packets we were sending
			s->RemoveData();
			std::map<std::string, std::shared_ptr<EQWebStream>>::iterator temp = stream_itr;
			++stream_itr;
			temp->second = nullptr;
			Streams.erase(temp);
			continue;
		}
		++stream_itr;
	}
	MStreams.unlock();
}


bool EQWebStreamFactory::FindSessionByID(const char* connection_id) { 
	MStreams.lock();
	std::map<std::string, std::shared_ptr<EQWebStream>>::iterator stream = Streams.find(connection_id); 
	bool ret = false;
	if(stream != Streams.end())
		ret = true;
	MStreams.unlock();
	return ret; 
}

bool EQWebStreamFactory::ProcessIncomingPacket(const char* connection_id, std::vector<std::string>& params) {  
	std::shared_ptr<EQWebStream> ptr_stream = nullptr;

	uint32 zone_id = std::stoi(params[0]);
	uint32 instance_id = std::stoi(params[1]);
	uint32 opcode = std::stoi(params[2]);
	uint32 datasize = std::stoi(params[3]);
	std::string dec = "";
	
	if(datasize > 0)
		Base64::decode(params[4], dec);

	

	if(dec.size() < datasize)
	{
		return true;
	}
	MStreams.lock();
	std::map<std::string, std::shared_ptr<EQWebStream>>::iterator stream = Streams.find(connection_id);
	if(stream != Streams.end())
		ptr_stream = (stream)->second;

	if(ptr_stream == nullptr)
	{
		MStreams.unlock();
		return true;
	}
	if(ptr_stream)
	{
		ptr_stream->ProcessPacket(opcode, (const unsigned char*)&dec[0], datasize);
	}
	MStreams.unlock();
	return true; 
}

std::shared_ptr<EQWebStream> EQWebStreamFactory::Pop()
{
	std::shared_ptr<EQWebStream> s = nullptr;
	MNewStreams.lock();
	if (NewStreams.size()) {
		s = NewStreams.front();
		NewStreams.pop();
		s->PutInUse();
	}
	MNewStreams.unlock();

	return s;
}

void EQWebStreamFactory::Push(const char* session_id, std::vector<std::string>& params)
{
	MStreams.lock();
	std::shared_ptr<EQWebStream> s = std::make_shared<EQWebStream>(std::string(session_id));
	s->SetLastPacketTime(Timer::GetCurrentTime());
	s->SetStreamType(StreamType);
	Streams[session_id]=s;
	WriterWork.Signal();
	MNewStreams.lock();
	NewStreams.push(s);
	MNewStreams.unlock();
	MStreams.unlock();
	ProcessIncomingPacket(session_id,params);
}