/* 
 * File:   VNCServer.cpp
 * Author: Sergio
 * 
 * Created on 4 de diciembre de 2013, 15:51
 */
#include "log.h"
#include "errno.h"
#include "VNCServer.h"

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>


extern "C"
{
	#include "rfb/rfbregion.h"
	/* sockets.c */
	 int rfbMaxClientWait = 100;
	 void rfbInitSockets(rfbScreenInfoPtr rfbScreen){}
	 void rfbShutdownSockets(rfbScreenInfoPtr rfbScreen){}
	 void rfbDisconnectUDPSock(rfbScreenInfoPtr rfbScreen){}

	 void rfbCloseClient(rfbClientPtr cl)
	 {
		 ((VNCServer::Client*)(cl->clientData))->Close();
	 }

	 int rfbReadExact(rfbClientPtr cl, char *buf, int len)
	 {
		if(cl->screen && cl->screen->maxClientWait)
			return(rfbReadExactTimeout(cl,buf,len,cl->screen->maxClientWait));
		else
			return(rfbReadExactTimeout(cl,buf,len,0));
	 }

	 int rfbReadExactTimeout(rfbClientPtr cl, char *buf, int len,int timeout)
	 {
		return ((VNCServer::Client*)(cl->clientData))->Read(buf,len,timeout);
	 }

	 int rfbPeekExactTimeout(rfbClientPtr cl, char *buf, int len,int timeout){return 1;}

	 int rfbWriteExact(rfbClientPtr cl, const char *buf, int len)
	 {
/*
		char name[256];
		sprintf(name,"/tmp/vnc_out_%p.raw",cl);
		int fd = open(name,O_CREAT|O_WRONLY|O_APPEND, S_IRUSR | S_IWUSR, 0644);
		write(fd,buf,len);
		close(fd);
*/
		return ((VNCServer::Client*)(cl->clientData))->Write(buf,len);
	 }

	 int rfbCheckFds(rfbScreenInfoPtr rfbScreen,long usec){return 1;}
	 int rfbConnect(rfbScreenInfoPtr rfbScreen, char* host, int port){return 1;}
	 int rfbConnectToTcpAddr(char* host, int port){return 1;}
	 int rfbListenOnTCPPort(int port, in_addr_t iface){return 1;}
	 int rfbListenOnTCP6Port(int port, const char* iface){return 1;}
	 int rfbListenOnUDPPort(int port, in_addr_t iface){return 1;}
	 int rfbStringToAddr(char* string,in_addr_t* addr){return 1;}
	 rfbBool rfbSetNonBlocking(int sock){return 1;}
	 /* httpd.c */
	 void rfbHttpInitSockets(rfbScreenInfoPtr rfbScreen){}
	 void rfbHttpShutdownSockets(rfbScreenInfoPtr rfbScreen){}
	 void rfbHttpCheckFds(rfbScreenInfoPtr rfbScreen){}

	 /* response is cl->authChallenge vncEncrypted with passwd */
	extern rfbBool rfbDefaultPasswordCheck(rfbClientPtr cl,const char* response,int len);
	extern void rfbInitServerFormat(rfbScreenInfoPtr screen, int bitsPerSample);
	extern void rfbDefaultKbdAddEvent(rfbBool down, rfbKeySym keySym, rfbClientPtr cl);
	extern void rfbDefaultSetXCutText(char* text, int len, rfbClientPtr cl);
	extern rfbCursorPtr rfbDefaultGetCursorPtr(rfbClientPtr cl);

	/* from tight.c */
	#ifdef LIBVNCSERVER_HAVE_LIBZ
	#ifdef LIBVNCSERVER_HAVE_LIBJPEG
	extern void rfbTightCleanup(rfbScreenInfoPtr screen);
	#endif

	/* from zlib.c */
	extern void rfbZlibCleanup(rfbScreenInfoPtr screen);

	/* from zrle.c */
	void rfbFreeZrleData(rfbClientPtr cl);
	#endif

	/* from ultra.c */
	extern void rfbFreeUltraData(rfbClientPtr cl);

}

static rfbCursor myCursor =
{
   FALSE, FALSE, FALSE, FALSE,
   (unsigned char*)"\000\102\044\030\044\102\000",
   (unsigned char*)"\347\347\176\074\176\347\347",
   8, 7, 3, 3,
   0, 0, 0,
   0xffff, 0xffff, 0xffff,
   NULL
};

