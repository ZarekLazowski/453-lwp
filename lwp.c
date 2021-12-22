#include <stdlib.h>
#include <stdio.h>
#include "lwp.h"

#define MAXTHREADCOUNT 24 /*Max threads allowed*/
#define BYTEPERWORD 8     /*For 64-bit, there are 8 bytes in a word*/
#define CALLERSTACK 3     /*Words we need to set up the caller*/


/* Here be the structure (Round Robin functions, other helpful list keeping
 *variables) for the default scheduler, the Round Robin. */

static thread schedHead;

static thread schedNextThread;

static int schedThreadCount = 0;

/*Round robin, stick new thread at the end, link end to the head of the list*/
void rrAdmit(thread new)
{
  thread end;
  
  /*If this is the first thread*/
  if(schedThreadCount == 0)
  {
    /*Mark as head of the list*/
    schedHead = new;

    /*Link next and previous to this first thread as well*/
    new->sched_one = new;
    new->sched_two = new;

    /*Select the first in queue as the current next thread*/
    schedNextThread = new;
  }

  /*Treat sched_one as previous and sched_two as next*/

  /*Grab the end of the list*/
  end = schedHead->sched_one;

  /*Link the end of the list to the new thread*/
  end->sched_two = new;
  new->sched_one = end;

  /*Link the beginning of the list to the new thread*/
  new->sched_two = schedHead;
  schedHead->sched_one = new;

  /*Increment schedulers thread count*/
  schedThreadCount++;
}

/*Find the first matching thread and remove it from the queue*/
void rrRemove(thread victim)
{
  thread current, prev, next;

  current = schedHead;

  /*If there is no one in the scheduler*/
  if(schedThreadCount == 0)
  {
    /*Do nothing*/
  }

  /*If there is one person in the scheduler*/
  else if(schedThreadCount == 1)
  {
    /*If the victim is the scheduler head*/
    if(victim == schedHead)
    {
      /*Set the scheduler head as null*/
      schedHead = NULL;

      /*Set the next thread also to null*/
      schedNextThread = NULL;

      schedThreadCount--;
    }
  }

  /*If there are more than one people in the scheduler*/
  else
  {
    do{
      /*If the current selected is the intended victim*/
      if(victim == current)
      {
      	/*Remove victim from linked list

      	 *Leave it up to the caller to free memory for this thread.
      	 */

      	/*If we are removing the head of the list*/
      	if(victim == schedHead)
      	{
      	  /*Move the head to the next*/
      	  schedHead = current->sched_two;
      	}

      	/*If we are removing the next thread*/
      	if(victim == schedNextThread)
      	{
      	  /*Move the next thread to the next*/
      	  schedNextThread = current->sched_two;
      	}

      	prev = current->sched_one; /*reference for the current's previous*/
      	next = current->sched_two; /*reference for the current's next*/
      	
      	next->sched_one = prev; /*Make the next's previous object prev*/
      	prev->sched_two = next; /*Make the prev's next object next*/

      	/*Decrement shcedulers thread count*/
      	schedThreadCount--;

      	/*Exit the loop*/
      	break;
      }

      /*If it isn't the victim, make current the next one*/
      current = current->sched_two;
    
    } while(victim != schedHead);

    /*Its possible that we could arrive here without ever finding the victim.
     *If that happens, I guess there was nothing to remove?
     */
  } 
}

/*Return object pointed at by nextThread. Shift nextThread to the next*/
thread rrNext()
{
  /*Make a quick reference to this thread*/
  thread temp = schedNextThread;

  /*If there is no next thread*/
  if(temp == NULL)
  {
    return NULL;
  }
  
  /*Otherwise move nextObject to the next in queue*/
  schedNextThread = schedNextThread->sched_two;

  /*Return the next object*/
  return temp;
}

/*A hidden structure with our Round Robin functions*/
static struct scheduler rrScheduler = {NULL, NULL, rrAdmit, rrRemove, rrNext};

/*An accessible struct pointer that can be used to call the Round Robin*/
scheduler RoundRobin = &rrScheduler;

/* Thus ends the structure for the Round Robin */



/*With a functioning round robin scheduler, we set course for the rest of the 
 *lwp functions*/

/*The installed scheduler*/
static scheduler effective;

/*Context for the current running thread*/
static thread libCurrent;

/*Head of linked list for library to keep track of threads*/
static thread libHead;

/*Number of threads created*/
static int libThreadCount = 0;

/*Context for the overarching system*/
static rfile *ogContext;

