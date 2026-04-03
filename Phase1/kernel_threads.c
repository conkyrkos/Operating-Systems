#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_proc.h"
#include "kernel_cc.h"
#include "util.h"
#include "kernel_streams.h"

/** 
  @brief Create a new thread in the current process.
  */
Tid_t sys_CreateThread(Task task, int argl, void* args)
{
  //CHECK IF TASK EXISTS
  if(task == NULL){
    return NOTHREAD;
  }
  else{
  PCB* curproc = CURPROC;
  PTCB* newptcb = (PTCB*)xmalloc(sizeof(PTCB));
  initialize_PTCB(newptcb);
	newptcb->argl=argl;
  newptcb->args=args;
  newptcb->task=task;
  
   
  TCB* tcb= spawn_thread(curproc, start_multi_threading);
  tcb->ptcb = newptcb;
  newptcb->tcb = tcb;

  rlist_push_back(&curproc->ptcb_list,&newptcb->ptcb_list_node);


  curproc->thread_count++;
 
  
  wakeup(newptcb->tcb);

  return (Tid_t) newptcb;
  }
}

/**
  @brief Return the Tid of the current thread.
 */
Tid_t sys_ThreadSelf()
{
	return (Tid_t) (cur_thread()->ptcb);
}

/**
  @brief Join the given thread.
  */
int sys_ThreadJoin(Tid_t tid, int* exitval)
{ 
  
  rlnode* cur_ptcb_list = &CURPROC->ptcb_list;
  PTCB* ptcb = (PTCB*)tid;
  rlnode* returned_node = rlist_find((cur_ptcb_list),ptcb,NULL);
  
	if(returned_node == NULL || returned_node->ptcb->detached == 1 || sys_ThreadSelf() == tid){ /*checks if given thread exists,is the same or is detached*/
    return -1;
  } 

  returned_node->ptcb->refcount++;
  
  //CondVar cv = returned_node->ptcb->exit_cv;
  
  while(returned_node->ptcb->exited != 1 && returned_node->ptcb->detached != 1){
    kernel_wait(&returned_node->ptcb->exit_cv,SCHED_USER);  
  }

    returned_node->ptcb->refcount--;

  if(returned_node->ptcb->detached == 1){
    return -1;
  }

  if(exitval!= NULL){
    (*exitval) = returned_node->ptcb->exitval;//<-this returns a pointer on exitval
  }

  
  if(returned_node->ptcb->refcount == 0){
    rlist_remove(returned_node);
    free(returned_node->ptcb);
  }
  
  return 0;
}

/**
  @brief Detach the given thread.
  */
int sys_ThreadDetach(Tid_t tid)
{
  rlnode* cur_ptcb_list = &CURPROC->ptcb_list;
  rlnode* returned_node = rlist_find(cur_ptcb_list,(PTCB*)tid,NULL);

  if(returned_node == NULL || returned_node->ptcb->exited == 1){ /*checks if given thread exists*/
    return -1;
  }
  
  if(returned_node->ptcb->detached != 1)
  {
    returned_node->ptcb->detached = 1;
    kernel_broadcast(&returned_node->ptcb->exit_cv);
  }
  
  return 0;
}

/**
  @brief Terminate the current thread.
  */
void sys_ThreadExit(int exitval)
{

  /* Reparent any children of the exiting process to the 
       initial task */
    PCB *curproc = CURPROC;  
 
    cur_thread()->ptcb->exited = 1;
    
    cur_thread()->ptcb->exitval = exitval;
   
    kernel_broadcast(&(cur_thread()->ptcb->exit_cv));
    curproc->thread_count--;

  if(curproc->thread_count==0){
    if(get_pid(curproc)!=1){
      
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
      kernel_broadcast(& initpcb->child_exit);
    }

    /* Put me into my parent's exited list */
    rlist_push_front(& curproc->parent->exited_list, &curproc->exited_node);
    kernel_broadcast(& curproc->parent->child_exit);
    }
  assert(is_rlist_empty(& curproc->children_list));
  assert(is_rlist_empty(& curproc->exited_list));


  /* 
    Do all the other cleanup we want here, close files etc. 
   */

  /* Release the args data */
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

  /* Disconnect my main_thread */
  curproc->main_thread = NULL;

  /* Now, mark the process as exited. */
  curproc->pstate = ZOMBIE;
  }

  /* Bye-bye cruel world */
  kernel_sleep(EXITED, SCHED_USER);

}

