#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
#include <signal.h>
#include "log.h"
#include "tools.h"
#include "audio.h"
#include "audiostream.h"

/**********************************
* AudioStream
*	Constructor
***********************************/
AudioStream::AudioStream(RTPSession::Listener* listener) : rtp(MediaFrame::Audio,listener)
{
	sendingAudio=0;
	receivingAudio=0;
	audioCodec=AudioCodec::PCMU;
	muted = 0;
}

/*******************************
* ~AudioStream
*	Destructor. 
********************************/
AudioStream::~AudioStream()
{
}

/***************************************
* SetAudioCodec
*	Fija el codec de audio
***************************************/
int AudioStream::SetAudioCodec(AudioCodec::Type codec,const Properties& properties)
{
	//Colocamos el tipo de audio
	audioCodec = codec;

	//Store properties
	audioProperties = properties;

	Log("-SetAudioCodec [%d,%s]\n",audioCodec,AudioCodec::GetNameFor(audioCodec));

	//Y salimos
	return 1;	
}

void AudioStream::SetRemoteRateEstimator(RemoteRateEstimator* estimator)
{
	//Set it in the rtp session
	rtp.SetRemoteRateEstimator(estimator);
}

/***************************************
* Init
*	Inicializa los devices 
***************************************/
int AudioStream::Init(AudioInput *input, AudioOutput *output)
{
	Log(">Init audio stream\n");

	//Iniciamos el rtp
	if(!rtp.Init())
		return Error("Could not open rtp\n");
	
	//Nos quedamos con los puntericos
	audioInput  = input;
	audioOutput = output;

	//Y aun no estamos mandando nada
	sendingAudio=0;
	receivingAudio=0;

	Log("<Init audio stream\n");

	return 1;
}

int AudioStream::SetLocalCryptoSDES(const char* suite, const char* key64)
{
	return rtp.SetLocalCryptoSDES(suite,key64);
}

int AudioStream::SetRemoteCryptoSDES(const char* suite, const char* key64)
{
	return rtp.SetRemoteCryptoSDES(suite,key64);
}

int AudioStream::SetLocalSTUNCredentials(const char* username, const char* pwd)
{
	return rtp.SetLocalSTUNCredentials(username,pwd);
}

int AudioStream::SetRemoteSTUNCredentials(const char* username, const char* pwd)
{
	return rtp.SetRemoteSTUNCredentials(username,pwd);
}

int AudioStream::SetRTPProperties(const Properties& properties)
{
	return rtp.SetProperties(properties);
}
/***************************************
* startSendingAudio
*	Helper function
***************************************/
void * AudioStream::startSendingAudio(void *par)
{
	AudioStream *conf = (AudioStream *)par;
	blocksignals();
	Log("SendAudioThread [%d]\n",getpid());
	pthread_exit((void *)conf->SendAudio());
}

/***************************************
* startReceivingAudio
*	Helper function
***************************************/
void * AudioStream::startReceivingAudio(void *par)
{
	AudioStream *conf = (AudioStream *)par;
	blocksignals();
	Log("RecvAudioThread [%d]\n",getpid());
	pthread_exit((void *)conf->RecAudio());
}

/***************************************
* StartSending
*	Comienza a mandar a la ip y puertos especificados
***************************************/
int AudioStream::StartSending(char *sendAudioIp,int sendAudioPort,RTPMap& rtpMap)
{
	Log(">StartSending audio [%s,%d]\n",sendAudioIp,sendAudioPort);

	//Si estabamos mandando tenemos que parar
	if (sendingAudio)
		//paramos
		StopSending();
	
	//Si tenemos audio
	if (sendAudioPort==0)
		//Error
		return Error("Audio port 0\n");


	//Y la de audio
	if(!rtp.SetRemotePort(sendAudioIp,sendAudioPort))
		//Error
		return Error("Error en el SetRemotePort\n");

	//Set sending map
	rtp.SetSendingRTPMap(rtpMap);

	//Set audio codec
	if(!rtp.SetSendingCodec(audioCodec))
		//Error
		return Error("%s audio codec not supported by peer\n",AudioCodec::GetNameFor(audioCodec));

	//Arrancamos el thread de envio
	sendingAudio=1;

	//Start thread
	createPriorityThread(&sendAudioThread,startSendingAudio,this,1);

	Log("<StartSending audio [%d]\n",sendingAudio);

	return 1;
}

