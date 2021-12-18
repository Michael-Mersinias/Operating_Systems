
#include <assert.h>
#include "kernel_cc.h"
#include "kernel_proc.h"
#include "kernel_streams.h"
#include "tinyos.h"


/* 
 The process table and related system calls:
 - Exec
 - Exit
 - WaitPid
 - GetPid
 - GetPPid

 */

/* The process table */
PCB PT[MAX_PROC];
procinfo infoptr[MAX_PROC];
unsigned int process_count;

PCB* get_pcb(Pid_t pid)
{
  return PT[pid].pstate==FREE ? NULL : &PT[pid];
}

Pid_t get_pid(PCB* pcb)
{
  return pcb==NULL ? NOPROC : pcb-PT;
}

/* Initialize a PCB */
static inline void initialize_PCB(PCB* pcb)
{
  pcb->pstate = FREE;
  pcb->argl = 0;
  pcb->args = NULL;

  for(int i=0;i<MAX_FILEID;i++)
    pcb->FIDT[i] = NULL;

  rlnode_init(& pcb->children_list, NULL);
  rlnode_init(& pcb->exited_list, NULL);
  rlnode_init(& pcb->children_node, pcb);
  rlnode_init(& pcb->exited_node, pcb);
  pcb->child_exit = COND_INIT;
}


static PCB* pcb_freelist;

void initialize_processes()
{
  /* initialize the PCBs */
  for(Pid_t p=0; p<MAX_PROC; p++) {
    initialize_PCB(&PT[p]);
  }

  /* use the parent field to build a free list */
  PCB* pcbiter;
  pcb_freelist = NULL;
  for(pcbiter = PT+MAX_PROC; pcbiter!=PT; ) {
    --pcbiter;
    pcbiter->parent = pcb_freelist;
    pcb_freelist = pcbiter;
  }

  process_count = 0;

  /* Execute a null "idle" process */
  if(Exec(NULL,0,NULL)!=0)
    FATAL("The scheduler process does not have pid==0");
}


/*
  Must be called with kernel_mutex held
*/
PCB* acquire_PCB()
{
  PCB* pcb = NULL;

  if(pcb_freelist != NULL) {
    pcb = pcb_freelist;
    pcb->pstate = ALIVE;
    pcb_freelist = pcb_freelist->parent;
    process_count++;
  }

  return pcb;
}

/*
  Must be called with kernel_mutex held
*/
void release_PCB(PCB* pcb)
{
  pcb->pstate = FREE;
  pcb->parent = pcb_freelist;
  pcb_freelist = pcb;
  process_count--;
}


/*
 *
 * Process creation
 *
 */

/*
	This function is provided as an argument to spawn,
	to execute the main thread of a process.
*/
void start_main_thread()
{
  int exitval;

  Task call =  CURPROC->main_task;
  int argl = CURPROC->argl;
  void* args = CURPROC->args;

  exitval = call(argl,args);
  Exit(exitval);
}


/*
	System call to create a new process.
 */
Pid_t Exec(Task call, int argl, void* args)
{
  PCB *curproc, *newproc;
  
  Mutex_Lock(&kernel_mutex);

  /* The new process PCB */
  newproc = acquire_PCB();

  if(newproc == NULL) goto finish;  /* We have run out of PIDs! */

  if(get_pid(newproc)<=1) {
    /* Processes with pid<=1 (the scheduler and the init process) 
       are parentless and are treated specially. */
    newproc->parent = NULL;
  }
  else
  {
    /* Inherit parent */
    curproc = CURPROC;

    /* Add new process to the parent's child list */
    newproc->parent = curproc;
    rlist_push_front(& curproc->children_list, & newproc->children_node);

    /* Inherit file streams from parent */
    for(int i=0; i<MAX_FILEID; i++) {
       newproc->FIDT[i] = curproc->FIDT[i];
       if(newproc->FIDT[i])
          FCB_incref(newproc->FIDT[i]);
    }
  }


  /* Set the main thread's function */
  newproc->main_task = call;

  /* Copy the arguments to new storage, owned by the new process */
  newproc->argl = argl;
  if(args!=NULL) {
    newproc->args = malloc(argl);
    memcpy(newproc->args, args, argl);
  }
  else
    newproc->args=NULL;

  /* 
    Create and wake up the thread for the main function. This must be the last thing
    we do, because once we wakeup the new thread it may run! so we need to have finished
    the initialization of the PCB.
   */
  if(call != NULL) {
    newproc->main_thread = spawn_thread(newproc, start_main_thread);
    wakeup(newproc->main_thread);
  }


finish:
  Mutex_Unlock(&kernel_mutex);
  return get_pid(newproc);
}


