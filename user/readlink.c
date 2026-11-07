#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "kernel/param.h"
#include "user/user.h"

int main(int argc, char* argv[])
{
    char buff[MAXPATH];
    struct stat st;
    int fd,n;

    if (argc != 2) 
    {
        fprintf(2, "Usage: readlink path\n");
        exit(1);
    }

    if ((fd = open(argv[1], O_RDONLY | O_NOFOLLOW)) < 0) 
    {
        fprintf(2, "readlink: cannot open %s\n", argv[1]);
        exit(1);
    }
    if (fstat(fd, &st) < 0) 
    {
        fprintf(2, "readlink: cannot stat %s\n", argv[1]);
        close(fd);
        exit(1);
    }
    if (st.type != T_SYMLINK) 
    {
        fprintf(2, "readlink: %s: not a symbolic link\n", argv[1]);
        close(fd);
        exit(1);
    }
    if (st.size == 0 || st.size >= MAXPATH || (n = read(fd, buff, st.size)) != (int)st.size) 
    {
        fprintf(2, "readlink: %s: cannot read link\n", argv[1]);
        close(fd);
        exit(1);
    }

    buff[n]=0;
    printf("%s\n", buff);
    close(fd);
    exit(0);
}