VNCServer::VNCServer()
{
	//NO editor
	editorId = 0;

	//NO listener yet
	listener = NULL;

	//No height
	int width=0;
	int height=0;
	int bytesPerPixel = 4;
	int bitsPerSample  = 8;

	//Alocate screen info
	screen = (rfbScreenInfo*)calloc(sizeof(rfbScreenInfo),1);

	//Set private data to use object to allow locking in static objects
	screen->screenData = (void*)&use;

	//Set default vaules
	screen->autoPort=FALSE;
	screen->clientHead=NULL;
	screen->pointerClient=NULL;
	screen->port=5900;
	screen->ipv6port=5900;
	screen->socketState=RFB_SOCKET_INIT;

	screen->inetdInitDone = FALSE;
	screen->inetdSock=-1;

	screen->udpSock=-1;
	screen->udpSockConnected=FALSE;
	screen->udpPort=0;
	screen->udpClient=NULL;

	screen->maxFd=0;
	screen->listenSock=-1;
	screen->listen6Sock=-1;

	screen->httpInitDone=FALSE;
	screen->httpEnableProxyConnect=FALSE;
	screen->httpPort=0;
	screen->http6Port=0;
	screen->httpDir=NULL;
	screen->httpListenSock=-1;
	screen->httpListen6Sock=-1;
	screen->httpSock=-1;

	screen->desktopName = "MCU";
	screen->alwaysShared = TRUE;
	screen->neverShared = FALSE;
	screen->dontDisconnect = TRUE;

	screen->authPasswdData = NULL;
	screen->authPasswdFirstViewOnly = 1;

	screen->width = width;
	screen->height = height;
	screen->bitsPerPixel = screen->depth = 8*bytesPerPixel;
	screen->paddedWidthInBytes = width*bytesPerPixel;

	//Allocate memory for max usage
	screen->frameBuffer = (char*) malloc32(4096*3072*3);

	screen->passwordCheck = rfbDefaultPasswordCheck;

	screen->ignoreSIGPIPE = TRUE;

	/* disable progressive updating per default */
	screen->progressiveSliceHeight = 0;

	screen->listenInterface = htonl(INADDR_ANY);

	screen->deferUpdateTime=0;
	screen->maxRectsPerUpdate=50;

	screen->handleEventsEagerly = FALSE;

	screen->protocolMajorVersion = rfbProtocolMajorVersion;
	screen->protocolMinorVersion = rfbProtocolMinorVersion;

	screen->permitFileTransfer = FALSE;

	screen->paddedWidthInBytes = width*bytesPerPixel;

	/* format */

	rfbInitServerFormat(screen, bitsPerSample);

	/* cursor */

	screen->cursorX=screen->cursorY=screen->underCursorBufferLen=0;
	screen->underCursorBuffer=NULL;
	screen->dontConvertRichCursorToXCursor = FALSE;
	screen->cursor = &myCursor;
	INIT_MUTEX(screen->cursorMutex);

	IF_PTHREADS(screen->backgroundLoop = FALSE);

	/* proc's and hook's */

	screen->kbdAddEvent = onKeyboardEvent;
	screen->kbdReleaseAllKeys = rfbDoNothingWithClient;
	screen->ptrAddEvent = onMouseEvent;
	screen->setXCutText = rfbDefaultSetXCutText;
	screen->getCursorPtr = rfbDefaultGetCursorPtr;
	screen->setTranslateFunction = rfbSetTranslateFunction;
	screen->newClientHook = NULL;
	screen->displayHook = NULL;
	screen->displayFinishedHook = NULL;
	screen->getKeyboardLedStateHook = NULL;
	screen->xvpHook = NULL;

	/* initialize client list and iterator mutex */
	rfbClientListInit(screen);
}

VNCServer::~VNCServer()
{
}

int VNCServer::Init(Listener* listener)
{
	//Store listener
	this->listener = listener;
}

int VNCServer::SetEditor(int editorId) 
{
	Debug(">VNCServer::SetEditor [partId:%d]\n",editorId);

	//Lock
	use.WaitUnusedAndLock();

	//If we got another editor
	if (this->editorId)
	{
		//Find it
		Clients::iterator it = clients.find(this->editorId);
		//If found
		if (it!=clients.end())
			//Set client view onlu
			it->second->SetViewOnly(true);
	}

	//Set new one
	this->editorId = editorId;
	//Find it
	Clients::iterator it = clients.find(this->editorId);
	//If found
	if (it!=clients.end())
		//Set client editor
		it->second->SetViewOnly(false);
	//Reset screen pointer
	screen->pointerClient = NULL;

	//Unlock
	use.Unlock();

	Debug("<VNCServer::SetEditor [partId:%d]\n",editorId);

	//Ok
	return true;
}

