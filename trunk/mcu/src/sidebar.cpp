/* 
 * File:   sidebar.cpp
 * Author: Sergio
 * 
 * Created on 9 de agosto de 2012, 15:26
 */

#include "sidebar.h"
#include <string.h>

Sidebar::Sidebar()
{
}

Sidebar::~Sidebar()
{
}

void Sidebar::Update(int index,WORD *samples,DWORD len)
{
	//Mix the audio
	for(int i = 0; i < len; ++i)
		//MIX
		mixer_buffer[i] += samples[i];
}

void Sidebar::Reset()
{
	//zero the mixer buffer
	memset(mixer_buffer, 0, MIXER_BUFFER_SIZE*sizeof(WORD));
}

void Sidebar::AddParticipant(int id)
{
	participants.insert(id);
}

bool Sidebar::HasParticipant(int id)
{
	return participants.find(id)!=participants.end();
}

void Sidebar::RemoveParticipant(int id)
{
	participants.erase(id);
}

	

	