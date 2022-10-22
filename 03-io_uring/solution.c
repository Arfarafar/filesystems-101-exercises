#include <solution.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <liburing.h>


#define ENTRIES 4
#define BLOCKSIZE 256*1024


struct user_data {
    int isread;
    off_t offset;

    size_t bufsize;
    char* buf;
};



static int prep_read(struct io_uring *ring, size_t bufsize, off_t offset, int fd) {
    
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    if (!sqe)
        return 0;
    
    struct user_data *user_data = (struct user_data *)calloc(bufsize + sizeof(*user_data), 1);
    if (!user_data)
        return 0;

    user_data -> isread  = 1;
    user_data -> offset  = offset;
    user_data -> bufsize = bufsize;
    user_data -> buf     = (char*)user_data + sizeof(*user_data);

    io_uring_prep_read(sqe, fd, user_data -> buf, bufsize, offset);
    io_uring_sqe_set_data(sqe, user_data);
    return 1;
}

static int prep_write(struct io_uring *ring, struct user_data *user_data, int fd) {

    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    if (!sqe)
        return 0;

    user_data -> isread = 0;

    io_uring_prep_write(sqe, fd, user_data -> buf, user_data -> bufsize, user_data -> offset);
    io_uring_sqe_set_data(sqe, user_data);
    return 1;
}




int copy(int in, int out)
{
    
    struct io_uring ring;
    if(io_uring_queue_init(ENTRIES, &ring, 0) < 0) 
        return -errno;


    struct stat st;
    if (fstat(in, &st) < 0)
        return -errno;
    off_t left_to_read = st.st_size;
    off_t left_to_write = left_to_read;
    off_t offset = 0;

    int reads_queued = 0;
    int writes_queued = 0;
    
    struct io_uring_cqe *cqe;

    while (left_to_read || left_to_write || writes_queued){

        char read_happened = 0;
        while (left_to_read) {

            if (reads_queued + writes_queued == ENTRIES)
                break;

            size_t bufsize = left_to_read > BLOCKSIZE ? BLOCKSIZE : left_to_read;
            
            if (!prep_read(&ring, bufsize, offset, in))
                break;

            left_to_read -= bufsize;
            offset += bufsize;
            reads_queued++;
            read_happened = 1;
        }

        if(read_happened){
            if (io_uring_submit(&ring) < 0) {
                return -errno;
            }
        }


        char get_cqe_happened = 0;

        while (left_to_write || writes_queued){

            if(!get_cqe_happened){
                if(io_uring_wait_cqe(&ring, &cqe) < 0)
                    return -errno;
                
                get_cqe_happened = 1;
            } else {
                if(io_uring_peek_cqe(&ring, &cqe) == -EAGAIN)
                    break;
            }
                
            struct user_data *user_data = io_uring_cqe_get_data(cqe);

            if (cqe->res < 0 || (cqe -> res) < (user_data -> bufsize)) {
                free(user_data);
                return -EIO;
            }
            
            if (user_data -> isread){
                
                if(!prep_write(&ring, user_data, out))
                    break;

                io_uring_submit(&ring);
                reads_queued--;
                writes_queued++;
                left_to_write -= user_data -> bufsize;
            } 
            else {
                free(user_data);
                writes_queued--;
            }
            io_uring_cqe_seen(&ring, cqe);
        }

    }


    io_uring_queue_exit(&ring);
	return 0;
}


