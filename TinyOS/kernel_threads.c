#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_proc.h"


/**********************************
      LAB30131008
Goumagias Konstantinos   2013030069
Mersinias Michail        2013030057
Motos Michail            2013030143
**********************************/



PTCB ptcb_table[MAX_PTHREAD];    // A PTCB table that contains all threads regardless of the process owner.
                                // Identification is accomplished either by using tcb->thread_idx (making use of the thread_owner pointer)
                                // which is identical to the id of the current ptcb or by the owned_by_pcb pointer.

Mutex my_mutex=MUTEX_INIT;    // We create our own mutex


void start_thread()			//Function that is needed in order to call ThreadExit after a thread is finished
{
  int exitval;

  Task call =  ptcb_table[ThreadSelf()].main_task;
  int argl = ptcb_table[ThreadSelf()].argl;
  void* args = ptcb_table[ThreadSelf()].args;

  exitval = call(argl,args);
  ThreadExit(exitval);
}


/** 
  @brief Create a new thread in the current process.
  */

Tid_t CreateThread(Task task, int argl, void* args)
{
  
  Mutex_Lock(&my_mutex);
  MSG("\n Creating Thread...");
  //PTCB* ptcb= NULL;
  //ptcb = (PTCB*)xmalloc(sizeof(PTCB));

  TCB* new_thread=spawn_thread_2(CURPROC,(void *)start_thread,task,argl,args);  //Creating a new thread using the spawn_thread_2 function 
																		                                        		//which functions the same as spanw_thread plus initiallization of 
  																			                                       	//ptcb_table and calculation of index_tid
  


  wakeup(new_thread); 
  Mutex_Unlock(&my_mutex);
  MSG("\n Created Thread \n");
  return new_thread->tid;
}


Tid_t FindNextAvailable() {				//Function to find the next available thread in the ptcb_table
  int flag=0;
  int i=0;
  int ptid=0;

  while(flag==0) {
    if(ptcb_table[i].owned_by_pcb==NULL){
      flag=1;
      ptid = i;
      return ptid;
    }
    i++;
  }
  return -1;
}


/**
  @brief Return the Tid of the current thread.
 */
Tid_t ThreadSelf()
{
	return  CURTHREAD->index_tid;
}

/**
  @brief Join the given thread.
  */
int ThreadJoin(Tid_t tid, int* exitval)
{
	if (ptcb_table[tid].owned_by_pcb != CURPROC || ptcb_table[tid].owned_by_pcb == NULL) {		//there is no thread with the given tid in this process
		return -1;
	}
	if (ptcb_table[tid].thread_owner == CURTHREAD) {		//the tid corresponds to the current thread
		return -1;
	}
	if (ptcb_table[tid].detach_flag == 1) {			//the tid corresponds to a detached thread
		return -1;
	}
	MSG("\n Joining Thread... ");
	Mutex_Lock(&my_mutex);
	if(exitval != NULL){
		*exitval=ptcb_table[tid].exitval;			//returning the exival of the tid thread
	}
	
	ptcb_table[tid].ref_cnt++;						//increasing the counter of the threads that have joined the tid thread
	Cond_Wait(&my_mutex, (&ptcb_table[tid].signal));//making the current thread sleep and wait until the condition variable 
													              //signal of the tid thread is awaken by a cond_signal or a cond_broadcast
	Mutex_Unlock(&my_mutex);
  MSG("\n Joined Thread ");
	return 0;
}

/**
  @brief Detach the given thread.
  */
int ThreadDetach(Tid_t tid)
{

  if (tid >= MAX_THREAD || tid<0 || ptcb_table[tid].exitval == 0) {		//there is no thread with the given tid in this process or the tid corresponds to an exited thread
    return -1;
  }
  if (ptcb_table[tid].detach_flag == 1) {		//the tid corresponds to a detached thread
    return 0;
  }
  MSG("\n Detaching Thread... ");
  Mutex_Lock(&my_mutex);
  if(ptcb_table[ThreadSelf()].signal.waitset!=NULL){		
  	Cond_Broadcast(&ptcb_table[ThreadSelf()].signal);	//if other threads wait for the detached thread to finish, 
  														//we wake them up by using Cond_Broadcast
  }

  ptcb_table[tid].detach_flag = 1;
  Mutex_Unlock(&my_mutex);
  MSG("\n Detached Thread ");
  return 0;
}