int VNCServer::Connect(int partId,WebSocket *socket)
{
	Log(">VNCServer connecting participant viewer [id:%d]\n",partId);

	//Lock
	use.WaitUnusedAndLock();

	//Create new client
	Client *client = new Client(partId,this);
	//Set client as as user data
	socket->SetUserData(client);

	//Check if there was one already ws connected for that user
	Clients::iterator it = clients.find(partId);
	//If it was
	if (it!=clients.end())
	{
		//Get old client
		Client *old = it->second;
		//End it
		old->Close();
		//Delete it
		delete(old);
		//Set new one
		it->second = client;
	} else {
		//Append to client list
		clients[partId] = client;
	}
	
	//Unlock clients list
	use.Unlock();

	//Accept incoming connection and add us as listeners
	socket->Accept(this);
	
	//Connect to socket
	return client->Connect(socket);
}

int VNCServer::Disconnect(WebSocket *socket)
{
	Log("-VNCServer::Disconnect [ws:%p]\n",socket);

	//Get client
	Client *client = (Client *)socket->GetUserData();

	//If no client was attached
	if (!client)
		//Nothing more
		return 0;
	
	//Lock
	use.WaitUnusedAndLock();
	//Remove it
	clients.erase(client->GetId());
	//Unlock
	use.Unlock();

	//Let it finish
	client->Disconnect();
	//Delete it
	delete(client);

	//OK
	return 1;
}

int VNCServer::Reset()
{
	Log("-VNCServer::Reset\n");

	//Lock
	use.IncUse();

	//Reset size
	screen->width = 0;
	screen->height = 0;
	screen->paddedWidthInBytes = 0;

	//Reset all viewers
	for (Clients::iterator it=clients.begin(); it!=clients.end(); ++it)
		//Send sharing stoped event
		it->second->Reset();

	//Unlock
	use.DecUse();

	return 1;
}

int VNCServer::SetSize(int width,int height)
{
	Log("-VNCServer::SetSize [%d,%d]\n",width,height);

	//Check max size
	if (width*height>4096*3072)
		//Error
		return Error("-Size bigger than max size allowed (4096*3072)\n");
	
	//Lock
	use.IncUse();

	//Set new size
	screen->width = width;
	screen->height = height;
	screen->paddedWidthInBytes = width*screen->depth/8;

	//Resize all viewers
	for (Clients::iterator it=clients.begin(); it!=clients.end(); ++it)
		//Update it
		it->second->ResizeScreen();
	
	//Unlock
	use.DecUse();

	//OK
	return 1;
}

int VNCServer::FrameBufferUpdateDone()
{
	Debug("-FrameBufferUpdateDone\n");

	//LOck
	use.IncUse();

	//Send and update to all viewers
	for (Clients::iterator it=clients.begin(); it!=clients.end(); ++it)
		//Update it
		it->second->Update();
	
	//Unlock
	use.DecUse();
}

int VNCServer::FrameBufferUpdate(BYTE *data,int x,int y,int width,int height)
{
	Debug("-FrameBufferUpdate [x:%d,y:%d,w:%d,h:%d]\n",x,y,width,height);

	//LOck
	use.IncUse();

	//Update frame
	for (int j=y;j<y+height;++j)
		//Copy
		memcpy(screen->frameBuffer+(x+j*screen->width)*4,data+(x+j*screen->width)*4,width*4);

	//Set modified region
	rfbMarkRectAsModified(screen,x,y,width,height);

	//Send and update to all viewers
	for (Clients::iterator it=clients.begin(); it!=clients.end(); ++it)
		//Update it
		it->second->Update();

	//Unlock
	use.DecUse();
}