/***************************************
* StartReceiving
*	Abre los sockets y empieza la recetpcion
****************************************/
int AudioStream::StartReceiving(RTPMap& rtpMap)
{
	//If already receiving
	if (receivingAudio)
		//Stop it
		StopReceiving();

	//Get local rtp port
	int recAudioPort = rtp.GetLocalPort();

	//Set receving map
	rtp.SetReceivingRTPMap(rtpMap);

	//We are reciving audio
	receivingAudio=1;

	//Create thread
	createPriorityThread(&recAudioThread,startReceivingAudio,this,1);

	//Log
	Log("<StartReceiving audio [%d]\n",recAudioPort);

	//Return receiving port
	return recAudioPort;
}

/***************************************
* End
*	Termina la conferencia activa
***************************************/
int AudioStream::End()
{
	//Terminamos de enviar
	StopSending();

	//Y de recivir
	StopReceiving();

	//Cerramos la session de rtp
	rtp.End();

	return 1;
}

/***************************************
* StopReceiving
* 	Termina la recepcion
****************************************/

int AudioStream::StopReceiving()
{
	Log(">StopReceiving Audio\n");

	//Y esperamos a que se cierren las threads de recepcion
	if (receivingAudio)
	{	
		//Paramos de enviar
		receivingAudio=0;

		//Cancel rtp
		rtp.CancelGetPacket();
		
		//Y unimos
		pthread_join(recAudioThread,NULL);
	}

	Log("<StopReceiving Audio\n");

	return 1;

}

/***************************************
* StopSending
* 	Termina el envio
****************************************/
int AudioStream::StopSending()
{
	Log(">StopSending Audio\n");

	//Esperamos a que se cierren las threads de envio
	if (sendingAudio)
	{
		//paramos
		sendingAudio=0;

		//Cancel
		audioInput->CancelRecBuffer();

		//Y esperamos
		pthread_join(sendAudioThread,NULL);
	}

	Log("<StopSending Audio\n");

	return 1;	
}


/****************************************
* RecAudio
*	Obtiene los packetes y los muestra
*****************************************/
int AudioStream::RecAudio()
{
	int 		recBytes=0;
	struct timeval 	before;
	SWORD		playBuffer[1024];
	const DWORD	playBufferSize = 1024;
	AudioDecoder*	codec=NULL;
	AudioCodec::Type type;
	DWORD		frameTime=0;
	DWORD		lastTime=0;
	
	Log(">RecAudio\n");
	
	//Inicializamos el tiempo
	gettimeofday(&before,NULL);

	//Mientras tengamos que capturar
	while(receivingAudio)
	{
		//Obtenemos el paquete
		RTPPacket *packet = rtp.GetPacket();
		//Check
		if (!packet)
			//Next
			continue;
		//Get type
		type = (AudioCodec::Type)packet->GetCodec();

		//Comprobamos el tipo
		if ((codec==NULL) || (type!=codec->type))
		{
			//Si habia uno nos lo cargamos
			if (codec!=NULL)
				delete codec;

			//Creamos uno dependiendo del tipo
			if ((codec = AudioCodecFactory::CreateDecoder(type))==NULL)
			{
				//Delete pacekt
				delete(packet);
				//Next
				Log("Error creando nuevo codec de audio [%d]\n",type);
				continue;
			}

			//Try to set native pipe rate
			DWORD rate = codec->TrySetRate(audioOutput->GetNativeRate());

			//Start playing at codec rate
			audioOutput->StartPlaying(rate);
		}

		//Lo decodificamos
		int len = codec->Decode(packet->GetMediaData(),packet->GetMediaLength(),playBuffer,playBufferSize);

		//Check len
		if (len>0)
		{
			//Obtenemos el tiempo del frame
			frameTime = packet->GetTimestamp() - lastTime;

			//Actualizamos el ultimo envio
			lastTime = packet->GetTimestamp();

			//Check muted
			if (!muted)
				//Y lo reproducimos
				audioOutput->PlayBuffer(playBuffer,len,frameTime);
		}


		//Aumentamos el numero de bytes recividos
		recBytes+=packet->GetMediaLength();

		//Delete
		delete(packet);
	}

	//Terminamos de reproducir
	audioOutput->StopPlaying();

	//Borramos el codec
	if (codec!=NULL)
		delete codec;

	//Salimos
	Log("<RecAudio\n");
	pthread_exit(0);
}

