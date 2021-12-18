
#include "tinyos.h"
#include "kernel_proc.h"
#include "kernel_streams.h"


Mutex sock_mutex = MUTEX_INIT;


RCB *req;
pipeCB pipeobj[1];
socketCB socketobj[MAX_FILEID];  


Fid_t Socket(port_t port)
{

	if(port == NOPORT || port < 0 || port > MAX_PORT){
		return NOFILE;
	}

	FCB* fcb;
	Fid_t fid;
	int exhausted;

	exhausted = FCB_reserve(1,&fid,&fcb);			//reserves a fcb for the respective fid
	if(exhausted == 0){								//if available resouces are exhausted return NOFILE
		return NOFILE;
	}
	
	Fid_t ffid;			

	for(int i=0; i<MAX_FILEID; i++){				//initiallize a new socket
		if(socketobj[i].fid == NOFILE){				
			socketobj[i].fid = fid;
			ffid = socketobj[i].fid;
			socketobj[i].resources = 1;
			socketobj[i].ref_count = 1;
			socketobj[i].port = port;
			socketobj[i].socket_type = UNBOUND;
			socketobj[i].active_flag = 1;
			break;
		}
	}
	
	return ffid;		//fid of the newly created socket
}



int Listen(Fid_t sock)
{

	if(sock < 0 || sock > MAX_FILEID){
		return -1;
	}

	for(int i=0; i<MAX_FILEID; i++){	
		if(socketobj[i].fid == sock){

			if(socketobj[i].socket_type == UNBOUND || socketobj[i].port == NOPORT){
				return -1;
			}
		}
	}

	for(int i=0 ;i<MAX_FILEID; i++){
		if(socketobj[i].fid==sock){

			socketobj[i].socket_type = LISTENER;		//initiallize socket as LISTENER
			break;
		}
	}

	return 0;
}


Fid_t Accept(Fid_t lsock)
{

	if(lsock < 0 || lsock > MAX_FILEID){
		return NOFILE;
	}

	rlist_push_back(&req->requests,lsock); //create request

	for(int i=0; i<MAX_FILEID; i++){
		if(socketobj[i].fid == lsock){		//find the specific socket id
			if(socketobj[i].socket_type != LISTENER || socketobj[i].resources ==0)		//check conditions
				return NOFILE;
		}
	}

	Mutex_Lock(&sock_mutex);
	while(is_rlist_empty(&req->requests)==1){				//if request list is empty, wait for a new request
		int wait = Cond_Wait(&sock_mutex, &req->CV);
		if(wait==1)
			break;
	}
	Mutex_Unlock(&sock_mutex);
	
	
	Fid_t new_socketfid;
	port_t nport;

	for(int i=0; i<MAX_FILEID; i++){
		if(socketobj[i].fid == lsock){						
			new_socketfid = Socket(socketobj[i].port);		//creates a new sockets
			break;
		}
	}

	//Pipe(&pipe);			//here we need to create new pipes that will connect the sockets

	for(int i=0; i<MAX_FILEID; i++){		//both the lsock and the new socket become PEERS
		if(socketobj[i].fid == lsock){
			socketobj[i].socket_type = PEER;
		}
		
		if(socketobj[i].fid == new_socketfid){
			socketobj[i].socket_type = PEER;
			nport = socketobj[i].port;
		}
	}

	int connected = Connect(new_socketfid,nport,500);  //establish connection with the new socket

	if(connected == 0){
		return new_socketfid;
	}
	else{
		return NOFILE;
	}
}


int Connect(Fid_t sock, port_t port, timeout_t timeout)
{	

	if(sock < 0 || sock > MAX_FILEID || port < 0 || port > MAX_PORT){
		return -1;
	}

	for(int i=0; i<MAX_FILEID; i++){
		
		if(socketobj[i].fid == sock){
			if(socketobj[i].port == NOPORT)
				return -1;
		}
	}

	return 0;
}


int ShutDown(Fid_t sock, shutdown_mode how)
{
	if( sock < 0 || sock > MAX_FILEID){
		return -1;
	}

	for(int i=0; i<MAX_FILEID; i++){
		if(socketobj[i].fid==sock){

			if(how==SHUTDOWN_READ){  			//closes the read end
				pipeobj[0].read_flag = 0;
				Close(pipeobj[0].fid_1);

			}

			else if (how == SHUTDOWN_WRITE){ 	//closes the write end
				pipeobj->write_flag = 0;
				Close(pipeobj[0].fid_2);
			}
	
			else if (how == SHUTDOWN_BOTH){		//closes both
				
				pipeobj->read_flag = 0;
				Close(pipeobj[0].fid_1);

				pipeobj->write_flag = 0;
				Close(pipeobj[0].fid_2);

				socketobj[i].active_flag = 0;	//closes the socket
			}
			return 0;
		}
		break;
	}
}