int VNCServer::CopyRect(BYTE *data,int src_x, int src_y, int w, int h, int dest_x, int dest_y)
{
	Debug("-CopyRect from [%d,%d] to [%d,%d] size [%d,%d]\n",src_x,src_y,dest_x,dest_y,w,h);
	
	//LOck
	use.IncUse();

	//Update frame
	for (int j=src_y;j<src_y+h;++j)
		//Copy
		//memcpy(screen->frameBuffer+(dest_x+j*screen->width)*4,data+(src_x+(src_y+j)*screen->width)*4,w*4);
		memset(screen->frameBuffer+dest_x+j*screen->width*4,0xCA,w*4);

	//Set modified region
	//rfbScheduleCopyRect(screen,src_x, src_y,src_x+w, src_y+h, dest_x, dest_y);
	//Set modified region
	rfbMarkRectAsModified(screen,dest_x,dest_y,w,h);

	//Send and update to all viewers
	for (Clients::iterator it=clients.begin(); it!=clients.end(); ++it)
		//Update it
		it->second->Update();

	//Unlock
	use.DecUse();
}

int VNCServer::End()
{
	Log(">VNCServer::End\n");

	//Lock
	use.WaitUnusedAndLock();

	//Get client iterator
	Clients::iterator it=clients.begin();

	//Close clients
	while (it!=clients.end())
	{
		//Get client
		Client *client = it->second;
		//Erase it and move forward
		clients.erase(it++);
		//End it
		client->Close();
		//Delete it
		delete(client);
	}

	//Unlock
	use.Unlock();

	Log("<VNCServer::End\n");
}


void VNCServer::onKeyboardEvent(rfbBool down, rfbKeySym keySym, rfbClientPtr cl)
{
	//Get client
	Client *client = (Client *)cl->clientData;
	//Get server
	VNCServer* server  = client->GetServer();

	Log("-onKeyboardEvent [key:%d,down:%d,client:%d,editor:%d,listener:%p]\n",keySym,down,server->editorId,client->GetId(),server->listener);

	//Double check it is the editor
	if (server->editorId==client->GetId())
		//If it has listener
		if (server->listener)
			//Launch it
			server->listener->onKeyboardEvent(down,keySym);
}

void VNCServer::onMouseEvent(int buttonMask, int x, int y, rfbClientRec* cl)
{
	//Get client
	Client *client = (Client *)cl->clientData;
	//Get server
	VNCServer* server  = client->GetServer();

	Log("-onMouseEvent [x:%d,y:%d,mask:%x,client:%d,editor:%d,listener:%p]\n",x,y,buttonMask,server->editorId,client->GetId(),server->listener);

	//Double check it is the editor
	if (server->editorId==client->GetId())
		//If it has listener
		if (server->listener)
			//Launch it
			server->listener->onMouseEvent(buttonMask,x,y);
}

void VNCServer::onUpdateDone(rfbClientRec* cl, int result)
{
	
}


void VNCServer::onOpen(WebSocket *ws)
{

}

void VNCServer::onMessageStart(WebSocket *ws,const WebSocket::MessageType type,const DWORD length)
{
	//Do nothing
}

void VNCServer::onMessageData(WebSocket *ws,const BYTE* data, const DWORD size)
{
	//Get client
	Client *client = (Client *)ws->GetUserData();
	//Process it
	client->Process(data,size);
}

void VNCServer::onMessageEnd(WebSocket *ws)
{
	//Get client
	Client *client = (Client *)ws->GetUserData();
	//Process recevied message
	client->ProcessMessage();
}

void VNCServer::onClose(WebSocket *ws)
{
	//Disconnect ws
	Disconnect(ws);
}

void VNCServer::onError(WebSocket *ws)
{
	//Disconnect ws
	Disconnect(ws);
}

void VNCServer::onWriteBufferEmpty(WebSocket *ws)
{
	//Nothing
	Client *client = (Client *)ws->GetUserData();
	//Debug
	Debug("-VNCServer::Client::onWriteBufferEmpty [%p]\n",client);
	//We can send a new update
	client->Update();
}

