/* 
 * File:   appmixer.cpp
 * Author: Sergio
 * 
 * Created on 15 de enero de 2013, 15:38
 */

#include "appmixer.h"
#include "log.h"
#include "fifo.h"
#include "use.h"


AppMixer::AppMixer()
{
	//No output
	output = NULL;
	//No presenter
	presenter = NULL;
	//No img
	img = NULL;
	// Create YUV rescaller cotext
	sws = sws_alloc_context();
}
AppMixer::~AppMixer()
{
	//Clean mem
	if (img) free(img);
	//Free sws contes
	sws_freeContext(sws);
}

int AppMixer::Init(VideoOutput* output)
{
	//Set output
	this->output = output;
}

int AppMixer::DisplayImage(const char* filename)
{
	Log("-DisplayImage [\"%s\"]\n",filename);

	//Load image
	if (!logo.Load(filename))
		//Error
		return Error("-Error loading file");

	//Check output
	if (!output)
		//Error
		return Error("-No output");

	//Set size
	output->SetVideoSize(logo.GetWidth(),logo.GetHeight());
	//Set image
	output->NextFrame(logo.GetFrame());

	//Everything ok
	return true;
}

int AppMixer::End()
{
	//Reset output
	output = NULL;

	//Check if presenting
	if (presenter)
	{
		//ENd presenter
		presenter->End();
		//End it
		presenter->ws->Close();
		//Delete presenter
		delete(presenter);
		//Nullify
		 presenter = NULL;
	}
	for (Viewers::iterator it = viewers.begin(); it!=viewers.end(); ++it)
		it->second->ws->Close();
}

int AppMixer::WebsocketConnectRequest(int partId,WebSocket *ws,bool isPresenter)
{
	Log("-WebsocketConnectRequest [partId:%d,isPresenter:%d]\n",partId,isPresenter);

	if (isPresenter)
	{
		//LOck
		use.WaitUnusedAndLock();
		//Check if already presenting
		if (presenter)
		{
			//Close websocket
			presenter->ws->Close();
			//Delete it
			delete(presenter);
		}
		//Create new presenter
		presenter = new Presenter(ws);
		//No user data
		ws->SetUserData(NULL);
		//Init it
		presenter->Init(this);
		//Unlock
		use.Unlock();
	} else {
		//Lock
		use.IncUse();
		//Find 
		Viewers::iterator it = viewers.find(partId);
		//If already got a viewer for that aprticipant
		if (it!=viewers.end())
		{
			//Get viewer
			Viewer* viewer = it->second;
			//Close websocket
			viewer->ws->Close();
			//Delete it
			delete(viewer);	
			//Erase it
			viewers.erase(it);
		}
		//Create new viewer
		Viewer* viewer = new Viewer(partId,ws);
		//Set as ws user data
		ws->SetUserData(viewer);
		//add To map
		viewers[partId] = viewer;
		//Unlock
		use.DecUse();
	}

	//Accept connection
	ws->Accept(this);
}

void AppMixer::onOpen(WebSocket *ws)
{

}

void AppMixer::onMessageStart(WebSocket *ws,const WebSocket::MessageType type,const DWORD length)
{
	Debug("-onMessageData %p size:%d\n",ws,length);
}

void AppMixer::onMessageData(WebSocket *ws,const BYTE* data, const DWORD size)
{
	Debug("-onMessageData %p size:%d\n",ws,size);

	///Get user data
	void * user = ws->GetUserData();

	//Check if it is presenter
	if (user)
	{
		//Get viewer from user data
		Viewer* viewer = (Viewer*) user;
		//Lock
		use.IncUse();
		//Process data
		viewer->Process(data,size);
		//Unlock
		use.DecUse();
	} else if (ws==presenter->ws) {
		//Process it
		presenter->Process(data,size);
	}


}

void AppMixer::onMessageEnd(WebSocket *ws)
{
	Debug("-onMessageEnd %p\n",ws);
}

void AppMixer::onClose(WebSocket *ws)
{
	///Get user data
	void * user = ws->GetUserData();

	//Check if it is presenter
	if (user)
	{
		//Get viewer from user data
		Viewer* viewer = (Viewer*) user;
		//Lock
		use.WaitUnusedAndLock();
		//Erase it
		viewers.erase(viewer->GetId());
		//Delete it
		delete(viewer);
		//Unlcok
		use.Unlock();
	} else if (presenter && ws==presenter->ws) {
		//Lock
		use.WaitUnusedAndLock();
		//Delete presenter
		delete(presenter);
		//Remote presenter
		presenter = NULL;
		//Unlock
		use.Unlock();
	} 
}

void AppMixer::onError(WebSocket *ws)
{

}

AppMixer::Viewer::Viewer(int id,WebSocket *ws)
{
	//Store id
	this->id = id;
	//Store ws
	this->ws = ws;
}

int AppMixer::Viewer::Process(const BYTE* data,DWORD size)
{
	
}