/*Trash register file to dump exited register values*/
static rfile *trash;

/*Quick and dirty check to see if lwp is running (for gettid)*/
static int running = 0;

void lwp_exit_for_realsies(thread deathStar)
{
  /*Take the thread out of the scheduler*/
  effective->remove(deathStar);
  
  /*Blow up the thread like the Death Star*/
  /*Free the stuff we malloc-ed in lwp_create, 
   *but also check so we don't double free accidentally*/
  
  if(deathStar)
  {
    if(deathStar->stack)
    {
      free(deathStar->stack);
    }
    free(deathStar);
  }

  /*Find next thread*/
  libCurrent = effective->next();
  
  /*If there was a next thread to run*/
  if(libCurrent)
  {
    swap_rfiles(trash, &(libCurrent->state));
  }

  /*If there wasn't a next thread, return to the original thread*/
  else
  {
    swap_rfiles(trash, ogContext);
  }
}

/*Terminates current lwp and frees its resources. sched->next() to get the next
 *thread. If there are no other threads, restores the original system thread.
 */
void lwp_exit(void)
{
  thread deathStar = libCurrent;

  /*Move stack pointer to ogContext's stack*/
  SetSP(ogContext->rsp);

  /*Really truly exit*/
  lwp_exit_for_realsies(deathStar);
}

/*Function for maintaining the linked list of created threads*/
void lwp_libList(thread new)
{
  /*Treat lib_one as previous and lib_two as next*/
  
  thread end;
  
  /*If there is no head thread*/
  if(!libHead)
  {
    /*Mark as head of the list*/
    libHead = new;

    /*Link next and previous to this first thread as well*/
    new->lib_one = new;
    new->lib_two = new;
  }

  /*Grab the end of the list*/
  end = libHead->lib_one;

  /*Link the end of the list to the new thread*/
  end->lib_two = new;
  new->lib_one = end;

  /*Link the beginning of the list to the new thread*/
  new->lib_two = libHead;
  libHead->lib_one = new;
}

/*Creates a new lightweight process which executes the given function (lwpfun)
 *with the given arguments. Allocates stacksize words in the stack. Returns 
 *the thread ID.
 */
tid_t lwp_create(lwpfun function, void *argument, size_t stacksize)
{
  thread new;
  unsigned long *stackPointer;

  /*Holds number of words to malloc, add two for alignment and lwp_exit*/
  int alignedStack = stacksize + 2;

  /*If the effective scheduler is not setup, make it the default scheduler*/
  if( !effective )
  {
    effective = RoundRobin;
  }
  
  /*Attempt to allocate memory for the thread context*/
  if( !(new = malloc(sizeof(context))) )
  {
    return NO_THREAD;
  }

  /*Stacks need to be aligned on a 16 byte boundry.*/
  /*If the given stacksize is odd*/
  if(alignedStack % 2 != 0)
  {
    /*Make it even*/
    alignedStack++;
  }
  
  /*Pretend we're playing house and create a fake stack for the thread*/
  if( !(stackPointer = malloc( alignedStack * BYTEPERWORD)) )
  {
    return NO_THREAD;
  }

  
  /*Set up caller frame and swap_rfile return frame*/
  /*Set the basepointer to point to lwp_exit return value*/
  stackPointer[alignedStack-2] = (unsigned long) &(stackPointer[alignedStack]);

  /*Set swap_rfile return as lwpfun pointer*/
  stackPointer[alignedStack-1] = (unsigned long) function;

  /*Set lwpfun return as the lwp_exit function pointer*/
  stackPointer[alignedStack] = (unsigned long) lwp_exit;
  

  /*Save argument to its respective register*/
  new->state.rdi = (unsigned long) argument;

  /*Initialize stack and base pointer to same value, top of stack, for when
   *swap_rfiles returns.
   */
  new->state.rsp = (unsigned long) &(stackPointer[stacksize]);
  new->state.rbp = (unsigned long) &(stackPointer[stacksize]);


  /*Initialize floating point registers*/
  new->state.fxsave = FPU_INIT;
  
  /*The value of the other registers arguably don't matter*/
  
  /*Mark the stacksize given to us*/
  new->stacksize = stacksize;

  /*Save pointer to threads stack environment*/
  new->stack = stackPointer;

  /*Admit the thread into the scheduler*/
  effective->admit(new);

  /*Insert into library's linked list*/
  lwp_libList(new);

  /*Increment num threads created, (avoid 0) and set as threadID*/
  new->tid = (tid_t) ++libThreadCount;
  
  /*Return new threads threadID*/
  return new->tid;
}