VNCServer::Client::Client(int id,VNCServer* server)
{
	//Store id
	this->id = id;
	//Store server
	this->server = server;

	//Not reseted
	reset = false;

	//No websocket yet
	this->ws = NULL;

	//Get server screen
	rfbScreenInfo* screen = server->GetScreenInfo();

	//Create client
	cl = (rfbClientRec*)calloc(sizeof(rfbClientRec),1);

	cl->screen = screen;
	cl->sock = 1;			//Dummy value to allow updatiing
	cl->viewOnly = TRUE;
	/* setup pseudo scaling */
	cl->scaledScreen = screen;
	cl->scaledScreen->scaledScreenRefCount++;

	//Allocate size for name
	cl->host = (char*) malloc(64);
	//Print id
	snprintf(cl->host,64,"%d",id);

	rfbResetStats(cl);
	cl->clientData = this;
	cl->clientGoneHook = rfbDoNothingWithClient;

	cl->state = _rfbClientRec::RFB_PROTOCOL_VERSION;

	cl->reverseConnection = FALSE;
	cl->readyForSetColourMapEntries = FALSE;
	cl->useCopyRect = FALSE;
	cl->preferredEncoding = -1;
	cl->correMaxWidth = 48;
	cl->correMaxHeight = 48;
#ifdef LIBVNCSERVER_HAVE_LIBZ
	cl->zrleData = NULL;
#endif

	cl->copyRegion = sraRgnCreate();
	cl->copyDX = 0;
	cl->copyDY = 0;

	cl->modifiedRegion = sraRgnCreateRect(0,0,screen->width,screen->height);

	INIT_MUTEX(cl->updateMutex);
	INIT_COND(cl->updateCond);

	cl->requestedRegion = sraRgnCreate();

	cl->format = cl->screen->serverFormat;
	cl->translateFn = rfbTranslateNone;
	cl->translateLookupTable = NULL;

	//Add to list
	cl->refCount = 0;
	cl->next = screen->clientHead;
	cl->prev = NULL;
	if (screen->clientHead)
		screen->clientHead->prev = cl;

	screen->clientHead = cl;

#if defined(LIBVNCSERVER_HAVE_LIBZ) || defined(LIBVNCSERVER_HAVE_LIBPNG)
	cl->tightQualityLevel = 1;//-1;
#ifdef LIBVNCSERVER_HAVE_LIBJPEG
	cl->tightCompressLevel = 9;//TIGHT_DEFAULT_COMPRESSION;
	cl->turboSubsampLevel = TURBO_DEFAULT_SUBSAMP;
	for (int i = 0; i < 4; i++)
		cl->zsActive[i] = FALSE;
#endif
#endif

	cl->fileTransfer.fd = -1;

	cl->enableCursorShapeUpdates = FALSE;
	cl->enableCursorPosUpdates = FALSE;
	cl->useRichCursorEncoding = FALSE;
	cl->enableLastRectEncoding = FALSE;
	cl->enableKeyboardLedState = FALSE;
	cl->enableSupportedMessages = FALSE;
	cl->enableSupportedEncodings = FALSE;
	cl->enableServerIdentity = FALSE;
	cl->lastKeyboardLedState = -1;
	cl->cursorX = screen->cursorX;
	cl->cursorY = screen->cursorY;
	cl->useNewFBSize = FALSE;

#ifdef LIBVNCSERVER_HAVE_LIBZ
	cl->compStreamInited = FALSE;
	cl->compStream.total_in = 0;
	cl->compStream.total_out = 0;
	cl->compStream.zalloc = Z_NULL;
	cl->compStream.zfree = Z_NULL;
	cl->compStream.opaque = Z_NULL;

	cl->zlibCompressLevel = 5;
#endif

	cl->progressiveSliceY = 0;
	cl->extensions = NULL;
	cl->lastPtrX = -1;
}

VNCServer::Client::~Client()
{
	if (cl->prev)
		cl->prev->next = cl->next;
	else
		cl->screen->clientHead = cl->next;
	if (cl->next)
		cl->next->prev = cl->prev;

	if (cl->scaledScreen!=NULL)
		cl->scaledScreen->scaledScreenRefCount--;

#ifdef LIBVNCSERVER_HAVE_LIBZ
	rfbFreeZrleData(cl);
#endif

	rfbFreeUltraData(cl);

	/* free buffers holding pixel data before and after encoding */
	free(cl->beforeEncBuf);
	free(cl->afterEncBuf);


	free(cl->host);

#ifdef LIBVNCSERVER_HAVE_LIBZ
	/* Release the compression state structures if any. */
	if ( cl->compStreamInited )
		deflateEnd( &(cl->compStream) );

#ifdef LIBVNCSERVER_HAVE_LIBJPEG
	for (int i = 0; i < 4; i++)
		if (cl->zsActive[i])
			deflateEnd(&cl->zsStruct[i]);
#endif
#endif

	if (cl->screen->pointerClient == cl) cl->screen->pointerClient = NULL;

	sraRgnDestroy(cl->modifiedRegion);
	sraRgnDestroy(cl->requestedRegion);
	sraRgnDestroy(cl->copyRegion);

	if (cl->translateLookupTable) free(cl->translateLookupTable);

	TINI_COND(cl->updateCond);
	TINI_MUTEX(cl->updateMutex);

	rfbPrintStats(cl);
	rfbResetStats(cl);

	//Free mem
	free(cl);
}