/* System call */
Pid_t GetPid()
{
  return get_pid(CURPROC);
}


Pid_t GetPPid()
{
  return get_pid(CURPROC->parent);
}


static void cleanup_zombie(PCB* pcb, int* status)
{
  if(status != NULL)
    *status = pcb->exitval;

  rlist_remove(& pcb->children_node);
  rlist_remove(& pcb->exited_node);

  release_PCB(pcb);
}


static Pid_t wait_for_specific_child(Pid_t cpid, int* status)
{
  Mutex_Lock(& kernel_mutex);

  /* Legality checks */
  if((cpid<0) || (cpid>=MAX_PROC)) {
    cpid = NOPROC;
    goto finish;
  }

  PCB* parent = CURPROC;
  PCB* child = get_pcb(cpid);
  if( child == NULL || child->parent != parent)
  {
    cpid = NOPROC;
    goto finish;
  }

  /* Ok, child is a legal child of mine. Wait for it to exit. */
  while(child->pstate == ALIVE)
    Cond_Wait(& kernel_mutex, & parent->child_exit);
  
  cleanup_zombie(child, status);
  
finish:
  Mutex_Unlock(& kernel_mutex);
  return cpid;
}


static Pid_t wait_for_any_child(int* status)
{
  Pid_t cpid;
  Mutex_Lock(&kernel_mutex);

  PCB* parent = CURPROC;

  /* Make sure I have children! */
  if(is_rlist_empty(& parent->children_list)) {
    cpid = NOPROC;
    goto finish;
  }

  while(is_rlist_empty(& parent->exited_list)) {
    Cond_Wait(& kernel_mutex, & parent->child_exit);
  }

  PCB* child = parent->exited_list.next->pcb;
  assert(child->pstate == ZOMBIE);
  cpid = get_pid(child);
  cleanup_zombie(child, status);

finish:
  Mutex_Unlock(& kernel_mutex);
  return cpid;
}


Pid_t WaitChild(Pid_t cpid, int* status)
{
  /* Wait for specific child. */
  if(cpid != NOPROC) {
    return wait_for_specific_child(cpid, status);
  }
  /* Wait for any child */
  else {
    return wait_for_any_child(status);
  }

}


void Exit(int exitval)
{
  /* Right here, we must check that we are not the boot task. If we are, 
     we must wait until all processes exit. */
  if(GetPid()==1) {
    while(WaitChild(NOPROC,NULL)!=NOPROC);
  }

  /* Now, we exit */
  Mutex_Lock(& kernel_mutex);

  PCB *curproc = CURPROC;  /* cache for efficiency */

  /* Do all the other cleanup we want here, close files etc. */
  if(curproc->args) {
    free(curproc->args);
    curproc->args = NULL;
  }

  /* Clean up FIDT */
  for(int i=0;i<MAX_FILEID;i++) {
    if(curproc->FIDT[i] != NULL) {
      FCB_decref(curproc->FIDT[i]);
      curproc->FIDT[i] = NULL;
    }
  }

  /* Reparent any children of the exiting process to the 
     initial task */
  PCB* initpcb = get_pcb(1);
  while(!is_rlist_empty(& curproc->children_list)) {
    rlnode* child = rlist_pop_front(& curproc->children_list);
    child->pcb->parent = initpcb;
    rlist_push_front(& initpcb->children_list, child);
  }

  /* Add exited children to the initial task's exited list 
     and signal the initial task */
  if(!is_rlist_empty(& curproc->exited_list)) {
    rlist_append(& initpcb->exited_list, &curproc->exited_list);
    Cond_Broadcast(& initpcb->child_exit);
  }

  /* Put me into my parent's exited list */
  if(curproc->parent != NULL) {   /* Maybe this is init */
    rlist_push_front(& curproc->parent->exited_list, &curproc->exited_node);
    Cond_Broadcast(& curproc->parent->child_exit);
  }

  /* Disconnect my main_thread */
  curproc->main_thread = NULL;

  /* Now, mark the process as exited. */
  curproc->pstate = ZOMBIE;
  curproc->exitval = exitval;

  /* Bye-bye cruel world */
  sleep_releasing(EXITED, & kernel_mutex);
}


procinfo sysInfo[MAX_PROC];

int find_array_size(procinfo* array) {
  int i;
  int j;
  i = 0;
  j = 1;

  while(i<MAX_PROC) {
    if(PT[i].pstate == ALIVE || PT[i].pstate == ZOMBIE) {
      j++;
    }
    i++;
  }

  return j;
}


int find_max_useful(int j) {  // Function used to find the amount of ALIVE and ZOMBIE (NON-FREE) processes, so we know the exact array size needed
  int i = 0;
  while (j<MAX_PROC) {
    if (PT[i].pstate == ALIVE || PT[i].pstate == ZOMBIE) {
      i++;
    } 
    j++;
  }
  return i;
}


