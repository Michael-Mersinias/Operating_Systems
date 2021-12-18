
#include "tinyos.h"
#include "kernel_proc.h"
#include "kernel_streams.h"




// Mersinias Michail: 2013030057
// Motos Michail: 2013030143
// Goumagias Konstantinos: 2013030069


//Creating a pipe object
pipeCB pipeobj[1];

// First implementation of read

/*
int pipe_read(pipeCB *this, char *buf, unsigned int size) {
	int i;

	i = this->buff_start;
	this->buff_end = this->buff_start + size;
	
	if (this->buff_end > MAX_BUFFER_SIZE) {
		this->buff_end = MAX_BUFFER_SIZE - (this->buff_end - MAX_BUFFER_SIZE);
	}
	
	char c = '\0';
	int num = 0;

	while (i<this->buff_end) {
		if (c == '\n') {
			return 0;
		}
		
		if(! bios_read_serial(this->fid_1, &c))
		{
			// polling 
			while(! bios_read_serial(this->fid_1, &c));
		}
		*buf = c;
		buf++;
		num++;
		size--;

		if (i == MAX_BUFFER_SIZE) {
			buf = 0;
		}
		i++;
	}

	if (this->write_flag == 0) {
		return 0;
	}

	if (i < this->buff_end) {
		this->error_flag = 1;
		return -1;
	} 
	else {
		return num;
	}
}
*/

// Second implementation of read, seems to be working well

int pipe_read(void* this, char* buf, unsigned int size) { //Pipe_read: reads size bytes from this(a stream) to buf 
	//while (size > 0) {
	//strcpy(buf,this);
		//char c = this.buffer[i];
    	//strcpy(buf[i],c);
    	//*buf = &this[i];
    //	buf++;
    //	size--;
    //	i++;
	//}
	int i;
	//char * thisbuf = &(((pipeCB *)this)->buffer);
	for (int i=0;i<size;i++) {
 		*(buf+i)	 = ((pipeCB *)this)->buffer[i];
 	}
	if (((pipeCB *)this)->write_flag == 0) { //Returns 0 after the reading process if the pipe write end is closed
  		return 0;
 	}
 

	//MSG("\nBuf is: %s \n",buf);
    return size;
}


int pipe_read_close(void *this) { // Closing the read stream and the respective fid. Also setting a flag for error handling in our pipe_write function

	((pipeCB *)this)->read_flag = 0;
	//Close(((pipeCB *)this)->fid_2);

	if(((pipeCB *)this)->error_flag == 1){
		return -1;
	}
	return 0;

}

// First implementation of write

/*
int pipe_write(pipeCB *this, char *buf, unsigned int size) {
	int num=0;
	int i = this->buff_start;

	this->buff_end = this->buff_start + size;
	if (this->buff_end > MAX_BUFFER_SIZE) {
		this->buff_end = MAX_BUFFER_SIZE - (this->buff_end - MAX_BUFFER_SIZE);
	}
	
	if (this->read_flag == 0) {
		return -1;
	}

	/* Write a number of bytes equal to size. Return the number of bytes written 
	while(size > 0) {
		char c = *buf;

		if(bios_write_serial(this->fid_2, c))
		{
			size--;
			buf++;
			num++;
		}
		
		if (i == MAX_BUFFER_SIZE) {
			buf = 0;
		}
		i++;
	}
	
	if (i < this->buff_end) {
		this->error_flag = 1;
		return -1;
	} 
	else {
		return num;
	}

}
*/

// Second implementation of write, seems to be working well

int pipe_write(void* this, char* buf, unsigned int size) { //Pipe_write: writes size bytes from buf to this(a stream) 
	int i = 0;
	MSG("\n ENTER WRITE \n");
	//while (size > 0) {
	if (((pipeCB *)this)->read_flag == 0) { //Returns -1 if the pipe read end is closed
  		return -1;
 	}
	//strcpy(this,buf);
		//size--;
		//buf++;
		//i++;
 	//char * thisbuf = &(((pipeCB *)this)[0].buffer);
	for (int i=0;i<size;i++) {
 	((pipeCB *)this)->buffer[i] =*(buf+i) ;
 	}
	//}
	MSG("\n this is: %s\n",((pipeCB *)this)->buffer);
	return size;
}

int pipe_write_close(void *this) { // Closing the write stream and the respective fid. Also setting a flag for error handling in our pipe_read function

	((pipeCB *)this)->write_flag = 0; 
	//Close(((pipeCB *)this)->fid_1); 

	if(((pipeCB *)this)->error_flag == 1){
		return -1;
	}
	return 0;


}

static file_ops writefops = { //file operations for the pipe->write fid and the respective FCB  
  .Open = NULL,
  .Read = pipe_read,
  .Write = pipe_write, 
  .Close = pipe_write_close
};

static file_ops readfops = { //file operations for the pipe->read fid and the respective FCB  
  .Open = NULL,
  .Read = pipe_read, 
  .Write = pipe_write,
  .Close = pipe_read_close
};

int Pipe(pipe_t* pipe) // Function to create a pipe and initialize its control block as well as the two streams which will be created
{
	Fid_t fid[2];
  	FCB* fcb[2];
  	int resources_check;
  	int resources_check2;
  	// Reserving the necessary resources(fcb) and checking if they are exhausted
	resources_check = FCB_reserve(1, &fid[0], &fcb[0]);
	resources_check2 = FCB_reserve(1, &fid[1], &fcb[1]);
	MSG("\n resources: %d\n",resources_check);
	MSG("\n resources2: %d\n",resources_check2);
	if(resources_check == 0 || resources_check2 == 0) {
		return -1;
	}
	// Initializing the pipeCB control block for this pipe
	pipe->read = fid[0];
	pipe->write = fid[1];
	pipeobj[0].fid_1 = pipe->read;//pipe->read;
	pipeobj[0].fid_2 = pipe->write;//pipe->write;
	pipeobj[0].CV1 = COND_INIT;
	
	pipeobj[0].CV2 = COND_INIT;

	pipeobj[0].buff_start = 0;
	pipeobj[0].buff_end = MAX_BUFFER_SIZE;
	pipeobj[0].read_flag = 1;
	pipeobj[0].write_flag = 1;
	pipeobj[0].error_flag = 0;

	// Initializing the read and write FCB elements respectively
	fcb[0]->refcount = 1;
	fcb[0]->streamobj = pipeobj;
	fcb[0]->streamfunc = &readfops;
	rlnode_init(&fcb[0]->freelist_node,NULL);

	fcb[1]->refcount = 1;
	fcb[1]->streamobj = pipeobj	;
	fcb[1]->streamfunc = &writefops;
	rlnode_init(&fcb[1]->freelist_node,NULL);
	return 0;
}





// Not used anywhere, FCB_Reserve has its functionality instead

/*
Fid_t findNextFCB() { //Function to check the first available FIDT 
  int flag=0;
  int i=0;
  int fidt=0;

  while(flag==0) {
    if(CURPROC->FIDT[i]==NULL){
      flag=1;
      fidt = i;
      return fidt;
    }
    i++;
  }
  return -1;
}
*/