int VNCServer::Client::Connect(WebSocket *ws)
{
	Debug(">VNCServer::Client::Connect [ws:%p,this:%p]\n",ws,this);
	//Store websocekt
	this->ws = ws;

	rfbProtocolVersionMsg pv;
	sprintf(pv,rfbProtocolVersionFormat,cl->screen->protocolMajorVersion,cl->screen->protocolMinorVersion);

	//Write protocol version
	if (rfbWriteExact(cl, pv, sz_rfbProtocolVersionMsg) < 0)
	{
		rfbLogPerror("rfbNewClient: write");
		rfbCloseClient(cl);
		rfbClientConnectionGone(cl);
		return Error("-Could not write protocol version");
	}

	//Enable extension
	for(rfbProtocolExtension* extension = rfbGetExtensionIterator(); extension; extension=extension->next)
	{
		void* data = NULL;
		/* if the extension does not have a newClient method, it wants
		* to be initialized later. */
		if(extension->newClient && extension->newClient(cl, &data))
			rfbEnableExtension(cl, extension, data);
	}
	rfbReleaseExtensionIterator();

	cl->onHold = FALSE;

	//Start thread
	createPriorityThread(&thread,run,this,0);

	Debug("<VNCServer::Client::Connect [ws:%p,this:%p]\n",ws,this);

	//OK
	return 1;
}

int VNCServer::Client::Disconnect()
{
	Debug(">VNCServer::Client::Disconnect [this:%p]\n",this);

	//Detach listeners
	ws->Detach();
	//Remove ws data
	ws->SetUserData(NULL);

	Debug("-VNCServer::Client::Disconnect cancel wait [this:%p]\n",this);

	//Cancel read wait
	wait.Cancel();
	//Signal cond to exit update loop
	TSIGNAL(cl->updateCond);

	Debug("-VNCServer::Client::Disconnect cancel join [this:%p]\n",this);
	//Join thread
	pthread_join(thread,NULL);

	//Wait
	Debug("<VNCServer::Client::Disconnect [this:%p]\n",this);

	//OK
	return 1;
}

int VNCServer::Client::Process(const BYTE* data,DWORD size)
{
	Debug("-Process size:%d\n",size);
	//Lock buffer
	wait.Lock();
	//Puss to buffer
	buffer.push(data,size);
	//Unlock
	wait.Unlock();
	//Signal
	wait.Signal();
	//Return written
	return size;
}

int VNCServer::Client::WaitForData(DWORD usecs)
{
	Debug(">WaitForData %d\n",buffer.length());

	//Wait for data
	int ret = wait.WaitSignal(usecs*1000);

	Debug("<WaitForData %d %d\n",buffer.length(),ret);

	return ret;
}

void VNCServer::Client::Close()
{
	Log("-VNCServer::Client::Close\n");
	//Disconnect
	Disconnect();
	//Close
	ws->Close();
}

void *VNCServer::Client::run(void *par)
{
	Log("VNCServer::Client::Thread [%p]\n",pthread_self());
	//Get worker
	VNCServer::Client *client = (VNCServer::Client *)par;
	//Block all signals
	blocksignals();
	//Run
	client->Run();
	//Exit
	return NULL;
}

int VNCServer::Client::Read(char *out, int size, int timeout)
{
	//Bytes read
	int num = 0;
	
	//Debug(">Read requested:%d,buffered:%d\n",size,buffer.length());

	//Lock
	wait.Lock();

	//Check if we have enought data
	while(buffer.length()<size)
	{
		//Wait for mor data
		if (!wait.WaitSignal(timeout))
			//End
			goto end;
	}

	//Read
	num = buffer.pop((BYTE*)out,size);

end:
	//Unlock
	wait.Unlock();

	//Debug("<Read requested:%d,returned:%d,left:%d\n",size,num,buffer.length());

	//Return readed
	return num;
}
int VNCServer::Client::Write(const char *buf, int size)
{
	//Send data
	ws->SendMessage((BYTE*)buf,size);
	//OK
	return size;
}