/*Returns the tid of the calling lwp or NO_THREAD if not called by a lwp*/
tid_t lwp_gettid(void)
{
  if(running)
    return libCurrent->tid;
  else
    return NO_THREAD;
}

/*Yields control to another lwp. Which one depends on the scheduler. Saves 
 *the current lwp context, picks the next one, restores that thread's context,
 * and returns.
 */
void  lwp_yield(void)
{
  /*Save a pointer to the old thread*/
  thread old = libCurrent;
  
  /*Grab the next thread in queue, set as current*/
  libCurrent = effective->next();

  /*If there was a next thread, run it. Otherwise return*/
  if(libCurrent)
  {
    swap_rfiles(&(old->state), &(libCurrent->state));
  }
}

/*Starts the lwp system. Saves the original context(for lwp_stop to use later),
 *picks a lwp and starts running. Returns immediately if no lwps
 */
void  lwp_start(void)
{
  thread next;

  /*If there was a suspended thread*/
  if(libCurrent)
  {
    /*Mark that we are running*/
    running = 1;
    /*Resume that context*/
    swap_rfiles(ogContext, &(libCurrent->state));
  }

  /*Otherwise we're starting from scratch*/
  else
  {
    /*Attempt to create a structure to house current register values, to return
     *to later.
     */
    if( !(ogContext = malloc(sizeof(rfile))) )
    {
      perror("alloc mem for original context");
      exit(EXIT_FAILURE);
    }

    /*Attempt to grab next thread in scheduler*/
    if( (next = effective->next()) )
    {
      /*If a thread was successfully grabbed:
       *1.Mark this thread as the current running thread
       *2.Save current registers in ogContext
       *3.Swap over into selected context and pray
       */
      libCurrent = next;

      /*Mark as running*/
      running = 1;
      
      swap_rfiles(ogContext, &(next->state));
    }

    /*If next returns 0, we skip the swap part and just return*/
  }
}

/*Stops the lwp system, suspends the current thread, and restores the original 
 *system thread. Does not destroy any existing contexts, and thread processing
 *will be restarted by a call to lwp_start()
 */
void lwp_stop(void)
{
  /*Mark that we have stopped running*/
  running = 0;
  
  /*Save register states for running thread*/
  swap_rfiles(&(libCurrent->state), ogContext);
}

/*Returns the thread corresponding to the given thread ID, or NULL if the ID 
 *is invalid
 */
thread tid2thread(tid_t tid)
{
  /*First check the head*/
  thread temp = libHead;

  do{
    /*If this is what we want*/
    if(temp->tid == tid)
    {
      return temp;
    }

    /*Choose the next one*/
    temp = temp->lib_two;
    
  } while(temp != libHead);

  /*If we exhaust the list of threads created, return NULL*/
  return NULL;
}


/*Causes the lwp package to use the given scheduler to choose the next process
 *to run. Transfers all threads from the old scheduler to the new one in next()
 *order. If scheduler is null, the library should return to round-robin 
 *scheduling.
 */
void  lwp_set_scheduler(scheduler fun)
{
  /*If there is no current scheduler there is nothing to pass around*/
  if(!effective)
  {
    if(!fun)
    {
      effective = RoundRobin;
    }
    else
    {
      effective = fun;
    }
  }
  
  /*If the given scheduler is void, default to RoundRobin*/
  else if(!fun)
  {
    /*If the current scheduler is not the default*/
    if(effective != RoundRobin)
    {
      /*Need to remove each thread from the effective scheduler and feed it to
       *into the round robin scheduler. 
       */
      thread temp;
      
      while((temp = effective->next()))
      {
      	effective->remove(temp);
      	RoundRobin->admit(temp);
      }

      /*Set round robin scheduler as the effective scheduler*/
      effective = &rrScheduler;
    }
  }
  
  /*If they pass the effective scheduler*/
  else if(fun == effective)
  {
    /*Do nothing*/
  }
  
  /*Else if a scheduler is given that is not in effect is passed*/
  else
  {
    /*Need to remove each thread from the effective scheduler and feed it to
     *into the passed scheduler. 
     */
    thread temp;
    
    while((temp = effective->next()))
    {
      effective->remove(temp);
      fun->admit(temp);
    }

    /*Set passed scheduler to the effective sheduler*/
    effective = fun;
  }
}

/*Returns the pointer to the current scheduler.*/
scheduler lwp_get_scheduler(void)
{
  return effective;
}