/*******************************************
* SendAudio
*	Capturamos el audio y lo mandamos
*******************************************/
int AudioStream::SendAudio()
{
	RTPPacket	packet(MediaFrame::Audio,audioCodec,audioCodec);
	SWORD 		recBuffer[512];
        int 		sendBytes=0;
        struct timeval 	before;
	AudioEncoder* 	codec;
	DWORD		frameTime=0;

	//Log
	Log(">SendAudio\n");

	//Check input
	if (!audioInput)
		//Error
		return Error("-SendAudio failed, audioInput is null");

	//Obtenemos el tiempo ahora
	gettimeofday(&before,NULL);

	//Creamos el codec de audio
	if ((codec = AudioCodecFactory::CreateEncoder(audioCodec,audioProperties))==NULL)
		//Error
		return Error("-SendAudio failed, could not create audio codec [codec:%d]\n",audioCodec);

	//Get codec rate
	DWORD rate = codec->TrySetRate(audioInput->GetNativeRate());

	//Start recording at codec rate
	audioInput->StartRecording(rate);

	//Get clock rate for codec
	DWORD clock = codec->GetClockRate();

	//Set it
	packet.SetClockRate(clock);

	//Get ts multiplier
	float multiplier = clock/rate;

	//Mientras tengamos que capturar
	while(sendingAudio)
	{
		//Incrementamos el tiempo de envio
		frameTime += codec->numFrameSamples*multiplier;

		//Capturamos 
		if (audioInput->RecBuffer(recBuffer,codec->numFrameSamples)==0)
		{
			Log("-sendingAudio cont\n");
			continue;
		}

		//Lo codificamos
		int len = codec->Encode(recBuffer,codec->numFrameSamples,packet.GetMediaData(),packet.GetMaxMediaLength());

		//Comprobamos que ha sido correcto
		if(len<=0)
			continue;

		//Set length
		packet.SetMediaLength(len);

		//Set frametiem
		packet.SetTimestamp(frameTime);

		//Lo enviamos
		rtp.SendPacket(packet,frameTime);
	}

	Log("-SendAudio cleanup[%d]\n",sendingAudio);

	//Paramos de grabar por si acaso
	audioInput->StopRecording();

	//Logeamos
	Log("-Deleting codec\n");

	//Borramos el codec
	delete codec;

	//Salimos
        Log("<SendAudio\n");
	pthread_exit(0);
}

MediaStatistics AudioStream::GetStatistics()
{
	MediaStatistics stats;

	//Fill stats
	stats.isReceiving	= IsReceiving();
	stats.isSending		= IsSending();
	stats.lostRecvPackets   = rtp.GetLostRecvPackets();
	stats.numRecvPackets	= rtp.GetNumRecvPackets();
	stats.numSendPackets	= rtp.GetNumSendPackets();
	stats.totalRecvBytes	= rtp.GetTotalRecvBytes();
	stats.totalSendBytes	= rtp.GetTotalSendBytes();

	//Return it
	return stats;
}

int AudioStream::SetMute(bool isMuted)
{
	//Set it
	muted = isMuted;
	//Exit
	return 1;
}