int my_procinfo_Read(void* this, char* buf, unsigned int size,int i) {  // A function in order to read elements from streamobj (element of an array of structs) to the buffer

  int j = 0;
  int flag = 0;
  int k = 0;


    if (((procinfo *)this)->alive == ALIVE || ((procinfo *)this)->alive == ZOMBIE) {
      j++;
      ((procinfo *)buf)->pid = ((procinfo *)this)->pid;
      ((procinfo *)buf)->ppid = ((procinfo *)this)->ppid;
      ((procinfo *)buf)->alive = ((procinfo *)this)->alive;
      ((procinfo *)buf)->thread_count = ((procinfo *)this)->thread_count;
      ((procinfo *)buf)->main_task = ((procinfo *)this)->main_task;
      ((procinfo *)buf)->argl = ((procinfo *)this)->argl;
    } 

  return 1;
}

static file_ops procinfo_ops = {
  .Read = my_procinfo_Read
};



// The following function begins with a loop in order to find all ALIVE or ZOMBIE (non free) processes, and assign their useful information to the respective element of the array infoptr.
// Infoptr is an array consisted of Procinfo (struct) elements. The procinfo struct has the following variables: pid, ppid, alive, thread_count, main_task, argl, args.
// Afterwards, we reserve a FCB by using the FCB_reserve function, and we assign the FCB (file control block) elements as we should (streamobj = infoptr, and streamfunc = my_procinfo_Read).
// In the end, the fid is returned so it can be used in our Read_2 function, which is identical to the Read function, but also stores a counter which is used to parse the infoptr array.
Fid_t OpenInfo()
{

  int i = 0;
  int j = 0;
  while(i < MAX_PROC){
    if(PT[i].pstate == ALIVE || PT[i].pstate == ZOMBIE) {
     // MSG("j is: %d",j);
     // MSG("i is: %d",i);
     // MSG("pid: %d",PT[i].infoptr->pid);
      infoptr[j].pid = i;
      PT[i].index_pid = j;
      
      if(PT[i].parent == NULL){
        infoptr[j].ppid = NOPROC;
      }
      else{
        infoptr[j].ppid = infoptr[PT[i].parent->index_pid].pid;
      }
      

      if(PT[i].pstate == ALIVE){
        MSG("\n ALIVE \n");
        infoptr[j].alive = 1;
      }
      else if(PT[i].pstate == ZOMBIE){
        infoptr[j].alive = 0;
      }
      
      infoptr[j].thread_count = 0;
      infoptr[j].main_task = PT[i].main_task;
      infoptr[j].argl = PT[i].argl;
      j++;
      //PT[i].args = (void*)malloc(PT[i].argl);  
      //memcpy(infoptr[i].args,PT[i].args,PT[i].argl); 

    }

    i++;

  }
  infoptr[j].pid = -1;
  FCB* fcb;
  Fid_t fid;

  FCB_reserve(1, &fid, &fcb);
  j = 0;
  int k = 0;

/*
  while(k<MAX_PROC) {
    if(PT[k].pstate == ALIVE || PT[k].pstate == ZOMBIE) {
      sysInfo[j].pid = infoptr[k].pid;
      sysInfo[j].ppid = infoptr[k].ppid;
      sysInfo[j].alive = infoptr[k].alive;
      sysInfo[j].thread_count = infoptr[k].thread_count;
      sysInfo[j].main_task = infoptr[k].main_task;
      sysInfo[j].argl = infoptr[k].argl;
     // MSG("\n ALIVE STATUS: %d ",sysInfo[j].alive);
     // MSG(" PID : %d \n",sysInfo[j].pid);
      //PT[j].args = (void*)malloc(PT[j].argl);  
      //memcpy(sysInfo[j].args,PT[j].args,PT[j].argl); 
      
      j++;
    }
    k++;
  }
*/

  // fcb kai velaki h fcb[0] kai teleia? (Mallon array kai teleia)

  fcb->streamobj = infoptr;
  fcb->refcount = 1;
  rlnode_init(&fcb->freelist_node,NULL);
  fcb->streamfunc = &procinfo_ops;

//  if((j+1)!=find_array_size(sysInfo)) {
//    MSG("SOMETHING IS WRONG!");
//  }

  //MSG("after reserve");
  //for (int hi = 0;hi<j+1;hi++) {
    //printf("sysInfo_pid: %d",sysInfo[hi].pid);
  //}
  //int temp_test = (get_fcb(fid)->streamobj);
  //MSG("\nFID IS: %d\n",temp_test);
  return fid;
}

