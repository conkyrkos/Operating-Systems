#include "kernel_streams.h"
#include "kernel_dev.h"
#include "kernel_sched.h"
#include "kernel_cc.h"

static file_ops reader_file_ops = {
    .Open = NULL,
    .Read = pipe_read,
    .Write = NULL,
    .Close = pipe_reader_close
};

static file_ops writer_file_ops = {
    .Open = NULL,
    .Read = NULL,
    .Write = pipe_write,
    .Close = pipe_writer_close
};

int sys_Pipe(pipe_t* pipe)
{
	if(pipe==NULL){
		return -1;
	}
	
	Fid_t fid_array[2]; //0 for read, 1 for write
	FCB* fcb_array[2]; //0 for read, 1 for write
	//check if fcb exist 
	if(!FCB_reserve(2,fid_array,fcb_array)){
		return -1;
	}

	p_cb* pipe_cb = (p_cb*) xmalloc(sizeof(p_cb));
	
	if(pipe_cb == NULL){
		return -1;
	}
	
	pipe_cb->reader = fcb_array[0];
	pipe_cb->writer = fcb_array[1];

	pipe_cb->w_position = 0;
	pipe_cb->r_position = 0;
	
	pipe_cb->has_data = COND_INIT;
	pipe_cb->has_space = COND_INIT;
	
	fcb_array[0]->streamobj = pipe_cb;
	fcb_array[0]->streamfunc = &reader_file_ops;

	fcb_array[1]->streamobj = pipe_cb;
	fcb_array[1]->streamfunc = &writer_file_ops;

	pipe->read = fid_array[0]; // connect pipe with fid
	pipe->write = fid_array[1];
	
	return 0;
}


int pipe_write(void* pipecb_t, const char *buf, unsigned int n){
	p_cb* pipe = (p_cb*)pipecb_t;

	if(pipe->writer==NULL || pipe->reader==NULL || pipe == NULL){
		return -1;
	}
	int written_bytes = 0;

	while((pipe->w_position + 1) % PIPE_BUFFER_SIZE == pipe->r_position && pipe->reader != NULL) //check if buffer is full and if reader exists. [w+1 % buffer size = r]: when write is !size then we get w+1 else we get 0.
	{
		kernel_wait(&pipe->has_space, SCHED_PIPE); //wait until it receives "has space"
	}

	if(pipe->reader == NULL){ // if reader broke during wait time, return -1
		return -1;
	}

	while ((pipe->w_position + 1) % PIPE_BUFFER_SIZE != pipe->r_position && written_bytes < n)
	{
		pipe->BUFFER[pipe->w_position] = buf[written_bytes]; //write bytes in buffer
		written_bytes++;
		pipe->w_position++;

		if(pipe->w_position >= PIPE_BUFFER_SIZE){//check if write position reached the end, then send it back to the start(circular)
			pipe->w_position = 0;
		}
	}
	
	kernel_broadcast(&pipe->has_data);
	return written_bytes;
}

int pipe_read(void* pipecb_t, char *buf, unsigned int n){
	p_cb* pipe = (p_cb*)pipecb_t;

	if(pipe->reader==NULL || pipe == NULL){
		return -1;
	}
	int read_bytes = 0;

	while (pipe->w_position == pipe->r_position && pipe->writer != NULL) //check if pipe is empty, then wait until it has data 
	{
		kernel_wait(&pipe->has_data, SCHED_PIPE);
	}

	while (pipe->w_position != pipe->r_position && read_bytes < n) //check if pipe is not empty, and if it read all the bytes, else read bytes
	{
		buf[read_bytes] = pipe->BUFFER[pipe->r_position];
		read_bytes++;
		pipe->r_position++;

		if(pipe->r_position >= PIPE_BUFFER_SIZE){
			pipe->r_position = 0;
		}
	}

	kernel_broadcast(&pipe->has_space);
	
	return read_bytes;
}

int pipe_writer_close(void* _pipecb){
	
	p_cb* pipe = (p_cb*) _pipecb;
	
	if(pipe == NULL)
	{
		return -1;
	}
	
	pipe->writer = NULL;
	kernel_broadcast(&pipe->has_data); //if writer is closed, reader continues to work

	if(pipe->reader == NULL){
		free(pipe);
	}

	return 0;
}

int pipe_reader_close(void* _pipecb){
	p_cb* pipe = (p_cb*) _pipecb;

	if(pipe == NULL)
	{
		return -1;
	}

	pipe->reader = NULL;
	
	if(pipe->writer == NULL){
		free(pipe);           //if reader is closed, writer closes
	}
	
	return 0;
}

