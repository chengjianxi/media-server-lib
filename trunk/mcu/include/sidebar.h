/* 
 * File:   sidebar.h
 * Author: Sergio
 *
 * Created on 9 de agosto de 2012, 15:26
 */

#ifndef SIDEBAR_H
#define	SIDEBAR_H
#include "config.h"
#include <set>

class Sidebar
{
public:
	Sidebar();
	~Sidebar();

	void Update(int index,WORD *samples,DWORD len);
	void Reset();

	void AddParticipant(int id);
	bool HasParticipant(int id);
	void RemoveParticipant(int id);

	WORD* GetBuffer()	{ return mixer_buffer; }
public:
	static const DWORD MIXER_BUFFER_SIZE = 320;
private:
	typedef std::set<int> Participants;
private:
	//Audio mixing buffer
	WORD mixer_buffer[MIXER_BUFFER_SIZE];
	Participants participants;
};

#endif	/* SIDEBAR_H */