AppMixer::Presenter::Presenter(WebSocket *ws)
{
	//Store ws
	this->ws = ws;
}

AppMixer::Presenter::~Presenter()
{
}


int AppMixer::Presenter::Init(VNCViewer::Listener *listener)
{
	//Init viewer
	return viewer.Init(this,listener);
}

int AppMixer::Presenter::End()
{
	//End viewer
	return viewer.End();
}

int AppMixer::Presenter::Process(const BYTE* data,DWORD size)
{
	Debug("-Process size:%d\n",size);
	//Puss to buffer
	buffer.push(data,size);
	//Signal
	wait.Signal();
	//Return written
	return size;
}

int AppMixer::Presenter::WaitForMessage(DWORD usecs)
{
	Debug(">WaitForMessage %d\n",buffer.length());

	//Lock
	wait.Lock();

	//Wait for data
	int ret = wait.WaitSignal(usecs*1000);

	//Unlock
	wait.Unlock();
	
	Debug("<WaitForMessage %d %d\n",buffer.length(),ret);

	return ret;
}

void AppMixer::Presenter::CancelWaitForMessage()
{
	wait.Cancel();
}

bool  AppMixer::Presenter::ReadFromRFBServer(char *out, unsigned int size)
{
	Debug(">ReadFromRFBServer requested:%d,buffered:%d\n",size,buffer.length());
	//Lock
	wait.Lock();

	//Bytes to read
	int num = size;
	
	//Check if we have enought data
	while(buffer.length()<num)
	{
		//Wait for mor data
		if (!wait.WaitSignal(0))
			//Error
			return 0;
	}

	//Read
	buffer.pop((BYTE*)out,num);

	//Unlock
	wait.Unlock();

	Debug("<ReadFromRFBServer requested:%d,returned:%d,left:%d\n",size,num,buffer.length());
	

	//Return readed
	return num;
}
bool AppMixer::Presenter::WriteToRFBServer(char *buf, int size)
{
	//Send data
	ws->SendMessage((BYTE*)buf,size);
	//OK
	return size;
}

int AppMixer::onFrameBufferSizeChanged(VNCViewer *viewer, int width, int height)
{
	// Set property's of YUV rescaller context
	av_opt_set_defaults(sws);
	av_opt_set_int(sws, "srcw",       width			,AV_OPT_SEARCH_CHILDREN);
	av_opt_set_int(sws, "srch",       height		,AV_OPT_SEARCH_CHILDREN);
	av_opt_set_int(sws, "src_format", AV_PIX_FMT_RGBA	,AV_OPT_SEARCH_CHILDREN);
	av_opt_set_int(sws, "dstw",       width			,AV_OPT_SEARCH_CHILDREN);
	av_opt_set_int(sws, "dsth",       height		,AV_OPT_SEARCH_CHILDREN);
	av_opt_set_int(sws, "dst_format", PIX_FMT_YUV420P	,AV_OPT_SEARCH_CHILDREN);
	av_opt_set_int(sws, "sws_flags",  SWS_FAST_BILINEAR	,AV_OPT_SEARCH_CHILDREN);

	// Init YUV rescaller context
	if (sws_init_context(sws, NULL, NULL) < 0)
		//Set errror
		return  Error("Couldn't init sws context\n");

	//If already got memory
	if (img)
		//Free it
		free(img);
	
	//Create number of pixels
	DWORD num = width*height;
	//Allocate memory
	img = (BYTE*) malloc(num*3/2+FF_INPUT_BUFFER_PADDING_SIZE+32);

	// paint the background in black for YUV
	memset(img	, 0		, num);
	memset(img+num	, (BYTE) -128	, num/2);

	//Set size
	if (output) output->SetVideoSize(width,height);

	return 1;
}

int AppMixer::onFrameBufferUpdate(VNCViewer *viewer,int x, int y, int w, int h)
{
	//Log("-onFrameBufferUpdate [%d,%d,%d,%d]\n",x,y,w,h);
	return 1;
}
int AppMixer::onFinishedFrameBufferUpdate(VNCViewer *viewer)
{
	DWORD width = viewer->GetWidth();
	DWORD height = viewer->GetHeight();

	Log("-onFinishedFrameBufferUpdate [%d,%d]\n",width,height);

	//Calc num pixels
	DWORD numpixels = width*height;

	//Alloc data
	AVFrame* in = avcodec_alloc_frame();
	AVFrame* out = avcodec_alloc_frame();

	//Set in frame
	in->data[0] = viewer->GetFrameBuffer();

	//Set size
	in->linesize[0] = width*4;

	//Alloc data
	out->data[0] = img;
	out->data[1] = out->data[0] + numpixels;
	out->data[2] = out->data[1] + numpixels / 4;

	//Set size for planes
	out->linesize[0] = width;
	out->linesize[1] = width/2;
	out->linesize[2] = width/2;

	//Convert
	sws_scale(sws, in->data, in->linesize, 0, height, out->data, out->linesize);

	//Put new frame
	if (output) output->NextFrame(img);
	
	return 1;
}