int isCOND_INIT(CondVar a) 			//function to check if the condition variable is at state COND_INIT (therefore the waitset is empty)
{	
	if(a.waitset==NULL && a.waitset_lock==0){
		return 1;
	}
	return 0;
}

/**
  @brief Terminate the current thread.
  */
void ThreadExit(int exitval)
{
  MSG("\n Exiting Thread... ");
	Mutex_Lock(&my_mutex);

	while (ptcb_table[ThreadSelf()].ref_cnt>1) {			//we wake up all sleeping threads corresponding to the condition variable signal 
															//of the thread that is about to exit and adjust the counter accordingly
  		Cond_Signal(&ptcb_table[ThreadSelf()].signal);
  		ptcb_table[ThreadSelf()].ref_cnt--;
  	} 
  	ptcb_table[ThreadSelf()].ref_cnt--;

    if(ptcb_table[ThreadSelf()].ref_cnt==0) {				//in the end we clear the PTCB resources
    	ClearPTCB(exitval);
   
    	//free(ptcb_table[ThreadSelf()].owned_by_pcb->ptcb_ptr);		
    	//release_TCB(&CURTHREAD);
    }
  	Mutex_Unlock(&my_mutex);
    MSG("\n Exited Thread ");
}


/**
  @brief Awaken the thread, if it is sleeping.

  This call will set the interrupt flag of the
  thread.
  */
int ThreadInterrupt(Tid_t tid)			//we enable the interrupt flag and change the tid thread state to READY 
{
  MSG("\n Interrupting Thread... ");
  if(ptcb_table[tid].thread_owner == NULL) {
    return -1;
  }
  else {
    Mutex_Lock(&my_mutex);
    ptcb_table[tid].thread_owner->interrupt_flag = 1;     
    wakeup(ptcb_table[tid].thread_owner);
    Mutex_Unlock(&my_mutex);
    MSG("\n Interrupted Thread ");
    return 0;
  }
}


/**
  @brief Return the interrupt flag of the 
  current thread.
  */
int ThreadIsInterrupted()				//returns the interrupt flag of the current thread
{
	return CURTHREAD->interrupt_flag;
}

/**
  @brief Clear the interrupt flag of the
  current thread.
  */
void ThreadClearInterrupt()				//we disable the interrupt flag by setting it to 0
{
  Mutex_Lock(&my_mutex);
	CURTHREAD-> interrupt_flag = 0;
  Mutex_Unlock(&my_mutex);
}

void ClearPTCB(int exitval) 			//clearing the PTCB resources 
{
	ptcb_table[ThreadSelf()].owned_by_pcb = NULL;
	ptcb_table[ThreadSelf()].signal = COND_INIT;
	ptcb_table[ThreadSelf()].main_task = NULL;
	ptcb_table[ThreadSelf()].exitval = exitval;
	ptcb_table[ThreadSelf()].detach_flag = -1;
	ptcb_table[ThreadSelf()].argl = -1;
	ptcb_table[ThreadSelf()].args = NULL;
	ptcb_table[ThreadSelf()].ptid = -1;
	ptcb_table[ThreadSelf()].ref_cnt = -1;
  ptcb_table[ThreadSelf()].thread_owner=NULL;
}


Tid_t InitializePTCB(PCB* pcb,TCB* new_thread,Task task, int argl, void* args)		//initiallizing the PTCB resources
{
  Tid_t tid= new_thread->index_tid;
  ptcb_table[tid].ref_cnt = 1;
  ptcb_table[tid].detach_flag = 0;
  ptcb_table[tid].owned_by_pcb = pcb;
  ptcb_table[tid].ptid = tid;
  ptcb_table[tid].signal = COND_INIT;
  ptcb_table[tid].exitval = 1;
  ptcb_table[tid].main_task = task;
  ptcb_table[tid].argl = argl;
  ptcb_table[tid].args=(void*)malloc(argl);	 
  memcpy(ptcb_table[tid].args,args,argl);	
  ptcb_table[tid].thread_owner=new_thread;
  //pcb->ptcb_ptr = &ptcb_table[tid];
  return tid;
}
