#include "log.h"
#include "pipevideooutput.h"
#include <string.h>
#include <stdlib.h>

PipeVideoOutput::PipeVideoOutput(pthread_mutex_t* mutex, pthread_cond_t* cond)
{
	//Nos quedamos con los mutex
	videoMixerMutex = mutex;
	videoMixerCond  = cond;

	//E iniciamos el buffer
	buffer		= NULL;
	bufferSize	= 0;

	//Ponemos el cambio
	inited		= false;
	isChanged	= false;
	versionChanged	= false;
	version		= -1;
	videoWidth	= 0;
	videoHeight	= 0;
}

PipeVideoOutput::~PipeVideoOutput()
{
	//Si estaba reservado
	if (buffer!=NULL)
		//Liberamos memoria
		free(buffer);
}

int PipeVideoOutput::NextFrame(BYTE *pic)
{
	//Check pic
	if (!pic)
		return Error("-PipeVideoOuput called with null frame\n");

	//Check pic
	if (!buffer)
		return Error("-Null buffer, size not set\n");

	//Check if wer are inited
	if (!inited)
		//Exit
		return Error("-PipeVideoOutput calling NextFrame without been inited\n");
	
	//Bloqueamos
	pthread_mutex_lock(videoMixerMutex);

	//Copiamos
	memcpy(buffer,pic,bufferSize);

	//Ponemos el cambio
	isChanged = true;

	//Se�alizamos
	pthread_cond_signal(videoMixerCond);

	//Y desbloqueamos
	pthread_mutex_unlock(videoMixerMutex);

	return true;
}

int PipeVideoOutput::SetVideoSize(int width,int height)
{
	//Check it it is the same size
	if ((videoWidth==width) && (videoHeight==height))
		//Not changed
		return 0;
	
	//Lock
	pthread_mutex_lock(videoMixerMutex);

	//Freem memory
	if (buffer)
		free(buffer);
	
	//Store size
	videoWidth = width;
	videoHeight= height;

	//Check frame size
	bufferSize = (width*height*3)/2;
	//Get memory
	buffer = (BYTE*)malloc(bufferSize);

	//Unlock
	pthread_mutex_unlock(videoMixerMutex);
	
	//Changed
	return 1;
}

BYTE* PipeVideoOutput::GetFrame()
{
	//QUitamos el cambio
	isChanged = false;

	//Y devolvemos el buffer
	return buffer;
}

int PipeVideoOutput::Init()
{
	//Iniciamos
	inited = true;

	return true;
} 

int PipeVideoOutput::End()
{
	//Terminamos
	inited = false;

	return true;
} 

int PipeVideoOutput::IsChanged(DWORD version)
{
	//If not inited
	if (!inited)
		//Not changed
		return false;
	//Check if we are asking for the same answer than before
	if (this->version==version)
		//Return previous chanded
		return versionChanged;
	//Store version number
	this->version = version;
	//Store value for change associated to that version
	versionChanged = isChanged;
	//Have we changed?
	return isChanged;
};