int VNCServer::Client::ProcessMessage()
{
	//While we have data
	while(buffer.length() && !wait.IsCanceled())
		//Process message
		rfbProcessClientMessage(cl);
	
	//OK
	return 1;
}

int VNCServer::Client::Run()
{
	Log(">VNCServer::Client::Run [this:%p]\n",this);

	//Lock
	LOCK(cl->updateMutex);
	
	//Until ended
	while (!wait.IsCanceled())
	{
		Debug("-VNCServer::Client lopp [this:%p,state:%d,empty:%d]\n",this,cl->state,sraRgnEmpty(cl->requestedRegion));
		//If connected
		/* always require a FB Update Request (otherwise can crash.) */
		if (cl->state == rfbClientRec::RFB_NORMAL && !sraRgnEmpty(cl->requestedRegion))
		{
			//If reseted
			if (reset)
			{
				Log("-Reseting client\n");
				//Reset message
				char msg[] = {16};
				//Write it
				Write(msg,sizeof(msg));
				//Clear reset flag
				reset = false;
			}

			//If the scren has been resized
			if (cl->newFBSizePending)
			{
				//Free region
				sraRgnDestroy(cl->modifiedRegion);
				//Set new modified region
				cl->modifiedRegion = sraRgnCreateRect(0,0,cl->screen->width,cl->screen->height);
			}
			
			//We need to update by default
			bool haveUpdate = true;

			/* Now, get the region we're going to update, and remove
			it from cl->modifiedRegion _before_ we send the update.
			That way, if anything that overlaps the region we're sending
			is updated, we'll be sure to do another update later. */
			sraRegion* updateRegion = sraRgnCreateRgn(cl->modifiedRegion);

			//Check if we didn't already have a pending update
			if  (!FB_UPDATE_PENDING(cl))
				//Check it is inside requested region
				haveUpdate = !sraRgnEmpty(cl->modifiedRegion);//sraRgnAnd(updateRegion,cl->requestedRegion);
				
			//Unlock region
			UNLOCK(cl->updateMutex);

			Debug("-VNCServer::Client update [this:%p,update:%d,mod:%d,req:%d]\n",this,haveUpdate,sraRgnEmpty(cl->modifiedRegion),sraRgnEmpty(cl->requestedRegion));

			//If we have to update
			if (haveUpdate)
			{
				Debug(">VNCServer::Client SendFramebufferUpdate [%p]\n",this);
				/* Now actually send the update. */
				rfbIncrClientRef(cl);
				//LOCK(cl->sendMutex);
				rfbSendFramebufferUpdate(cl, updateRegion);
				//UNLOCK(cl->sendMutex);
				rfbDecrClientRef(cl);
				Debug("<VNCServer::Clietn SendFramebufferUpdate [%p]\n",this);
			}

			//Destroy region
			sraRgnDestroy(updateRegion);

			//Lock again
			LOCK(cl->updateMutex);
		}
		Debug("<VNCServer::Client going to sleep [this:%p]\n",this);

		//Wait cond
		WAIT(cl->updateCond,cl->updateMutex);

		Debug(">VNCServer::Client waked up from wait mutex[this:%p,isCanceled:%d]\n",this,wait.IsCanceled());
	}
	//Unlock
	UNLOCK(cl->updateMutex);

	Log("<VNCServer::Client::Run [this:%p]\n",this);
	
	//OK
	return 0;
}

void VNCServer::Client::Update()
{
	Debug("-VNCServer::Client::Update\n");
	//Check if write buffer is empty before sending anything
	if (ws->IsWriteBufferEmtpy())
		//Signal cond
		TSIGNAL(cl->updateCond);
}

void VNCServer::Client::ResizeScreen()
{
	//Lock update region
	LOCK(cl->updateMutex);
	//Set new one and wait for display finished hook to do the other chantes
	cl->newFBSizePending = true;
	//Unlock region
	UNLOCK(cl->updateMutex);
	//Update
	Update();
}

void VNCServer::Client::Reset()
{
	//Lock update region
	LOCK(cl->updateMutex);
	//We have been reseted
	reset = true;
	//Unlock region
	UNLOCK(cl->updateMutex);
	//Update
	Update();
}

void VNCServer::Client::SetViewOnly(bool viewOnly)
{
	this->cl->viewOnly = viewOnly;
}

